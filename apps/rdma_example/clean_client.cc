#include "config.h"
#include "perfv2.h"
#include "sample.h"
#include "zipf.h"
#include <functional> 

// using coroutine;
DEFINE_int32(node_id, 0, "node id");
DEFINE_int32(qp_type, 0, "RC RAW rpc");
DEFINE_double(zipf, 0.99, "ZIPF");
DEFINE_uint64(keyspace, (100 * MB), "key space");
DEFINE_int32(kv_size_b, 128, "test size B");
DEFINE_uint32(read, 100, "read persentage");
DEFINE_bool(cable_cache, true, "cable_cache");
DEFINE_int32(coro_num, kConfigCoroCnt, "coro nume");
DEFINE_int32(port, 0xFF, "steering test");

DEFINE_int32(mr_size_mb, 1, "mr size mb");
DEFINE_double(test_size_kb, 1, "test size KB");

rdma::GlobalConfig<kConfigServerCnt> global_config;
std::vector<rdma::ServerConfig> server_config;

struct CableCache {
	int64_t key[kConfigCoroCnt];
	bool running[kConfigCoroCnt];
	std::vector<uint8_t> same_coro[kConfigCoroCnt];

	CableCache() {
		memset(key, 0, sizeof(key));
		for (int i = 0; i < FLAGS_coro_num; i++) {
			same_coro[i].clear();
			same_coro[i].resize(FLAGS_coro_num);
		}
		memset(running, false, sizeof(running));
	}

	bool new_request(uint64_t read_key, uint8_t coro_id) {
		for (int i = 0; i < FLAGS_coro_num; i++) {
			if (running[i] == false)
				continue;
			if ((uint64_t)key[i] == read_key) {
				same_coro[i].push_back(coro_id);
				return true;
			}
		}
		running[coro_id] = true;
		key[coro_id] = read_key;
		same_coro[coro_id].clear();
		return false;
	}

	void clear(uint8_t coro_id) {
		running[coro_id] = false;
	}

};

struct SampleKv {
	std::unordered_map<uint64_t, char*> table;
	void* buffer;

	void init() {
		table.clear();
		uint64_t size = ALIGNMENT_XB(FLAGS_kv_size_b, 64) * FLAGS_keyspace;
		std::cout << "key value size = " << perf::PerfTool::tp_to_string(size) << std::endl;
		buffer = malloc(size);
		memset(buffer, 0, size);
		return;

		for (uint64_t i = 0; i < (MB); i++) {
			table[i] = (char*)buffer + i * ALIGNMENT_XB(FLAGS_kv_size_b, 64);
			((char*)table[i])[0] = i % 26 + 'a';
			((char*)table[i])[FLAGS_kv_size_b - 1] = i % 26 + 'A';
		}
	}

	bool get(uint64_t key, void* addr) {
		// if (table.find(key) == table.end()) {
		// 	table[key] = (char*)buffer + key * ALIGNMENT_XB(FLAGS_kv_size_b, 64);
		// }
		// memcpy(addr, table[key], FLAGS_kv_size_b);
		void* addr_s = (char*)buffer + key * ALIGNMENT_XB(FLAGS_kv_size_b, 64);
		memcpy(addr, addr_s, FLAGS_kv_size_b);
		return true;
	}

	bool put(uint64_t key, void* addr) {
		if (table.find(key) == table.end()) {
			LOG(ERROR) << "Sorry, no supports for insert";
			exit(0);
		}
		// std::cout << key << "(put)=" << ((char*)addr)[0] << "..." << ((char*)addr)[FLAGS_kv_size_b - 1] << std::endl;
		memcpy(table[key], addr, FLAGS_kv_size_b);
		return true;
	}
};

enum RpcType {
	kEnumReadReq = 0,
	kEnumWriteReq,
	kEnumReadReply,
	kEnumWriteReply,
};

struct RpcHeader {
	// route header
	uint8_t dst_s_id;

	// rpc header
	uint8_t src_s_id; //1<<3
	uint8_t src_t_id; //1<<4
	uint8_t src_c_id; //1<<4
	uint8_t local_id;
	uint8_t op;
	uint32_t payload_size;

}__attribute__((packed));

struct RawKvMsg {

	RpcHeader rpc_header;
	uint8_t flag;
	uint16_t switch_key;
	uint8_t kv_g_id; // [s:4][t:4]
	uint8_t kv_c_id; // [c:8]
	uint16_t server_map;
	uint16_t qp_map; // 2 * 8  bitmap
	uint64_t coro_id; // 8 * id 
	// key value payload
	uint32_t key;
	uint8_t value1;
	uint8_t value2;
	uint16_t sum;

	static uint32_t get_size(int op = -1) {
		switch (op)
		{
		case kEnumReadReq:
			return sizeof(RawKvMsg);
		case kEnumWriteReq:
			return sizeof(RawKvMsg) + FLAGS_kv_size_b;
		case kEnumReadReply:
			return sizeof(RawKvMsg) + FLAGS_kv_size_b;
		case kEnumWriteReply:
			return sizeof(RawKvMsg);
		default:
			return sizeof(RawKvMsg) + FLAGS_kv_size_b;
			break;
		}
	}

	void* get_value_addr() {
		return (char*)this + sizeof(RawKvMsg);
	}
}__attribute__((packed));

struct KvCtx {

	uint8_t op;
	uint64_t key;
	uint8_t value1;
	uint8_t value2;
	uint8_t server_id;
	uint8_t thread_id;
};


void config() {
	server_config.clear();
	rdma::ServerConfig item;

	global_config.rc_msg_size = RawKvMsg::get_size();
	global_config.raw_msg_size = RawKvMsg::get_size();
	global_config.link_type = rdma::kEnumRoCE;

	server_config.push_back(item = {
			.thread_num = 1,
			.numa_id = 0,
			.numa_size = 12,
			.dev_id = 0,
			.nic_name = "ens2"
		});

	server_config.push_back(item = {
			.thread_num = kConfigMaxThreadCnt,
			.numa_id = 1,
			.numa_size = 12,
			.dev_id = 1,
			.nic_name = "ens6"
		});

	for (int i = 0; i < kConfigServerCnt; i++) {
		for (int j = 0; j < kConfigServerCnt; j++)
			global_config.matrix[i][j] = rdma::TopoType::All;
	}
}

perf::PerfConfig perf_config = {
	thread_cnt: kConfigMaxThreadCnt,
	coro_cnt : kConfigCoroCnt,
	type_cnt : 4,
};


class Worker {
public:
	static const int kCoroWorkerNum = kConfigCoroCnt;
	rdma::Rdma<kConfigServerCnt, kConfigMaxThreadCnt, kConfigMaxQPCnt>* client;
	CoroSlave* coro_slave[kCoroWorkerNum];
	rdma::CoroCtx coro_ctx[kCoroWorkerNum];
	KvCtx kv_ctx[kCoroWorkerNum];
	struct zipf_gen_state state;
	struct zipf_gen_state op_state;
	sampling::StoRandomDistribution<>* sample; /*Fast*/

	CableCache cache;

	perf::PerfTool* reporter;

	uint8_t server_id;
	uint8_t thread_id;

	void init() {
		server_id = client->get_server_id();
		thread_id = client->get_thread_id();
		BindCore(client->server.get_core_id(thread_id));

		uint32_t mm_size = FLAGS_mr_size_mb * MB;
		void* mm_addr = malloc(mm_size);
		client->config_rdma_region(0, (uint64_t)mm_addr, mm_size);
		client->connect();

		uint32_t send_size = client->get_send_buf_size();
		void* send_addr = malloc(send_size);
		client->config_send_region((uint64_t)send_addr, send_size);

		uint32_t recv_size = client->get_recv_buf_size();
		void* recv_addr = malloc(recv_size);
		client->config_recv_region((uint64_t)recv_addr, recv_size);
	}

	void run_server_CPC() {
		
		init();

		SampleKv kv;
		kv.init();

		while (true) {
			auto wc_array = client->cq->poll_cq_try(16);
			ibv_wc* wc = nullptr;
			while ((wc = wc_array->get_next_wc())) {
				auto qp = ((rdma::WrInfo*)wc->wr_id)->qp;
				// wr_info->qp->process_next_msg();
				// printf("qp_id = %d\n", qp->my_port);

				auto req = (RawKvMsg*)qp->get_recv_msg_addr();
				auto reply = (RawKvMsg*)qp->get_send_msg_addr();

				reply->sum = req->value1 + req->value2;
				reply->rpc_header = req->rpc_header;
				reply->rpc_header.dst_s_id = req->rpc_header.src_s_id;
				qp->modify_smsg_dst_port(client->get_thread_port(req->rpc_header.src_t_id, qp->qp_id));
				reply->sum = req->value1 + req->value2;
				reply->key = req->key;

				switch (req->rpc_header.op)
				{
				case kEnumReadReq:
					kv.get(req->key, reply->get_value_addr());
					reply->rpc_header.op = kEnumReadReply;
					qp->modify_smsg_size(reply->get_size(kEnumReadReply));
					break;
				case kEnumWriteReq:
					/* code */
					kv.put(req->key, req->get_value_addr());
					reply->rpc_header.op = kEnumWriteReply;
					qp->modify_smsg_size(reply->get_size(kEnumWriteReply));
					break;
				default:
					break;
				}

				reply->switch_key = req->switch_key;
				reply->flag = req->flag;
				reply->qp_map = 0;
				reply->server_map = 0;
				reply->coro_id = req->coro_id;

				qp->append_signal_smsg();
				qp->post_appended_smsg();
				qp->free_recv_msg();
			}
		}
	}

	void send_test(int server_id, int mode_id);

	void client_worker(CoroMaster& sink, uint8_t coro_id) {

		// printf("coro_id = %d\n", coro_id);
		auto& ctx = coro_ctx[coro_id];
		auto& app_ctx = kv_ctx[coro_id];
		bool cache_tag = false;

		uint8_t qp_id = coro_id % kConfigMaxQPCnt;
		RawKvMsg* msg;
		sink();

		while (true) {
			ctx.free = false;
			reporter->begin(thread_id, coro_id, 0);

			// workloads
			app_ctx.op = (mehcached_zipf_next(&op_state) < FLAGS_read) ? kEnumReadReq : kEnumWriteReq;
			app_ctx.key = mehcached_zipf_next(&state);
			// app_ctx.key = sample->sample();
			app_ctx.server_id = 0;
			app_ctx.thread_id = 0;

			// qp
			rdma::QPInfo* qp;
			if (FLAGS_qp_type == 1) {
				qp = &client->get_raw_qp(qp_id);
			}
			else if (FLAGS_qp_type == 0){
				qp = &client->get_rc_qp(app_ctx.server_id, app_ctx.thread_id, qp_id);
			}

			msg = (RawKvMsg*)qp->get_send_msg_addr();
			qp->modify_smsg_dst_port(client->get_thread_port(app_ctx.thread_id, qp_id)); // remote thread_id



			// src
			msg->rpc_header.src_s_id = server_id;
			msg->rpc_header.src_t_id = thread_id;
			msg->rpc_header.src_c_id = coro_id;
			msg->rpc_header.dst_s_id = app_ctx.server_id;

			// msg
			msg->value1 = app_ctx.value1;
			msg->value2 = app_ctx.value2;
			msg->rpc_header.op = app_ctx.op;
			msg->key = app_ctx.key;

			// switch 
			msg->qp_map = (qp->steering_port);
			msg->switch_key = app_ctx.key & 0xFFFFu;
			msg->flag = (app_ctx.key < 0xFFFFu) && FLAGS_cable_cache;
			msg->coro_id = (((uint64_t)coro_id) << (8 * thread_id));
			msg->kv_g_id = client->get_global_id();
			msg->kv_c_id = coro_id;

			// send
			if (FLAGS_cable_cache && cache.new_request(msg->key, coro_id)) {
				cache_tag = true; // client cache
				sink();
			}
			else {
				cache_tag = false;
				qp->modify_smsg_size(msg->get_size(app_ctx.op));
				qp->append_signal_smsg();
				qp->post_appended_smsg(&sink);
			}

			// recv + check
			auto reply = (RawKvMsg*)ctx.msg_ptr;
			if (reply->key != app_ctx.key) {
				printf("I want %lu, but i get %u\n", app_ctx.key, reply->key);
				exit(0);
			}
			// if (app_ctx.op == kEnumReadReq && thread_id == 0) {
			// 	assert(reply->rpc_header.op == kEnumReadReply);
			// 	void* addr = reply->get_value_addr();
			// 	std::cout << app_ctx.key << "(get)=" << ((char*)addr)[0] << "..." << ((char*)addr)[FLAGS_kv_size_b - 1] << std::endl;
			// }
			uint64_t res = reporter->end(thread_id, coro_id, 0);
			if (cache_tag)
				reporter->end_copy(thread_id, coro_id, 1, res);
			else if (reply->rpc_header.src_s_id != server_id || reply->rpc_header.src_t_id != thread_id)
				reporter->end_copy(thread_id, coro_id, 2, res);
			if (app_ctx.key < 0xFFFFu)
				reporter->end_copy(thread_id, coro_id, 3, res);

			ctx.free = true;
			sink();
		}

	}

	void run_client_CPC() {

		init();
		
		mehcached_zipf_init(&state, FLAGS_keyspace - 1, FLAGS_zipf, thread_id);
		mehcached_zipf_init(&op_state, 100, 0, thread_id);

		reporter = perf::PerfTool::get_instance_ptr();
		reporter->worker_init(thread_id);
		
		// sampling::StoRandomDistribution<>::rng_type rng(thread_id);
		// sample = new sampling::StoZipfDistribution<>(rng, 0, FLAGS_keyspace - 1, FLAGS_zipf);
		
		sleep(2);
		

		for (int i = 0; i < FLAGS_coro_num; i++) {
			using std::placeholders::_1;
			coro_slave[i] = new CoroSlave(std::bind(&Worker::client_worker, this, _1, i));
			coro_ctx[i].app_ctx = &kv_ctx[i];
			coro_ctx[i].async_op_cnt = -1;
		}

		while (true) {

			for (int i = 0; i < FLAGS_coro_num; i++) {
				if (coro_ctx[i].free) {
					(*coro_slave[i])(); // new request
				}
				if (coro_ctx[i].async_op_cnt == 0 && coro_ctx[i].free == false) {
					coro_ctx[i].async_op_cnt = -1;
					(*coro_slave[i])(); // one-sided RDMA
				}
			}

			auto wc_list = client->cq->poll_cq_try(16);
			ibv_wc* wc = nullptr;
			while ((wc = wc_list->get_next_wc())) {

				auto qp = ((rdma::WrInfo*)wc->wr_id)->qp;
				auto reply = (RawKvMsg*)qp->get_recv_msg_addr();

				uint8_t c_id;
				if (reply->rpc_header.src_s_id != server_id || reply->rpc_header.src_t_id != thread_id)
					c_id = (reply->coro_id >> (thread_id * 8)) & 0xFF; // cable cache
				else
					c_id = reply->rpc_header.src_c_id; // normal req
				coro_ctx[c_id].msg_ptr = reply;
				(*coro_slave[c_id])();

				if (FLAGS_cable_cache) {
					for (size_t c = 0; c < cache.same_coro[c_id].size(); c++) {
						auto cache_id = cache.same_coro[c_id][c];
						coro_ctx[cache_id].msg_ptr = reply;
						(*coro_slave[cache_id])(); // client cache
					}
					cache.clear(c_id);
				}

				qp->free_recv_msg();

			}
		}
		return;
	}
};

void send_test(int server_id, int mode_id);
void sample_test();

int main(int argc, char** argv) {

	FLAGS_logtostderr = 1;
	gflags::SetUsageMessage("Usage ./rdma_clean_client --help");
	gflags::ParseCommandLineFlags(&argc, &argv, true);
	google::InitGoogleLogging(argv[0]);

	perf::PerfTool* reporter = perf::PerfTool::get_instance_ptr(&perf_config);
	reporter->new_type("all");
	reporter->new_type("cachec");
	reporter->new_type("caches");
	reporter->new_type("hot64K");
	reporter->master_thread_init();

	config();
	Memcached::initMemcached(FLAGS_node_id);

	int thread_num = server_config[FLAGS_node_id].thread_num;
	std::thread** th = new std::thread * [thread_num];
	Worker* worker_list = new Worker[thread_num];

	for (int i = 0; i < thread_num; i++) {
		worker_list[i].client = new rdma::Rdma<kConfigServerCnt, kConfigMaxThreadCnt, kConfigMaxQPCnt>(
			server_config,
			&global_config,
			FLAGS_node_id,
			i);
	}
	
	if (FLAGS_node_id == 0) {
		for (int i = 0; i < thread_num; i++)
			th[i] = new std::thread(std::bind(&Worker::run_server_CPC, &worker_list[i]));
	}
	else {
		for (int i = 0; i < thread_num; i++)
			th[i] = new std::thread(std::bind(&Worker::run_client_CPC, &worker_list[i]));
		while (true) {
			reporter->try_wait();
			reporter->try_print(perf_config.type_cnt);
		}
	}

	for (int i = 0; i < thread_num; i++)
		th[i]->join();

	return 0;
}


void sample_test() {

	sampling::StoRandomDistribution<>* sample;
	sampling::StoRandomDistribution<>::rng_type rng(0);
	sample = new sampling::StoZipfDistribution<>(rng, 1, FLAGS_keyspace, FLAGS_zipf);
	struct zipf_gen_state state;
	mehcached_zipf_init(&state, FLAGS_keyspace, FLAGS_zipf, 0);
	sleep(1);

	perf::Timer T;

	for (int64_t i = -100; i < (int64_t)MB; i++) {
		if (i == 0)
			T.begin();
		uint64_t x = mehcached_zipf_next(&state);
		(void)x;
	}
	printf("mica = %f Mops\n", MB / (T.end() / 1e3));
	sleep(1);

	for (int64_t i = -100; i < (int64_t)MB; i++) {
		if (i == 0)
			T.begin();
		uint64_t x = sample->sample();
		(void)x;
	}
	printf("sto = %f Mops\n", MB / (T.end() / 1e3));
	sleep(1);

	uint64_t x;
	for (int64_t i = -100; i < (int64_t)MB; i++) {
		if (i == 0)
			T.begin();
		x += 1;
		(void)x;
	}
	printf("add = %f Mops\n", MB / (T.end() / 1e3));
	sleep(1);
}




void Worker::send_test(int server_id, int mode_id) {
	int task_num = 32;
	std::string flag_str[2] = { "rc send", "raw send" };
	perf::Timer timer;

	for (int count = -kWarmUp; count < kTestCount; count++) {
		if (count == 0) {
			timer.begin();
		}
		for (int j = 0; j < task_num; j++) {
			if (mode_id == 0) {
				auto& qp = client->get_rc_qp(server_id, 0, 0);

				auto msg = (RawKvMsg*)qp.get_send_msg_addr();
				msg->rpc_header.dst_s_id = 0;
				msg->key = ((uint64_t)count << 32) | j;

				uint64_t tail_addr = (uint64_t)msg + sizeof(RawKvMsg) + FLAGS_test_size_kb * KB - 8;
				auto tail = (uint64_t*)tail_addr;
				*tail = ((uint64_t)count << 32) | j;

				qp.append_signal_smsg(j == task_num - 1);
			}
			else if (mode_id == 1) {
				auto& qp = client->get_raw_qp(0);
				auto msg = (RawKvMsg*)qp.get_send_msg_addr();

				msg->rpc_header.dst_s_id = 0; // node_id
				qp.modify_smsg_dst_port(0); // thread_id

				msg->key = ((uint64_t)count << 32) | j;
				uint64_t tail_addr = (uint64_t)msg + sizeof(RawKvMsg) + FLAGS_test_size_kb * KB - 8;
				auto tail = (uint64_t*)tail_addr;
				*tail = ((uint64_t)count << 32) | j;

				qp.append_signal_smsg(j == task_num - 1);
			}
		}
		if (mode_id == 0)
			client->get_rc_qp(server_id, 0, 0).post_appended_smsg();
		else if (mode_id == 1) {
			client->get_raw_qp(0).post_appended_smsg();
		}

	}

	uint64_t duration = timer.end();
	LOG(INFO)
		<< flag_str[mode_id]
		<< " iops= "
		<< " bw= " << 1.0 * kTestCount * FLAGS_test_size_kb * (SDIVNS)*task_num / (duration * (GB / KB)) << " GB/s"
		<< " lat= " << duration / (kTestCount * task_num) / (1e3);
}