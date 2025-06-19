
#ifndef _RDMA_H_
#define _RDMA_H_
#include "operation/operation.h"
#include "qp_info.h"
#include "struct/node_info.h"
#include "struct/memory_region.h"
#include "struct/connection.h"
#include "struct/work_request.h"
#include <glog/logging.h>

namespace rdma {

#define LOCALMAXSERVER 8

enum TopoType {
	All = 1,
	LoadBalance,
	AtoB,
	BtoA,
};




class GlobalAddress
{
public:
    union
    {
        struct
        {
            uint64_t nodeID : 8;
			uint64_t threadID : 8;
            uint64_t offset : 48;
        };
        uint64_t val;
    };

    operator uint64_t()
    {
        return val;
    }

    static GlobalAddress Null()
    {
        static GlobalAddress zero{0, 0, 0};
        return zero;
    };
}__attribute__((packed));




template<int kMaxServer = LOCALMAXSERVER>
struct GlobalConfig {
	TopoType matrix[kMaxServer][kMaxServer];
	TopoType matrix_GSRQ[kMaxServer][kMaxServer];
	TopoType matrix_TSRQ[kMaxServer][kMaxServer];
	int func{ ConnectType::RC | ConnectType::RAW | ConnectType::UD};
	uint16_t raw_packet_flag{ 666 };
	
	uint32_t one_sided_rw_size {1};
	uint16_t raw_msg_size;
	uint16_t rc_msg_size;
	uint16_t ud_msg_size; //TODO:

	// bool mp_config.mp_flag{false};
	SrqConfig srq_config;

	LinkType link_type;
};

struct msg_size {
	std::vector<uint16_t> send_msg_size;
	std::vector<uint16_t> recv_msg_size;
	bool check (uint size) {
		return (send_msg_size.size() == size) && (recv_msg_size.size() == size);
	}
};

struct ServerConfig { // nic 
	int server_type;
	int thread_num;
	int numa_id;
	int numa_size;
	int dev_id;
	std::string nic_name;
	uint8_t port_id;
	bool srq_receiver = false;
	bool srq_sender = false;
	int32_t recv_cq_len {-1};
	int32_t send_cq_len {-1};
	uint32_t srq_wr_num;
	int get_core_id(int thread_id) {
		return (numa_id * numa_size + thread_id);
	}
};


template<int kMaxServer, int kMaxThread, int kMaxQPNum = 1, int kMaxMRNum = 1>
class Rdma;

constexpr int buf_region_num = 2;

template<int kMaxServer, int kMaxThread, int kMaxQPNum = 1, int kMaxMRNum = 1, int MyThread = 1>
class RdmaGSRQ {
public:
	// CQInfo* send_cq[kMaxThread];
	CQInfo* recv_cq[MyThread];
	QPInfo* srq;
	RdmaCtx* rdma_ctx = nullptr;
	ServerConfig server;
	GlobalConfig<kMaxServer>* global_config;
	int kServerNum;
	uint8_t server_id;
	ConnectGroup<kMaxServer * kMaxThread, kMaxQPNum, kMaxMRNum> connects[MyThread];
	RegionArr<1> srq_mr;
	const int srq_mr_region_id = 0;
	volatile uint64_t init_flag = 0;
	void *buffer;
	uint8_t thread_id;

	friend class Rdma<kMaxServer, kMaxThread, kMaxQPNum, kMaxMRNum>;
private:
	RdmaGSRQ(){}
	static uint16_t compute_gid(uint8_t s_id, uint8_t t_id) { return (uint16_t)s_id * kMaxThread + t_id; }
	RdmaGSRQ(std::vector<ServerConfig> &server_list, GlobalConfig<kMaxServer> *topology, int server_id_, RdmaCtx* ctx = nullptr, uint8_t thread_id_ = (uint8_t)-1, ibv_cq* cq = nullptr, SrqConfig* srq_config_ = nullptr) {
		
		srq = new QPInfo(srq_config_);
		server = server_list[server_id_];
		global_config = topology;
		server_id = server_id_;
		thread_id = thread_id_;
		
		if (ctx == nullptr) {
			rdma_ctx = new RdmaCtx();
			rdma_ctx->createCtx(server.dev_id, global_config->link_type);
		}
		else {
			rdma_ctx = ctx;
		}
		
		kServerNum = server_list.size();

		for (int i = 0; i < MyThread; i++) {
			connects[i].setDefault(rdma_ctx);
			if (thread_id != (uint8_t)-1)
				connects[i].set_global_id(compute_gid(server_id, thread_id));
			else 
				connects[i].set_global_id(compute_gid(server_id, i));
		}

		init_srq(rdma_ctx, &srq->qpunion.srq, cq, srq_config_);

		init_flag = 0;
		
		// init buffer;
	}

	void init_receive_thread(uint8_t thread_id, CQInfo * cq_info) {
		cq_info->initCq(rdma_ctx);
		recv_cq[thread_id] = cq_info;
	}

	void config_cq(uint8_t thread_id, CQInfo * cq_info) {
		recv_cq[thread_id] = cq_info;
	}

	void gsrq_config_recv_region(int thread_id) {

		if (thread_id == 0) {
			uint32_t total_recv_buf_size = 0;
			uint32_t tmp = 0;
			srq->config_msg_buf(&tmp, &total_recv_buf_size, 0, global_config->rc_msg_size);
			buffer = malloc(total_recv_buf_size);
			srq_mr.initRegion(rdma_ctx, srq_mr_region_id, (uint64_t)buffer, total_recv_buf_size);
			srq->init_srq_with_one_wr(srq_mr.get_region(srq_mr_region_id));
			asm volatile("mfence" : : : "memory");
			init_flag = 1;
		}
		else {
			while (init_flag == 0) {
				asm volatile("pause" : : : "memory");
			}
		}
	}
};

template<int kMaxServer, int kMaxThread, int kMaxQPNum, int kMaxMRNum>
class Rdma
{
private:
	typedef RdmaGSRQ<kMaxServer, kMaxThread, kMaxQPNum, kMaxMRNum, kMaxThread> RdmaGSRQ_t;
	typedef RdmaGSRQ<kMaxServer, kMaxThread, kMaxQPNum, kMaxMRNum, 1> RdmaTSRQ_t;
	
	/* data */
public:
	uint8_t server_id;
	uint8_t thread_id;
	uint16_t global_id;
	ServerConfig server;
	int kServerNum;
	GlobalConfig<kMaxServer>* global_config;

	RdmaGSRQ_t* gsrq;
	RdmaTSRQ_t* tsrq;

	uint32_t total_send_buf_size{ 0 };
	uint32_t total_recv_buf_size{ 0 };
	uint8_t send_mr_id{ 0 };
	uint8_t recv_mr_id{ 1 };

	static uint16_t compute_gid(uint8_t s_id, uint8_t t_id) { return (uint16_t)s_id * kMaxThread + t_id; }
	static uint16_t compute_port(uint8_t t_id, uint8_t qp_id) { return ((uint16_t)t_id * kMaxQPNum + qp_id + 1) << 8; }
	static uint16_t get_thread_port(uint8_t t_id, uint8_t qp_id) {
		if (FLAGS_port_func == false) {
			return compute_port(t_id, qp_id);
		}
		else {
			return 1u << compute_port(t_id, qp_id);
		}
	}
	
	uint8_t get_server_id() { return server_id; }
	uint8_t get_thread_id() { return thread_id; }
	uint8_t get_thread_num() { return server.thread_num; }
	uint16_t get_global_id() { return global_id; }

	RdmaCtx rdma_ctx;
	RegionArr<kMaxMRNum>* local_mr; // read/write rg + send/recv rg
	RegionArr<2>* two_side_mr;
	ConnectGroup<kMaxServer* kMaxThread, kMaxQPNum, kMaxMRNum>* connects;
	ConnectGroup<kMaxServer* kMaxThread, kMaxQPNum, kMaxMRNum>* gsrq_connects; // for send gsrq;
	ConnectGroup<kMaxServer* kMaxThread, kMaxQPNum, kMaxMRNum>* tsrq_connects; // for send tsrq;
	CQInfo* cq;
	CQInfo* send_cq;

	CQInfo* srq_cq;
	// CQInfo srq_cq;

	// CQInfo* wait_cq;

	Rdma(/* args */) {}
	Rdma(std::vector<ServerConfig> &server_list, GlobalConfig<kMaxServer> *topology,
		int server_id_, int thread_id_) {

		local_mr = new RegionArr<kMaxMRNum>(); // read/write rg + send/recv rg
		two_side_mr = new RegionArr<2>();
		
		connects = new ConnectGroup<kMaxServer* kMaxThread, kMaxQPNum, kMaxMRNum>();
		
		

		cq = new CQInfo();
		send_cq = new CQInfo();
		srq_cq = new CQInfo();

		assert(server_list.size() <= kMaxServer);
		assert(thread_id_ < kMaxThread);
		server_id = server_id_;
		thread_id = thread_id_;
		global_id = compute_gid(server_id, thread_id);
		server = server_list[server_id_];
		global_config = topology;

		rdma_ctx.createCtx(server.dev_id, global_config->link_type);

		cq->initCq(&rdma_ctx, server.recv_cq_len, global_config->srq_config.mp_flag);
		cq->recv_cq_only = true;

		send_cq->initCq(&rdma_ctx, server.send_cq_len);
		
		connects->setDefault(&rdma_ctx);
		connects->set_global_id(global_id);
		connects->port_num = kMaxQPNum * server.thread_num; // the num of qps which share the NIC with me.

		if (global_config->func & ConnectType::G_SRQ) {
			gsrq_connects = new ConnectGroup<kMaxServer* kMaxThread, kMaxQPNum, kMaxMRNum>();
			gsrq_connects->setDefault(&rdma_ctx);
			gsrq_connects->set_global_id(global_id);
			gsrq_connects->port_num = kMaxQPNum * server.thread_num; // the num of qps which share the NIC with me.
		}

		if (global_config->func & ConnectType::T_LOCAL_SRQ) {
			tsrq_connects = new ConnectGroup<kMaxServer* kMaxThread, kMaxQPNum, kMaxMRNum>();
			tsrq_connects->setDefault(&rdma_ctx);
			tsrq_connects->set_global_id(global_id);
			tsrq_connects->port_num = kMaxQPNum * server.thread_num; // the num of qps which share the NIC with me.
		}

		

		local_mr->set_node_id(global_id);

		kServerNum = server_list.size();
		// kThreadNum = server.thread_num;

		if (global_config->func & ConnectType::RC) {
			for (int i = 0; i < kServerNum; i++) {

				TopoType type = global_config->matrix[i][server_id];
				int remote_thread_num = server_list[i].thread_num;

				switch (type)
				{
				case TopoType::All:

					if (i == server_id) continue;

					for (int j = 0; j < remote_thread_num; j++) {
						uint16_t remote_gid = compute_gid(i, j);
						connects->remote_node_[remote_gid].valid = true;
						for (int k = 0; k < kMaxQPNum; k++) {
							connects->defaultInitRCQP(remote_gid, k, send_cq, cq);
							connects->get_rc_qp(remote_gid, k)
								.config_msg_buf(
									&total_send_buf_size,
									&total_recv_buf_size,
									global_config->rc_msg_size,
									global_config->rc_msg_size
								);
						}
					}
					break;
				default:
					break;
				}
			}
		}
		
		if (global_config->func & ConnectType::RAW) {
			for (int i = 0; i < kMaxQPNum; i++) {
			// for (int i = kMaxQPNum - 1; i >= 0; i--) {
				uint16_t port_id = get_thread_port(thread_id, i);
				connects->default_init_raw_qp(i, send_cq, cq, server.nic_name,
					port_id, (global_config->raw_packet_flag));
				connects->get_raw_qp(i)
					.config_raw_msg_buf(
						&total_send_buf_size,
						&total_recv_buf_size,
						global_config->raw_msg_size,
						global_config->raw_msg_size
					);
			}
		}

		if (global_config->func & ConnectType::UD) {
			for (int i = 0; i < kMaxQPNum; i++) {
				connects->default_init_ud_qp(i, send_cq, cq);
				connects->get_ud_qp(i)
					.config_ud_msg_buf(
						&total_send_buf_size,
						&total_recv_buf_size,
						global_config->ud_msg_size,
						global_config->ud_msg_size
					);
			}
			for (int i = 0; i < kServerNum; i++) {
				int remote_thread_num = server_list[i].thread_num;
				if (i == server_id) continue;
				for (int j = 0; j < remote_thread_num; j++) {
					uint16_t remote_gid = compute_gid(i, j);
					connects->remote_node_[remote_gid].ud_valid = true;
				}
			}
		}

		if (global_config->func & ConnectType::DCT) {
			for (int i = 0; i < kMaxQPNum; i++) {
				connects->default_init_dct(i, send_cq, cq);
				connects->get_dct_qp(i)
					.config_msg_buf(
						&total_send_buf_size,
						&total_recv_buf_size,
						0,
						global_config->rc_msg_size
					);
				connects->get_dct_qp(i).set_dct_type();
				connects->get_dct_qp(i).set_srq_dct();
				// srq.qpunion.srq
				
			}

			for (int i = 0; i < kServerNum; i++) {
				int remote_thread_num = server_list[i].thread_num;
				if (i == server_id) continue;
				for (int j = 0; j < remote_thread_num; j++) {
					uint16_t remote_gid = compute_gid(i, j);
					connects->remote_node_[remote_gid].dct_valid = true;
					for (int k = 0; k < kMaxQPNum; k++) {
						connects->default_init_dci(remote_gid, k, send_cq, cq);
						connects->get_dci_qp(remote_gid, k)
							.config_msg_buf(
								&total_send_buf_size,
								&total_recv_buf_size,
								global_config->rc_msg_size,
								0
							);
						connects->get_dci_qp(remote_gid, k).set_dct_type();
					}
				}
			}
		}
		



		if (global_config->func & ConnectType::T_LOCAL_SRQ) {

			if (server.srq_receiver) {
				global_config->srq_config.num_of_stride_groups = POSTPIPE;
				get_RdmaTSRQ(server_list, topology, cq->get_cq());
				tsrq->config_cq(0, cq);
				if (!global_config->srq_config.mp_flag)
					tsrq->srq->config_msg_buf( // TODO: multiple srq per threads
						&total_send_buf_size,
						&total_recv_buf_size,
						0,
						global_config->rc_msg_size
					);
				else 
					tsrq->srq->config_mp_msg_buf( // TODO: multiple srq per threads
						&total_recv_buf_size,
						global_config->srq_config
					);
			}

			for (int i = 0; i < kServerNum; i++) {
				TopoType type = global_config->matrix_TSRQ[i][server_id];
				int remote_thread_num = server_list[i].thread_num;
				switch (type)
				{
				case TopoType::All:

					if (i == server_id) continue;

					for (int j = 0; j < remote_thread_num; j++) {
						uint16_t remote_gid = compute_gid(i, j);
						// tsrq->connects[0].remote_node_[remote_gid].valid = true;
						tsrq_connects->remote_node_[remote_gid].valid = true;
						for (int k = 0; k < kMaxQPNum; k++) {
							if (server.srq_receiver)
									tsrq_connects->initRCQP(remote_gid, k, send_cq, cq, &rdma_ctx, tsrq->srq->qpunion.srq,
									global_config->srq_config.mp_flag ? &global_config->srq_config : nullptr);
							else 
								tsrq_connects->initRCQP(remote_gid, k, send_cq, cq, &rdma_ctx, nullptr);
							uint32_t recv_size = global_config->rc_msg_size, send_size = global_config->rc_msg_size;
							if (server.srq_receiver) {
								tsrq_connects->get_rc_qp(remote_gid, k).set_srq(tsrq->srq);
								recv_size = 0;
							}
							tsrq_connects->get_rc_qp(remote_gid, k).config_msg_buf(
								&total_send_buf_size,
								&total_recv_buf_size,
								send_size,
								recv_size
							);
						}
					}
					break;
				default:
					break;
				}
			}
		}

		if (global_config->func & ConnectType::G_SRQ) {
			if (server.srq_receiver) {
				global_config->srq_config.num_of_stride_groups = POSTPIPE;
				gsrq = get_RdmaGSRQ(server_list, topology);
				gsrq->init_receive_thread(thread_id, send_cq);
				for (int i = 0; i < kServerNum; i++) {

					TopoType type = global_config->matrix_GSRQ[i][server_id];
					int remote_thread_num = server_list[i].thread_num;

					switch (type)
					{
					case TopoType::All:

						if (i == server_id) continue;

						for (int j = 0; j < remote_thread_num; j++) {
							uint16_t remote_gid = compute_gid(i, j);
							gsrq->connects[thread_id].remote_node_[remote_gid].valid = true;
							for (int k = 0; k < kMaxQPNum; k++) {
								gsrq->connects[thread_id].initRCQP(remote_gid, k, send_cq, send_cq, gsrq->rdma_ctx, gsrq->srq->qpunion.srq);
								gsrq->connects[thread_id].get_rc_qp(remote_gid, k).set_srq(gsrq->srq);
							}
						}
						break;
					default:
						break;
					}
				}
			}
			if (server.srq_sender) {
				for (int i = 0; i < kServerNum; i++) {

					TopoType type = global_config->matrix_GSRQ[i][server_id];
					int remote_thread_num = server_list[i].thread_num;

					switch (type)
					{
					case TopoType::All:

						if (i == server_id) continue;
						if (!server_list[i].srq_receiver) continue;

						for (int j = 0; j < remote_thread_num; j++) {
							uint16_t remote_gid = compute_gid(i, j);
							gsrq_connects->remote_node_[remote_gid].valid = true;
							for (int k = 0; k < kMaxQPNum; k++) {
								gsrq_connects->initRCQP(remote_gid, k, send_cq, cq, &rdma_ctx);
								gsrq_connects->get_rc_qp(remote_gid, k)
								.config_msg_buf(
									&total_send_buf_size,
									&total_recv_buf_size,
									global_config->rc_msg_size,
									global_config->rc_msg_size
								);
							}
						}
						break;
					default:
						break;
					}
				}
			}
			
			
			// TODO:
		
			// TODO:
		}
		// puts("?");

	}

	RdmaGSRQ_t* get_RdmaGSRQ(std::vector<ServerConfig> &server_list, GlobalConfig<kMaxServer> *topology) {
		static RdmaGSRQ_t GSRQ(server_list, topology, server_id, nullptr);
		return &GSRQ;
	}

	void get_RdmaTSRQ(std::vector<ServerConfig> &server_list, GlobalConfig<kMaxServer> *topology, ibv_cq* cq) {
		tsrq = new RdmaTSRQ_t(server_list, topology, server_id, &rdma_ctx, thread_id, cq
			, &global_config->srq_config);
		return;
	}

	uint32_t get_send_buf_size() {
		return total_send_buf_size;
	}

	uint32_t get_recv_buf_size() {
		return total_recv_buf_size;
	}

	void config_rdma_region(uint8_t mr_id, uint64_t addr, uint32_t size) {
		assert(mr_id < kMaxMRNum);
		local_mr->initRegion(&rdma_ctx, mr_id, addr, size);
	}

	void connect() {

		LOG(INFO) << "Publishing!";
		
		local_mr->publish_mrs();
		connects->publish_node_info();

		LOG(INFO) << "Node Info ing!";
		for (int i = 0; i < kServerNum * kMaxThread; i++) {
			if (connects->remote_node_[i].valid || connects->remote_node_[i].ud_valid )
				connects->subscribe_node_info(i);
		}

		if (global_config->func & ConnectType::RC) { // 1-1 * N
			LOG(INFO) << "RCing!";
			for (int i = 0; i < kServerNum * kMaxThread; i++)
				if (connects->remote_node_[i].valid)
					connects->publish_connect_info(i);
			for (int i = 0; i < kServerNum * kMaxThread; i++) 
				if (connects->remote_node_[i].valid)
					connects->subscribe_nic_rcqp_mr(i);
		}

		// ibv_create_ah
		if (global_config->func & ConnectType::UD) { // 1-N
			LOG(INFO) << "UDing!";
			connects->publish_ud_connect_info();
			for (int i = 0; i < kServerNum * kMaxThread; i++)
				if (connects->remote_node_[i].ud_valid)
					connects->subscribe_ud_connect_info(i);
		}

		if (global_config->func & ConnectType::DCT) { // 1-N
			LOG(INFO) << "DCTing!";
			
			connects->publish_dct_connect_info();

			for (int i = 0; i < kServerNum * kMaxThread; i++)
				if (connects->remote_node_[i].dct_valid)
					connects->subscribe_dct_connect_info(i);
		}

		if (global_config->func & ConnectType::T_LOCAL_SRQ) {
			// publish

			LOG(INFO) << "Thread SRQ RECV ing!";
			for (int i = 0; i < kServerNum * kMaxThread; i++) {
				if (tsrq_connects->remote_node_[i].valid)
					tsrq_connects->subscribe_node_info(i);
			}
			for (int i = 0; i < kServerNum * kMaxThread; i++)
				if (tsrq_connects->remote_node_[i].valid)
					tsrq_connects->publish_connect_info(i, LOGTSRQ);

			for (int i = 0; i < kServerNum * kMaxThread; i++)
				if (tsrq_connects->remote_node_[i].valid) {
					// printf("wait=%d\n",i);
					tsrq_connects->subscribe_connect_info(i, LOGTSRQ);
					tsrq_connects->modify_rcqp_state(i);
				}
		}

		if (global_config->func & ConnectType::G_SRQ) {
			
			// publish
			if (server.srq_receiver) {
				LOG(INFO) << "Global SRQ RECV ing!";
				for (int i = 0; i < kServerNum * kMaxThread; i++) { 
					if (gsrq->connects[thread_id].remote_node_[i].valid)
						gsrq->connects[thread_id].subscribe_node_info(i);
				}
				for (int i = 0; i < kServerNum * kMaxThread; i++)
					if (gsrq->connects[thread_id].remote_node_[i].valid)
						gsrq->connects[thread_id].publish_connect_info(i, LOGGSRQ);
			}
			if (server.srq_sender) {
				LOG(INFO) << "Global SRQ SEND ing!";
				for (int i = 0; i < kServerNum * kMaxThread; i++) { 
					if (gsrq_connects->remote_node_[i].valid)
						gsrq_connects->subscribe_node_info(i);
				}
				for (int i = 0; i < kServerNum * kMaxThread; i++)
					if (gsrq_connects->remote_node_[i].valid)
						gsrq_connects->publish_connect_info(i, LOGGSRQ);
			}

			// subscribe
			if (server.srq_receiver) {
				for (int i = 0; i < kServerNum * kMaxThread; i++)
					if (gsrq->connects[thread_id].remote_node_[i].valid) {
						gsrq->connects[thread_id].subscribe_connect_info(i, LOGGSRQ);
						gsrq->connects[thread_id].modify_rcqp_state(i);
					}
			}
			if (server.srq_sender) {
				for (int i = 0; i < kServerNum * kMaxThread; i++)
					if (gsrq_connects->remote_node_[i].valid) {
						gsrq_connects->subscribe_connect_info(i, LOGGSRQ);
						gsrq_connects->modify_rcqp_state(i);
					}
			}
		}

		connects->publish_barrier_info();
		
		for (int i = 0; i < kServerNum * kMaxThread; i++) {
			if ((connects != nullptr && connects->remote_node_[i].valid) ||
			  	(connects != nullptr && connects->remote_node_[i].ud_valid) ||
			  	(tsrq_connects != nullptr && tsrq_connects->remote_node_ != nullptr && tsrq_connects->remote_node_[i].valid))
				connects->p2p_barrier(i);
		}

		LOG(INFO) << "Connected!";
	}

	// void disconnect() {
	// 	disconnectMemcached(memc);
	// }

	void config_send_region(uint64_t addr, uint32_t size) {
		assert(size >= total_send_buf_size);
		two_side_mr->initRegion(&rdma_ctx, send_mr_id, addr, size);

		if (global_config->func & ConnectType::RC) {
			for (int qp_i = 0; qp_i < kServerNum * kMaxThread; qp_i++)
				if (connects->remote_node_[qp_i].valid)
					for (int i = 0; i < kMaxQPNum; i++) // rc
						connects->remote_node_[qp_i].qp[i].init_send_with_one_mr(two_side_mr->get_region(send_mr_id));
		}

		if (global_config->func & ConnectType::RAW) {
			for (int i = 0; i < kMaxQPNum; i++) // raw packet
				connects->raw_qp[i].init_raw_send_with_one_mr(two_side_mr->get_region(send_mr_id));
		}

		if (global_config->func & ConnectType::UD) {
			for (int i = 0; i < kMaxQPNum; i++) 
				connects->ud_qp_[i].init_send_with_one_mr(two_side_mr->get_region(send_mr_id));
		}

		if (global_config->func & ConnectType::DCT) {
			for (int qp_i = 0; qp_i < kServerNum * kMaxThread; qp_i++)
				if (connects->remote_node_[qp_i].dct_valid)
					for (int i = 0; i < kMaxQPNum; i++) // rc
						connects->remote_node_[qp_i].dci_qp_[i].init_send_with_one_mr(two_side_mr->get_region(send_mr_id));
		}

		if (global_config->func & ConnectType::T_LOCAL_SRQ) {
			for (int qp_i = 0; qp_i < kServerNum * kMaxThread; qp_i++)
			if (tsrq_connects->remote_node_[qp_i].valid)
				for (int i = 0; i < kMaxQPNum; i++) // rc
					tsrq_connects->remote_node_[qp_i].qp[i].init_send_with_one_mr(two_side_mr->get_region(send_mr_id));
		}

		if (global_config->func & ConnectType::G_SRQ) {
			if (server.srq_sender) {
				for (int qp_i = 0; qp_i < kServerNum * kMaxThread; qp_i++)
				if (gsrq_connects->remote_node_[qp_i].valid)
					for (int i = 0; i < kMaxQPNum; i++) // rc
						gsrq_connects->remote_node_[qp_i].qp[i].init_send_with_one_mr(two_side_mr->get_region(send_mr_id));
			}
		}
	}

	void config_recv_region(uint64_t addr, uint32_t size) {
		assert(size >= total_recv_buf_size);
		two_side_mr->initRegion(&rdma_ctx, recv_mr_id, addr, size);

		if (global_config->func & ConnectType::RC) {
			for (int qp_i = 0; qp_i < kServerNum * kMaxThread; qp_i++)
				if (connects->remote_node_[qp_i].valid)
					for (int i = 0; i < kMaxQPNum; i++)
						connects->remote_node_[qp_i].qp[i].init_recv_with_one_mr(two_side_mr->get_region(recv_mr_id));
		}

		if (global_config->func & ConnectType::RAW) {
			for (int i = 0; i < kMaxQPNum; i++)
				connects->raw_qp[i].init_recv_with_one_mr(two_side_mr->get_region(recv_mr_id));
		}

		if (global_config->func & ConnectType::UD) {
			for (int i = 0; i < kMaxQPNum; i++) 
				connects->ud_qp_[i].init_recv_with_one_mr(two_side_mr->get_region(recv_mr_id));
		}

		if (global_config->func & ConnectType::DCT) {
			for (int i = 0; i < kMaxQPNum; i++) 
				connects->dct_qp_[i].init_srq_with_one_wr(two_side_mr->get_region(recv_mr_id));
		}

		if (global_config->func & ConnectType::T_LOCAL_SRQ) {
			if (server.srq_receiver) {
				// tsrq->config_recv_region(0, false, two_side_mr->get_region(recv_mr_id));
				tsrq->srq->init_srq_with_one_wr(two_side_mr->get_region(recv_mr_id));
				tsrq->srq->change_type_for_tsrq();
			}
			else{
				for (int qp_i = 0; qp_i < kServerNum * kMaxThread; qp_i++)
				if (tsrq_connects->remote_node_[qp_i].valid)
					for (int i = 0; i < kMaxQPNum; i++)
						tsrq_connects->remote_node_[qp_i].qp[i].init_recv_with_one_mr(two_side_mr->get_region(recv_mr_id));
			}

		}
		
		if (global_config->func & ConnectType::G_SRQ) {
			if (server.srq_receiver) {
				gsrq->gsrq_config_recv_region(thread_id);
			}
		}
	}

	void read_append(char *buffer, GlobalAddress gaddr, size_t size,
              CoroCtx*ctx, bool signal = false, int qp_id = 0, bool dct = false) {
		auto& qp = get_rc_qp(gaddr.nodeID, gaddr.threadID, qp_id, dct);
		Region* remote_mr = connects->remote_node_[compute_gid(gaddr.nodeID, gaddr.threadID)].remote_mr.get_region(0);
		qp.get_one_wr().clear_flags()
			.set_read_op()
			.set_qp_info(&qp)
			.add_sg_to_wr(local_mr->get_region(0), (uint64_t)buffer - local_mr->get_addr(0), size)
			.count_sg_num()
			.rdma_remote(remote_mr, gaddr.offset);
		if (dct)
			modify_dct_ah(gaddr.nodeID, gaddr.threadID, qp_id, true /*is_one*/);
		qp.append_signal_one(ctx, signal);
	}

	void read(char *buffer, GlobalAddress gaddr, size_t size,
              CoroCtx*ctx, bool signal = false, int qp_id = 0, bool dct = false) {
		read_append(buffer, gaddr, size, ctx, signal, qp_id, dct);
		auto& qp = get_rc_qp(gaddr.nodeID, gaddr.threadID, qp_id, dct);
		qp.post_appended_one(nullptr, dct); 
	}

    void read_sync(char *buffer, GlobalAddress gaddr, size_t size,
                   CoroCtx* ctx, CoroMaster* master = nullptr, int qp_id = 0, bool dct = false) {
		read_append(buffer, gaddr, size, ctx, true, qp_id, dct);
		auto& qp = get_rc_qp(gaddr.nodeID, gaddr.threadID, qp_id, dct);
		if (master != nullptr)
			qp.post_appended_one(master, dct); 
		else {
			// polling;
			LOG(ERROR) << "read_sync must have a master";

		}
	}

    void write_append(char *buffer, GlobalAddress gaddr, size_t size,
              CoroCtx*ctx, bool signal = false, int qp_id = 0, bool dct = false) {
		
		auto& qp = get_rc_qp(gaddr.nodeID, gaddr.threadID, qp_id, dct);
		

		Region* remote_mr = connects->remote_node_[compute_gid(gaddr.nodeID, gaddr.threadID)].remote_mr.get_region(0);
		qp.get_one_wr().clear_flags()
			.set_write_op()
			.set_qp_info(&qp)
			.add_sg_to_wr(local_mr->get_region(0), (uint64_t)buffer - local_mr->get_addr(0), size)
			.count_sg_num()
			.rdma_remote(remote_mr, gaddr.offset);
		
		if (dct)
			modify_dct_ah(gaddr.nodeID, gaddr.threadID, qp_id, true /*is_one*/);
		qp.append_signal_one(ctx, signal);
	}

	void write(char *buffer, GlobalAddress gaddr, size_t size,
              CoroCtx*ctx, bool signal = false, int qp_id = 0, bool dct = false) {
		write_append(buffer, gaddr, size, ctx, signal, qp_id, dct);
		auto& qp = get_rc_qp(gaddr.nodeID, gaddr.threadID, qp_id, dct);
		qp.post_appended_one(nullptr, dct); 
	}

    void write_sync(char *buffer, GlobalAddress gaddr, size_t size,
                   CoroCtx* ctx, CoroMaster* master = nullptr, int qp_id = 0, bool dct = false) {
		write_append(buffer, gaddr, size, ctx, true, qp_id, dct);
		auto& qp = get_rc_qp(gaddr.nodeID, gaddr.threadID, qp_id, dct);
		if (master != nullptr) {
			qp.post_appended_one(master, dct);
		}
		else {
			LOG(ERROR) << "write_sync must have a master";
			// polling;
		}
	}

	QPInfo& get_rc_qp(uint8_t server_id, uint8_t thread_id, uint8_t qp_id = 0, bool dct = false) {
		if (dct == true)
			return connects->remote_node_[compute_gid(server_id, thread_id)].dci_qp_[qp_id];
		return connects->remote_node_[compute_gid(server_id, thread_id)].qp[qp_id];
	}

	QPInfo& get_raw_qp(uint8_t qp_id = 0) {
		return connects->raw_qp[qp_id];
	}

	QPInfo& get_ud_qp(uint8_t qp_id = 0) {
		return connects->ud_qp_[qp_id];
	}



	QPInfo& get_global_srq_qp(uint8_t server_id, uint8_t thread_id, uint8_t qp_id = 0) {
		return gsrq_connects->remote_node_[compute_gid(server_id, thread_id)].qp[qp_id];
	}

	QPInfo& get_thread_srq_qp(uint8_t server_id, uint8_t thread_id, uint8_t qp_id = 0) {
		return tsrq_connects->remote_node_[compute_gid(server_id, thread_id)].qp[qp_id];
	}

	void modify_ud_ah(uint8_t s_id, uint8_t t_id, uint8_t qp_id) {
		uint16_t remote_id = compute_gid(s_id, t_id);
		get_ud_qp(qp_id).get_send_wr().set_remote_ah(
			connects->remote_node_[remote_id].remote_ah.ah, 
			connects->remote_node_[remote_id].remote_ud_qp_num[qp_id]
			);
	}

	void modify_dct_ah(uint8_t s_id, uint8_t t_id, uint8_t qp_id, bool one = false) {
		uint16_t remote_id = compute_gid(s_id, t_id);
		if (one)
			get_rc_qp(s_id, t_id,qp_id, true).get_one_wr().set_remote_dct_ah(
				connects->remote_node_[remote_id].remote_ah.ah, 
				connects->remote_node_[remote_id].remote_dct_qp_num[qp_id]
			);
		else 
			get_rc_qp(s_id, t_id,qp_id, true).get_send_wr().set_remote_dct_ah(
				connects->remote_node_[remote_id].remote_ah.ah, 
				connects->remote_node_[remote_id].remote_dct_qp_num[qp_id]
				);
	}

	~Rdma();
};

}




#endif