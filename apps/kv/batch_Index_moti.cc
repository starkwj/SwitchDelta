// #include "../../rdma/operation.h"
// #include "masstree_key.hh"
#include "qp_info.h"
#include "rpc.h"
#include "zipf.h"
#include "sample.h"
#include <cassert>
#include <cmath>
#include <iterator>
#include "include/index_wrapper.h"
#include "include/test.h"


DEFINE_uint32(batch, 1024, "batch size");
DEFINE_string(index, "tree", "index type");
DEFINE_uint64(keyspace, 100 * MB, "key space");

enum RpcType {
	kReadReq = 0,
	kReadReply,
	kWriteReq,
	kWriteReply,
};

struct SimpleCtx {
	enum RpcType op;
	uint64_t key;
	uint64_t value;
	uint8_t server_id;
	uint8_t thread_id;
};

struct BatchHeader {
	// route header
	uint8_t dst_s_id;

	// rpc header
	uint8_t src_s_id;      // 1<<3
	uint8_t src_t_id;      // 1<<4
	uint8_t fake_src_c_id; // 1<<4
	uint8_t local_id;
	uint8_t op;
	uint32_t src_c_id;

} __attribute__((packed));

const int kMaxClientReq = 8;

rdma::GlobalConfig<rpc::kConfigServerCnt> global_config;
std::vector<rdma::ServerConfig> server_config;
// const int kClientId = 1;


struct RawKvMsg {
	BatchHeader rpc_header;
	uint64_t key;
	uint64_t value;
	static uint32_t get_size(int op = -1) {
		switch (op) {
		case kReadReq:
			return sizeof(RawKvMsg);
		case kReadReply:
			return sizeof(RawKvMsg);
		case kWriteReq:
			return sizeof(RawKvMsg);
		case kWriteReply:
			return sizeof(RawKvMsg);
		default:
			return sizeof(RawKvMsg); // max size
			break;
		}
	}
} __attribute__((packed));

struct ReqInfo {
	RawKvMsg req;
	rdma::QPInfo* qp_info;
}__attribute__((packed));


struct Batch_req {
	ReqInfo** req_ptr;
	ReqInfo* req;
	Batch_req() {
		req = (ReqInfo*)malloc(sizeof(ReqInfo) * FLAGS_batch);
		req_ptr = (ReqInfo**)malloc(sizeof(ReqInfo*) * FLAGS_batch);
	}

	static bool cmp(const ReqInfo* a, const ReqInfo* b) {
		if ((*a).req.key == (*b).req.key) {
			if ((*a).req.rpc_header.op == (*b).req.rpc_header.op)
				return (uint64_t)a > (uint64_t)b;
			return (*a).req.rpc_header.op > (*b).req.rpc_header.op;
		}
		return (*a).req.key < (*b).req.key;
	}

	static bool cmp_hash(const ReqInfo* a, const ReqInfo* b) {
		if ((*a).req.key == (*b).req.key) {
			if ((*a).req.rpc_header.op == (*b).req.rpc_header.op)
				return (uint64_t)a > (uint64_t)b;
			return (*a).req.rpc_header.op > (*b).req.rpc_header.op;
		}
		// (std::hash<uint>()(i))
		return std::hash<uint>()((*a).req.key) < std::hash<uint>()((*b).req.key);
	}

	void sort_batch() {
		if (FLAGS_index == "tree")
			std::sort(req_ptr, req_ptr + FLAGS_batch, cmp);
		else 
			std::sort(req_ptr, req_ptr + FLAGS_batch, cmp_hash);
	}
	void print() {
		for (uint32_t i = 0; i < FLAGS_batch; i++) {
			printf("key=%lx op=%d id=%lx\n", req_ptr[i]->req.key, req_ptr[i]->req.rpc_header.op, (uint64_t)req_ptr[i]);
		}
		puts("-----------");
	}
};


inline void generate_key(uint i, uint64_t& key) {
	// uint64_t tmp = rand();

	// key_space[i] = ((uint64_t)(std::hash<uint>()(i)) << 32) + std::hash<uint>()(i);
	// key = (tmp << 32) + rand();
	// key = ((uint64_t)(std::hash<uint>()(i)) << 32) + std::hash<uint>()(i * i + i + 1);
	key = ((uint64_t)(std::hash<uint>()((i + 1) * 19210817)) << 32) + std::hash<uint>()(i * 10000079);
	// key = i + 1;
}
uint64_t* key_space;

class SimpleRpc: public rpc::Rpc {
private:
	// server
	
	struct zipf_gen_state state;
	struct zipf_gen_state op_state;
	static const int kCoroWorkerNum = kMaxClientReq;
	SimpleCtx batch_simple_ctx[kCoroWorkerNum];

	ThreadLocalIndex_masstree* tree_index;
	ThreadLocalIndex* hash_index;

	CoroSlave* batch_coro_slave[kCoroWorkerNum];  // for batch
	rdma::CoroCtx batch_coro_ctx[kCoroWorkerNum]; // for batch
	
	Batch_req batch_req;
	uint32_t batch_cnt = 0;
public:
	
	void batch_process() {
		reporter->begin(0,0,0);
		// batch_req.sort_batch();
		// batch_req.print();

		uint64_t last_key = (uint64_t)(-1);
		int real_cnt = 0;

		{
			for (uint i = 0; i < batch_cnt; i++) {
				auto qp = batch_req.req_ptr[i]->qp_info;
				auto reply = qp->get_send_msg_addr();
				auto& req = batch_req.req_ptr[i]->req;
				if (last_key == req.key) {
					server_process_msg(&req, reply, qp, &batch_req.req_ptr[i - 1]->req);
				}
				else {
					server_process_msg(&req, reply, qp, NULL);
					real_cnt++;
				}
				if (qp->type == 2) {
					agent->modify_ud_ah(req.rpc_header.src_s_id, req.rpc_header.src_t_id, qp->qp_id);
					// ah;
				}
				last_key = req.key;
				qp->append_signal_smsg();
				if (rpc::FLAGS_qp_type == 0)
					qp->post_appended_smsg();
			}
			if (rpc::FLAGS_qp_type == 2)
				agent->get_ud_qp(0).post_appended_smsg();
		}
		
		uint64_t res = reporter->many_end(batch_cnt, 0,0,0);
		reporter->many_end_copy(real_cnt, 0,0,1, res);
		batch_cnt = 0;
	}

	void run_server_CPC() {

		init();

		server_prepare();

		connect();

		batch_cnt = 0;
		while (true) {
			auto wc_array = agent->cq->poll_cq_try(16);
			// rdma::QPInfo* qp_array[16];
			// int cnt = 0;

			ibv_wc* wc = nullptr;
			while ((wc = wc_array->get_next_wc())) {
				auto qp = ((rdma::WrInfo*)wc->wr_id)->qp;
				auto req = qp->get_recv_msg_addr();
				batch_req.req_ptr[batch_cnt] = &batch_req.req[batch_cnt];
				batch_req.req[batch_cnt].req = *(RawKvMsg*)req;
				batch_req.req[batch_cnt].qp_info = qp;
				batch_cnt++;
				qp->free_recv_msg();
				if (batch_cnt == FLAGS_batch) {
					batch_process();
				}
			}
		}
	}

	void client_worker_(CoroMaster &sink, uint32_t coro_id) {
		sink();

		auto &ctx = batch_coro_ctx[coro_id];
		while (true) {
		ctx.free = false;

		cworker_new_request_(sink, coro_id);

		ctx.free = true;
		sink();
		}
	}

	void run_client_CPC() {

		init();

		client_prepare();

		connect();

		sleep(1); // !!! wait for remote server !!!

		int coro_num = std::min(rpc::FLAGS_coro_num, kMaxClientReq);

		for (int i = 0; i < coro_num; i++) {
			using std::placeholders::_1;
			batch_coro_slave[i] =
				new CoroSlave(std::bind(&SimpleRpc::client_worker_, this, _1, i));
			batch_coro_ctx[i].async_op_cnt = -1;
		}

		while (true) {

			for (int i = 0; i < coro_num; i++) {
				if (batch_coro_ctx[i].free) {
					(*batch_coro_slave[i])(); // new request
				}
				if (batch_coro_ctx[i].async_op_cnt == 0 && batch_coro_ctx[i].free == false) {
					batch_coro_ctx[i].async_op_cnt = -1;
					(*batch_coro_slave[i])(); // one-sided RDMA
				}
			}

			auto wc_list = agent->cq->poll_cq_try(16);
			ibv_wc* wc = nullptr;
			while ((wc = wc_list->get_next_wc())) {

				auto qp = ((rdma::WrInfo*)wc->wr_id)->qp;
				auto reply = qp->get_recv_msg_addr();
				// puts("new reply");
				// auto& reply_header = *(RpcHeader*)reply;

				int32_t c_id = client_get_cid_32(reply);
				if (c_id != -1) {
					batch_coro_ctx[c_id].msg_ptr = reply;
					(*batch_coro_slave[c_id])();
					// after_process_reply(reply, c_id);
				}

				qp->free_recv_msg();
			}
		}
		return;
	}

	void server_prepare() {

		reporter = perf::PerfTool::get_instance_ptr();
		reporter->worker_init(thread_id);

		return; // FIXME:

		if (FLAGS_index == "hash") {
			hash_index = ThreadLocalIndex::get_instance("cuckoo64");
			for (uint64_t i = 0; i < FLAGS_keyspace; i++) {
				hash_index->put_long(key_space[i],i);
			}
		}
		else if (FLAGS_index == "tree") {
			tree_index = ThreadLocalIndex_masstree::get_instance();
			tree_index->thread_init(thread_id);
			for (uint64_t i = 0; i < FLAGS_keyspace; i++) {
				tree_index->put_long(key_space[i],i);
			}
		}

		
	}

	void server_process_msg(void* req_addr, void* reply_addr, rdma::QPInfo* qp) {
		return;
	}

	void read(uint64_t key, uint64_t* value) {
		if (FLAGS_index == "hash") {
			hash_index->get_long(key, value);
		}
		else if (FLAGS_index == "tree") {
			tree_index->get_long(key, value);
		}
	}

	void write(uint64_t key, uint64_t value) {
		return; // FIXME:
		if (FLAGS_index == "hash") {
			hash_index->put_long(key, value);
		}
		else if (FLAGS_index == "tree") {
			tree_index->put_long(key, value);
		}
	}
	void server_process_msg(void* req_addr, void* reply_addr, rdma::QPInfo* qp, void* last_req_addr) {
		auto req = (RawKvMsg*)req_addr;
		auto reply = (RawKvMsg*)reply_addr;
		auto last_req = (RawKvMsg*)last_req_addr;

		reply->rpc_header = req->rpc_header;
		reply->key = req->key;

		switch (req->rpc_header.op) {
		case kReadReq:
			reply->rpc_header.op = kReadReply;
			if (last_req == NULL) {
				read(reply->key, &(reply->value));
			}
			else {
				reply->value = req->value;
			}
			req->value = reply->value;
			// reporter->end(thread_id, 0, 0);
			break;
		case kWriteReq:
			reply->rpc_header.op = kWriteReply;
			if (last_req == NULL) {
				write(req->key, req->value);
			}
			else {
				reply->value = req->value;
				req->value = last_req->value;
			}
			break;
		default:
			printf("error op= %d\n", req->rpc_header.op);
			exit(0);
			break;
		}
		qp->modify_smsg_size(reply->get_size(reply->rpc_header.op));
	}

	// worker
	void client_prepare() {
		mehcached_zipf_init(&state, FLAGS_keyspace, 0.99, thread_id + rand() % 1000);
		mehcached_zipf_init(&op_state, 100, 0.0, thread_id + rand() % 1000);
		
		// for (uint64_t i = 0; i < FLAGS_keyspace; i++) {
		// 	generate_key(i, key_space[i]);
		// }

		reporter = perf::PerfTool::get_instance_ptr();
		reporter->worker_init(thread_id);
		for (int i = 0; i < rpc::FLAGS_coro_num; i++)
			batch_coro_ctx[i].app_ctx = &batch_simple_ctx[i];
		
	}

	void cworker_new_request(CoroMaster& sink, uint8_t coro_id) {
		return;
	}
	void cworker_new_request_(CoroMaster& sink, uint32_t coro_id) {
		auto& app_ctx = batch_simple_ctx[coro_id];
		auto& ctx = batch_coro_ctx[coro_id];
		reporter->begin(thread_id, coro_id, 0);

		app_ctx.server_id = 0;
		app_ctx.thread_id = 0;
		uint8_t qp_id = coro_id % rpc::kConfigMaxQPCnt;

		// app_ctx.data1 = mehcached_zipf_next(&state) + 1;
		// app_ctx.data2 = mehcached_zipf_next(&state) + 1;
		app_ctx.op = mehcached_zipf_next(&op_state) >= 100 ? kReadReq : kWriteReq;
		app_ctx.key = key_space[mehcached_zipf_next(&state)];
		// app_ctx.key = mehcached_zipf_next(&state);
		// qp
		rdma::QPInfo* qp = rpc_get_qp(rpc::FLAGS_qp_type, app_ctx.server_id,
			app_ctx.thread_id, qp_id);
		RawKvMsg* msg = (RawKvMsg*)qp->get_send_msg_addr();

		msg->rpc_header = {
			.dst_s_id = app_ctx.server_id,
			.src_s_id = server_id,
			.src_t_id = thread_id,
			.fake_src_c_id = (uint8_t)coro_id,
			.local_id = 0,
			.op = (uint8_t)app_ctx.op,
			.src_c_id = coro_id,
		};

		// msg->data1 = app_ctx.data1;
		// msg->data2 = app_ctx.data2;
		msg->key = app_ctx.key;
		msg->value = app_ctx.value;

		qp->modify_smsg_size(msg->get_size(app_ctx.op));
		qp->append_signal_smsg();
		qp->post_appended_smsg(&sink);

		// puts("??");

		auto reply = (RawKvMsg*)ctx.msg_ptr;

		switch (reply->rpc_header.op) {
		case kReadReply:
			reporter->end_and_end_copy(thread_id, coro_id, 0, 1);
			break;
		case kWriteReply:
			reporter->end_and_end_copy(thread_id, coro_id, 0, 2);
			break;
		default:
			printf("error op= %d\n", reply->rpc_header.op);
			exit(0);
			break;
		}
	}

	int32_t client_get_cid_32(void* reply_addr) {
		auto reply = (RawKvMsg*)reply_addr;
		return reply->rpc_header.src_c_id; // normal req
	}

	int8_t client_get_cid(void* reply_addr) {
		auto reply = (RawKvMsg*)reply_addr;
		return reply->rpc_header.src_c_id; // normal req
	}


	// public:
	SimpleRpc(/* args */) {}
	~SimpleRpc() {}
};

void config() {
	server_config.clear();
	rdma::ServerConfig item;

	global_config.rc_msg_size = RawKvMsg::get_size();
	global_config.ud_msg_size = RawKvMsg::get_size();
	global_config.raw_msg_size = RawKvMsg::get_size();
	global_config.link_type = rdma::kEnumRoCE;
	global_config.func &= (rdma::ConnectType::ALL - rdma::ConnectType::RAW);


	server_config.push_back(rdma::ServerConfig {
		.server_type = 0,
			.thread_num = 1,
			.numa_id = 0,
			.numa_size = 12,
			.dev_id = 0,
			.nic_name = "ens2",
			.port_id = 128,

	});

	server_config.push_back(rdma::ServerConfig { //117
		.server_type = 2,
			.thread_num = 12,
			.numa_id = 1,
			.numa_size = 12,
			.dev_id = 0,
			.nic_name = "ens2",
			.port_id = 188,
	});

	server_config.push_back(rdma::ServerConfig { // 116.2
		.server_type = 2,
			.thread_num = 12,
			.numa_id = 1,
			.numa_size = 12,
			.dev_id = 1,
			.nic_name = "ens6",
			.port_id = 136,
	});

	server_config.push_back(rdma::ServerConfig { // 115 
		.server_type = 2,
			.thread_num = 12,
			.numa_id = 0,
			.numa_size = 12,
			.dev_id = 0,
			.nic_name = "ens2",
			.port_id = 136,
	});

	server_config.push_back(rdma::ServerConfig { // 118
		.server_type = 2,
			.thread_num = 12,
			.numa_id = 1,
			.numa_size = 12,
			.dev_id = 1,
			.nic_name = "ens6",
			.port_id = 136,
	});

	server_config.push_back(rdma::ServerConfig { // 119
		.server_type = 2,
			.thread_num = 12,
			.numa_id = 1,
			.numa_size = 12,
			.dev_id = 1,
			.nic_name = "ens6",
			.port_id = 136,
	});

	for (int i = 0; i < rpc::kConfigServerCnt; i++) {
		for (int j = 0; j < rpc::kConfigServerCnt; j++)
			global_config.matrix[i][j] = rdma::TopoType::All;
	}
	assert(server_config.size() <= rpc::kConfigServerCnt);
}



int main(int argc, char** argv) {

	FLAGS_logtostderr = 1;
	gflags::SetUsageMessage("Usage ./rdma_clean_client --help");
	gflags::ParseCommandLineFlags(&argc, &argv, true);
	google::InitGoogleLogging(argv[0]);

	config();
	Memcached::initMemcached(rpc::FLAGS_node_id);


	int thread_num = server_config[rpc::FLAGS_node_id].thread_num;
	std::thread** th = new std::thread * [thread_num];
	SimpleRpc* worker_list = new SimpleRpc[thread_num];

	for (int i = 0; i < thread_num; i++) {
		worker_list[i].config_client(server_config, &global_config,
			rpc::FLAGS_node_id, i);
	}

	int server_type = server_config[rpc::FLAGS_node_id].server_type;
	perf::PerfConfig perf_config = {
		.thread_cnt = rpc::kConfigMaxThreadCnt,
		.coro_cnt = std::min(rpc::FLAGS_coro_num, kMaxClientReq),
		.type_cnt = 3,
		.slowest_latency = 600,
	};
	perf_config.coro_cnt = std::min(rpc::FLAGS_coro_num, kMaxClientReq);
	perf::PerfTool* reporter = perf::PerfTool::get_instance_ptr(&perf_config);

	

	if (server_type == 0) {
		reporter->new_type("all");
		reporter->new_type("index");
		reporter->master_thread_init();
		key_space = (uint64_t*)malloc(sizeof(uint64_t) * FLAGS_keyspace);
		srand(233);
		for (uint64_t i = 0; i < FLAGS_keyspace; i++) {
			generate_key(i, key_space[i]);
		}
		for (int i = 0; i < thread_num; i++)
			th[i] = new std::thread(
				std::bind(&SimpleRpc::run_server_CPC, &worker_list[i]));
	}
	else if (server_type == 2) {
		reporter->new_type("all");
		reporter->new_type("read");
		reporter->new_type("write");
		reporter->master_thread_init();
		key_space = (uint64_t*)malloc(sizeof(uint64_t) * FLAGS_keyspace);
		srand(233);
		for (uint64_t i = 0; i < FLAGS_keyspace; i++) {
			generate_key(i, key_space[i]);
		}
		for (int i = 0; i < thread_num; i++)
			th[i] = new std::thread(
				std::bind(&SimpleRpc::run_client_CPC, &worker_list[i]));
	}

	

	while (true) {
		reporter->try_wait();
		reporter->try_print(perf_config.type_cnt);
	}

	for (int i = 0; i < thread_num; i++)
		th[i]->join();

	return 0;
}
