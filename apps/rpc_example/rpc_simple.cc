#include "operation.h"
#include "qp_info.h"
#include "rdma.h"
#include "rpc.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include "zipf.h"

DEFINE_int32(c_thread, 1, "client thread");
DEFINE_int32(dn_thread, 1, "dn_thread");
DEFINE_int32(rpc_qp, 0, "0:RC 1:Raw 2:UD 3:TSRQ 4:GSRQ 5:DCT");

enum RpcType {
	kADDReq = 0,
    kADDReply,
	kWrite,
	kRead,
};

struct SimpleCtx {
	enum RpcType op;
    uint32_t data1;
	uint32_t data2;
    uint8_t server_id;
	uint8_t thread_id;
};

rdma::GlobalConfig<rpc::kConfigServerCnt> global_config;
std::vector<rdma::ServerConfig> server_config;
const int kClientId = 1;

struct RawKvMsg {
	rpc::RpcHeader rpc_header;
	uint16_t data1;
	uint16_t data2;
	static uint32_t get_size(int op = -1) {
		switch (op)
		{
		case kADDReq:
			return sizeof(RawKvMsg);
		case kADDReply:
			return sizeof(RawKvMsg);
		default:
			return sizeof(RawKvMsg); // max size
			break;
		}
	}
}__attribute__((packed));


class SimpleRpc: public rpc::Rpc
{
private:
	// server
	struct zipf_gen_state state;
    static const int kCoroWorkerNum = rpc::kConfigCoroCnt;
    SimpleCtx simple_ctx[kCoroWorkerNum];

	
    void server_prepare() {
		reporter = perf::PerfTool::get_instance_ptr();
		reporter->worker_init(thread_id);
	}

    void server_process_msg(void* req_addr, void* reply_addr, rdma::QPInfo* qp) {
		auto req = (RawKvMsg*)req_addr;
		auto reply = (RawKvMsg*)reply_addr;
		reporter->begin(thread_id, 0, 0);
		
        
        switch (req->rpc_header.op)
		{
		case kADDReq:
			reply->data1 = req->data1 + req->data2;
            reply->data2 = req->data2;
            reply->rpc_header.op = kADDReply;
            reporter->end(thread_id, 0, 0);
			#ifdef LOGDEBUG
				LOG(INFO) << "a+b= " << (uint64_t)req->data1 <<"," << (uint64_t)req->data2;
			#endif
			break;
		default:
			printf("error op= %d\n", req->rpc_header.op);
			// exit(0);
			break;
		}
		qp->modify_smsg_size(reply->get_size(reply->rpc_header.op));
    }

	// worker
    void client_prepare() {
		mehcached_zipf_init(&state, 10000, 0.0, thread_id + rand() % 1000);
		reporter = perf::PerfTool::get_instance_ptr();
		reporter->worker_init(thread_id);
		for (int i = 0; i < rpc::FLAGS_coro_num; i++)
			coro_ctx[i].app_ctx = &simple_ctx[i];
	}

    uint8_t get_thread_id_by_load(uint8_t thread_id, uint8_t coro_id) {
        return ((uint32_t)thread_id * rpc::FLAGS_coro_num + coro_id) % FLAGS_dn_thread;
    }

	void cworker_new_request(CoroMaster& sink, uint8_t coro_id) {
		auto& app_ctx = simple_ctx[coro_id];
		uint32_t type = mehcached_zipf_next(&state) + 1;
		type = 0;
		// if (thread_id < 6) return;
		if (type % 2 == 0) {
			app_ctx.op = kADDReq;
			add_test(sink, coro_id);
		}
		else if (type % 2 == 1) {
			app_ctx.op = kWrite;
			rw_test(sink, coro_id);
		}
		else if (false) {
			app_ctx.op = kRead;
		}
	}

	void add_test(CoroMaster& sink, uint8_t coro_id) {
        auto& app_ctx = simple_ctx[coro_id];
		auto& ctx = coro_ctx[coro_id];
        reporter->begin(thread_id, coro_id, 0);

        app_ctx.server_id = 0;
        app_ctx.thread_id = get_thread_id_by_load(thread_id, coro_id) % FLAGS_dn_thread;
        app_ctx.thread_id = thread_id % FLAGS_dn_thread;

		uint8_t qp_id = coro_id % rpc::kConfigMaxQPCnt;

		app_ctx.data1 = mehcached_zipf_next(&state) + 1;
        app_ctx.data2 = mehcached_zipf_next(&state) + 1;
        app_ctx.op = kADDReq;

		// qp
		rdma::QPInfo* qp = rpc_get_qp(
            FLAGS_rpc_qp,
			app_ctx.server_id,
			app_ctx.thread_id,
			qp_id);
		// rdma::QPInfo* qp = &client->get_global_srq_qp(app_ctx.server_id, app_ctx.thread_id, qp_id);
		// rdma::QPInfo* qp = &client->get_rc_qp(app_ctx.server_id, app_ctx.thread_id, qp_id);

		RawKvMsg* msg = (RawKvMsg*)qp->get_send_msg_addr();

		msg->rpc_header = {
			.dst_s_id = app_ctx.server_id,
			.src_s_id = server_id,
			.src_t_id = thread_id,
			.src_c_id = coro_id,
			.local_id = 0,
			.op = (uint8_t)app_ctx.op,
		};

        msg->data1 = app_ctx.data1;
        msg->data2 = app_ctx.data2;

		#ifdef LOGDEBUG
			LOG(INFO) << "a+b: " << (uint64_t)msg->data1<<", "<<(uint64_t)msg->data2;
		#endif

		qp->modify_smsg_size(msg->get_size(app_ctx.op));
		qp->append_signal_smsg();
		qp->post_appended_smsg(&sink, FLAGS_rpc_qp == rdma::LOGDCT);

		auto reply = (RawKvMsg*)ctx.msg_ptr;

        switch (reply->rpc_header.op)
		{
		case kADDReply:
            
            if (reply->data1 != app_ctx.data1 + app_ctx.data2) {
                printf("add error, i want %d+%d=%d, but i get %d (data2=%d)\n", app_ctx.data1, app_ctx.data2,
                    app_ctx.data1 + app_ctx.data2, reply->data1, reply->data2
                );
                exit(0);
            }
			#ifdef LOGDEBUG
				LOG(INFO) << "a+b= " << (uint64_t)reply->data1;
			#endif
            reporter->end(thread_id, coro_id, 0);
			break;
		default:
			printf("error op= %d %u %u\n", reply->rpc_header.op, (uint32_t)reply->data1, (uint32_t)reply->data2);
			exit(0);
			break;
		}
	}

	void rw_test(CoroMaster& sink, uint8_t coro_id) {
		// auto& app_ctx = simple_ctx[coro_id];
		reporter->begin(thread_id, coro_id, 1);

		auto& ctx = coro_ctx[coro_id];
		uint16_t magic = (uint16_t)(mehcached_zipf_next(&state) % 100);

		rdma::GlobalAddress addr[4];
		addr[0].nodeID = 0;
		addr[0].threadID = thread_id % FLAGS_dn_thread; 
		addr[0].offset = agent->get_global_id() * rpc::FLAGS_coro_num * 128 + coro_id * 128;
		addr[1] = addr[0];
		addr[1].offset += 64;

		addr[2].nodeID = 0;
		addr[2].threadID = (thread_id + 1) % FLAGS_dn_thread; 
		addr[2].offset = addr[0].offset;
		addr[3] = addr[2];
		addr[3].offset += 64;

		// addr
		char* my_buffer = buffer + coro_id * KB;
		uint16_t* i32_a = (uint16_t*) my_buffer;
		i32_a[0] = agent->get_global_id();
		i32_a[1] = coro_id + 122;
		i32_a[2] = magic;
		

		// read_buf
		uint16_t* i32_a_r1 = (uint16_t*) (my_buffer + 512);
		uint16_t* i32_a_r2 = (uint16_t*) (my_buffer + 512 + 128);
		// memset(i32_a_r1, 0, 128);
		// memset(i32_a_r2, 0, 128);


		agent->write_append(my_buffer, addr[0], 64, &ctx, false, 0, (FLAGS_rpc_qp == rdma::LOGDCT));
		agent->write(my_buffer, addr[1], 64, &ctx, true, 0, (FLAGS_rpc_qp == rdma::LOGDCT));

		agent->write_append(my_buffer, addr[2], 64, &ctx, false, 0, (FLAGS_rpc_qp == rdma::LOGDCT));
		agent->write_sync(my_buffer, addr[3], 64, &ctx, &sink, 0, (FLAGS_rpc_qp == rdma::LOGDCT));

		#ifdef LOGDEBUG
		LOG(INFO) << "write over";
		#endif

		agent->read((char*)i32_a_r1, addr[0], 128, &ctx, true, 0, (FLAGS_rpc_qp == rdma::LOGDCT));
		agent->read_sync((char*)i32_a_r2, addr[2], 128, &ctx, &sink, 0, (FLAGS_rpc_qp == rdma::LOGDCT));

		// check
		#ifdef LOGDEBUG
		for (int i = 0; i < 3; i++) 
			printf("%d %d %d %d %d\n", i32_a_r1[i], i32_a_r1[i+64/2], i32_a_r2[i], i32_a_r2[i+64/2], i32_a[i]);
		#endif
		
		for (int i = 0; i < 3; i++) {
			if (i32_a_r1[i] != i32_a[i] || i32_a_r2[i] != i32_a[i] || 
				i32_a_r1[i+64/2] != i32_a[i] || i32_a_r2[i+64/2] != i32_a[i]) {
				LOG(ERROR) << "error";
				exit(0);
			}
		}

		reporter->end(thread_id, coro_id, 1);
	}

    int8_t client_get_cid(void* reply_addr) {
		auto reply = (RawKvMsg*)reply_addr;
		return reply->rpc_header.src_c_id; // normal req
	}
	
public:
	SimpleRpc(/* args */) {}
	~SimpleRpc() {}

	void run_server_CPC_srq() {

    init();
    
    server_prepare();
    
    connect();
    
	uint64_t type=0;
    while (true) {
      type++;

	  struct rdma::CQInfo::WcRet* wc_array;
      if (type % 2) {
        if (global_config.func & rdma::ConnectType::G_SRQ)
            wc_array = agent->srq_cq->poll_cq_try(16);
        else 
            continue;
      }
      else {
        wc_array = agent->cq->poll_cq_try(16);
      }

	  #ifdef LOGDEBUG
	  uint16_t* out_buf1 = (uint16_t *) (buffer + 24 * KB + 0 * 128);
	  uint16_t* out_buf2 = (uint16_t *) (buffer + 24 * KB + 1 * 128);
	  uint16_t* out_buf3 = (uint16_t *) (buffer + 24 * KB + 2 * 128);

	  printf("%lx=%d %d %d\n", out_buf1, out_buf1[0], out_buf1[1], out_buf1[2]);
	  printf("%lx=%d %d %d\n", out_buf2, out_buf2[0], out_buf2[1], out_buf2[2]);
	  printf("%lx=%d %d %d\n", out_buf3, out_buf3[0], out_buf3[1], out_buf3[2]);
	  sleep(1);
	  #endif

    //   rdma::QPInfo * qp_array[16];
    //   int cnt = 0;

      ibv_wc *wc = nullptr;
      while ((wc = wc_array->get_next_wc())) {
        auto wc_info = ((rdma::WrInfo *)wc->wr_id);
        auto recv_qp = wc_info->qp;
        void* req;
        rdma::QPInfo* send_qp;


        if (type % 2) {
            req = (void *)wc_info->sg_list[0].addr;
        }
        else {
            req = recv_qp->get_recv_msg_addr();
        }
		// printf("addr=%lx\n",req);

		auto &req_header = *(rpc::RpcHeader *)req;
		
		/*
		send_qp = recv_qp;
		if (recv_qp->type == rdma::LOGGSRQ) 
        	send_qp = &client->get_rc_qp(req_header.src_s_id, req_header.src_t_id);
		if (recv_qp->type == rdma::LOGTSRQ)
			send_qp = rpc_get_qp(rdma::LOGTSRQ, req_header.src_s_id, req_header.src_t_id, 0);
		*/
		
		send_qp = rpc_get_qp(rdma::LOGUD, req_header.src_s_id, req_header.src_t_id, 0);
        
		auto reply = send_qp->get_send_msg_addr();
        auto &reply_header = *(rpc::RpcHeader *)reply;
        reply_header = req_header;
        
		server_process_msg((void *)req, reply, send_qp);

		if (send_qp->type == rdma::LOGRAW) {
			send_qp->modify_smsg_dst_port(agent->get_thread_port(req_header.src_t_id, send_qp->qp_id));
			reply_header.dst_s_id = req_header.src_s_id;
		}	
		// if (send_qp->type == rdma::LOGUD) {
		// 	client->modify_ud_ah(req_header.src_s_id, req_header.src_t_id, send_qp->qp_id);
		// }

        send_qp->append_signal_smsg();
        
        if (type % 2)
            recv_qp->free_recv_msg(wc);
        else 
            recv_qp->free_recv_msg();

        // qp->free_recv_msg();
        // qp_array[cnt++] = send_qp;
      }
	  
		// for (int i = 0; i < cnt; i++) {
		// 	qp_array[i]->post_appended_smsg();
		// }
		agent->get_ud_qp().post_appended_smsg();
    }
  }
};

void config() {
	server_config.clear();
	rdma::ServerConfig item;

	global_config.rc_msg_size = RawKvMsg::get_size();
    global_config.ud_msg_size = RawKvMsg::get_size();
	global_config.raw_msg_size = RawKvMsg::get_size();
	global_config.one_sided_rw_size = 4;
	global_config.link_type = rdma::kEnumRoCE;
    global_config.func = (rdma::ConnectType::RC) | (rdma::ConnectType::G_SRQ) | (rdma::ConnectType::T_LOCAL_SRQ) | (rdma::ConnectType::UD) | (rdma::ConnectType::DCT);
    // global_config.func = (rdma::ConnectType::RC) ;
    if (global_config.link_type == rdma::kEnumIB) {
        global_config.func &= (rdma::ConnectType::ALL) - (rdma::ConnectType::RAW);
    }

	server_config.push_back(item = {
			.server_type = 0,
			.thread_num = FLAGS_dn_thread,
			.numa_id = 0,
			.numa_size = 12,
			.dev_id = 0,
			.nic_name = "ens2", // for raw packet
			.port_id = 128, // for raw packet
			.srq_receiver = 1,
			.srq_sender = 0,
		});

	server_config.push_back(item = {
			.server_type = 1,
			.thread_num = FLAGS_c_thread,
			.numa_id = 0,
			.numa_size = 12,
			.dev_id = 0, 
			.nic_name = "ens6", // for raw packet
			.port_id = 188, // for raw packet
			.srq_receiver = 0,
			.srq_sender = 1,
		});

	server_config.push_back(item = {
			.server_type = 1,
			.thread_num = FLAGS_c_thread,
			.numa_id = 0,
			.numa_size = 12,
			.dev_id = 0, 
			.nic_name = "ens6", // for raw packet
			.port_id = 188, // for raw packet
			.srq_receiver = 0,
			.srq_sender = 1,
		});

	for (int i = 0; i < rpc::kConfigServerCnt; i++) {
		for (int j = 0; j < rpc::kConfigServerCnt; j++) {
			global_config.matrix[i][j] = rdma::TopoType::All;
			global_config.matrix_TSRQ[i][j] = rdma::TopoType::All;
			global_config.matrix_GSRQ[i][j] = rdma::TopoType::All;
		}
	}
    assert(server_config.size() <= rpc::kConfigServerCnt);

}

perf::PerfConfig perf_config = {
	.thread_cnt = rpc::kConfigMaxThreadCnt,
	.coro_cnt = rpc::kConfigCoroCnt,
	.type_cnt = 2,
};

int main(int argc, char** argv) {

	FLAGS_logtostderr = 1;
	gflags::SetUsageMessage("Usage ./rdma_clean_client --help");
	gflags::ParseCommandLineFlags(&argc, &argv, true);
	google::InitGoogleLogging(argv[0]);

	config();
	Memcached::initMemcached(rpc::FLAGS_node_id);

	int thread_num = server_config[rpc::FLAGS_node_id].thread_num;
	std::thread** th = new std::thread* [thread_num];
	SimpleRpc* worker_list = new SimpleRpc[thread_num];

	for (int i = 0; i < thread_num; i++) {
		worker_list[i].config_client(
			server_config,
			&global_config,
			rpc::FLAGS_node_id,
			i);
	}

    int server_type = server_config[rpc::FLAGS_node_id].server_type;
    perf_config.thread_cnt = thread_num;
    perf::PerfTool* reporter = perf::PerfTool::get_instance_ptr(&perf_config);
	

	if (server_type == 0) {
        reporter->new_type("server add");
        reporter->master_thread_init();
		for (int i = 0; i < thread_num; i++)
			th[i] = new std::thread(std::bind(&SimpleRpc::run_server_CPC_srq, &worker_list[i]));
	}
	else if (server_type == 1) {
        reporter->new_type("client add");
        reporter->new_type("client one");

        reporter->master_thread_init();
		for (int i = 0; i < thread_num; i++)
			th[i] = new std::thread(std::bind(&SimpleRpc::run_client_CPC, &worker_list[i]));
	}

	while (true) {
		reporter->try_wait();
		reporter->try_print(perf_config.type_cnt);
	}

	for (int i = 0; i < thread_num; i++)
		th[i]->join();

	return 0;
}

