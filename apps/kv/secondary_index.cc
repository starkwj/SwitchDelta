#include "qp_info.h"
#include "rpc.h"
#include "zipf.h"
#include "sample.h"
#include <cassert>
#include <cmath>
#include "include/index_wrapper.h"
#include "include/test.h"

DEFINE_uint64(keyspace, 100 * MB, "key space");
DEFINE_uint64(logspace, 100 * MB, "key space");
const int average_secondary = 25;

DEFINE_double(zipf, 0.99, "ZIPF");
DEFINE_int32(kv_size_b, 128, "test size B");
DEFINE_bool(visibility, false, "visibility");
DEFINE_uint32(read, 50, "read persentage");
DEFINE_bool(cache_test, false, "cable cache test");
DEFINE_bool(secondary_test, true, "secondary test, 2 replies for sindex read");
DEFINE_bool(read_first, false, "read req has a higher priority, in secondary index node"); 
DEFINE_bool(cable_cache, false, "cable_cache");

DEFINE_int32(c_thread, 1, "client thread");
DEFINE_int32(dn_thread, 1, "dn_thread");
DEFINE_int32(mn_thread, 1, "mn_thread");

constexpr int kMaxCurrentIndexReq = 1024;
// constexpr int kDataThread = 4;
// constexpr int kIndexThread = 4;

constexpr int kKeySize = 8; 
// constexpr int kSecondaryKeySize = 64;
// constexpr uint8_t kSlowWrite = 255;

enum RpcType: uint8_t {
	kEnumReadReq = 0,
	kEnumWriteReq,
	kEnumReadReply,
	kEnumWriteReply,

	kEnumAppendReq,
	kEnumAppendReply,
	kEnumReadLog,

	ReplySwitch = 10,
	kEnumUpdateIndexReq,
	kEnumUpdateIndexReply,

	kEnumReadIndexReq,
	kEnumReadIndexReply,

	kEnumMultiCastPkt = 21,
	kEnumReadSend,

	kEnumWriteAndRead,
	kEnumWriteReplyWithData,

	// kEnumReadIndexReplyHalf,
	kEnumDelSIndex,
};

enum TestType {
	kEnumSRS,
	kEnumSW,
	kEnumSRP,
};
const int kTestTypeNum = 3;
int test_percent[kTestTypeNum] = {50,50,0};
int pre_sum[kTestTypeNum] = {0,0,0};
TestType test_type[100];
// struct KvKeyType {
// 	uint8_t content_[64];
// };


uint32_t get_fingerprint(KvKeyType& key) {
	// return (key & 0xFFFFFFFFu) % (0xFFFFFFFFu - 1) + 1;
	return ((key >> 16) & 0xFFFFFFFFu) % (0xFFFFFFFFu - 1) + 1;
	// return ((key >> 16)) & 1;
	// return ((key >> 16)) & 1;
}
uint16_t get_switch_key(KvKeyType& key) {
	return key & 0xFFFFu;
}
inline void generate_key(uint i, KvKeyType& key) {
	uint64_t tmp = rand();
	// key_space[i] = ((uint64_t)(std::hash<uint>()(i)) << 32) + std::hash<uint>()(i);
	key = (tmp << 32) + rand();
	// key = i + 1;
}
inline void print_key(int i, KvKeyType& key) {
	printf("key%d = %lx\n", i, key);
}

/*
struct KvKeyType {
	uint64_t content_[8];
}__attribute__((packed));
uint32_t get_fingerprint(KvKeyType& key) {
	// return (key & 0xFFFFFFFFu) % (0xFFFFFFFFu - 1) + 1;
	return ((key.content_[0] >> 16) & 0xFFFFFFFFu) % (0xFFFFFFFFu - 1) + 1;
	// return ((key >> 16)) & 1;
	// return ((key >> 16)) & 1;
}
uint16_t get_switch_key(KvKeyType& key) {
	return key.content_[0] & 0xFFFFu;
}
*/

rdma::GlobalConfig<rpc::kConfigServerCnt> global_config;
std::vector<rdma::ServerConfig> server_config;

struct RawKvMsg {

	rpc::RpcHeader rpc_header;
	uint8_t flag;
	uint16_t switch_key;
	uint8_t kv_g_id; // [s:4][t:4]
	uint8_t kv_c_id; // [c:8]
	uint16_t magic; 
	uint16_t server_map;
	uint16_t qp_map; // 2 * 8  bitmap
	uint64_t coro_id; // 8 * id 
	// key value payload
	uint32_t key;
	uint64_t log_id;
	uint16_t route_port;
	uint16_t route_map;
	uint16_t client_port;
	uint16_t send_cnt = 0;
	uint16_t index_port;
	uint16_t magic_in_switch;
	uint32_t timestamp;
	/*for each thread: [xxxx] [xxxx] : [write_coro_id: 0~14] [read_coro_id: 0~14]*/
	uint64_t generate_coro_id(uint8_t c_id_, RpcType op, uint8_t thread_id) {
		// return ((uint64_t)(c_id_ + 1 ) | (op == kEnumReadReq ? 0ull : 0x80ull)) << (thread_id*8ull);
		return ((uint64_t)(c_id_ + 1 )) << (thread_id * 8ull +  (op == kEnumReadReq ? 0 : 4));
	}

	static uint32_t get_size(int op = -1) {
		switch (op)
		{
		case kEnumWriteReq:
			return sizeof(RawKvMsg) + sizeof(KvKeyType) + FLAGS_kv_size_b;
		case kEnumReadReply:
			return sizeof(RawKvMsg) + sizeof(KvKeyType) + FLAGS_kv_size_b;
		

		case kEnumReadLog: return sizeof(RawKvMsg); // read - primary
		// case kEnumReadReply: return sizeof(RawKvMsg) + sizeof(KvKeyType) + FLAGS_kv_size_b;
		
		case kEnumAppendReq: return sizeof(RawKvMsg) + sizeof(KvKeyType) + FLAGS_kv_size_b;
		case kEnumAppendReply: return sizeof(RawKvMsg) + sizeof(KvKeyType);

		case kEnumReadIndexReq: return sizeof(RawKvMsg) + sizeof(KvKeyType);
		case kEnumReadIndexReply: return sizeof(RawKvMsg);

		case kEnumMultiCastPkt: return sizeof(RawKvMsg) + sizeof(KvKeyType);
		case ReplySwitch: return sizeof(RawKvMsg);

		case kEnumUpdateIndexReq: return sizeof(RawKvMsg) + sizeof(KvKeyType);
		case kEnumUpdateIndexReply: return sizeof(RawKvMsg);

		case -1:
			return 2 * KB;
			// return sizeof(RawKvMsg) + sizeof(KvKeyType) + FLAGS_kv_size_b; // max;

		default:
			return sizeof(RawKvMsg); // default size
			break;
		}
	}

	static uint32_t multiple_args_size(int op = -1, uint64_t value_size = kKeySize, uint64_t count = 1) {
		return sizeof(RawKvMsg) + count * value_size;
	}

	void* get_ith_key_addr(int id) {
		return (char *)this + sizeof(RawKvMsg) + kKeySize * id; 
	}


	void* get_value_addr() {
		return (char*)this + sizeof(KvKeyType) + sizeof(RawKvMsg);
	}

	void* get_key_addr() {
		return (char*)this + sizeof(RawKvMsg);
	}

	void* get_log_addr() {
		return (char*)this + sizeof(RawKvMsg);
	}

}__attribute__((packed));

void config() {
	rdma::ServerConfig item;

	global_config.rc_msg_size = 2048;
	global_config.raw_msg_size = RawKvMsg::get_size();
	global_config.link_type = rdma::kEnumRoCE;
	// global_config.func = rdma::ConnectType::RAW;	

	printf("size = %d\n",global_config.raw_msg_size);

	server_config.clear();
	server_config.push_back(item = {
			.server_type = 0,
			.thread_num = FLAGS_dn_thread,
			.numa_id = 0,
			.numa_size = 12,
			.dev_id = 0,
			.nic_name = "ens2",
			.port_id = 128,
			
		});
	

	server_config.push_back(item = {
			.server_type = 1,
			.thread_num = FLAGS_mn_thread,
			.numa_id = 1,
			.numa_size = 12,
			.dev_id = 0,
			.nic_name = "ens2",
			.port_id = 188,
		});

	server_config.push_back(item = {
			.server_type = 2,
			.thread_num = 1,
			.numa_id = 1,
			.numa_size = 12,
			.dev_id = 1,
			.nic_name = "ens6",
			.port_id = 136,
		});

	server_config.push_back(rdma::ServerConfig {
			.server_type = 2,
			.thread_num = FLAGS_c_thread,
			.numa_id = 1,
			.numa_size = 12,
			.dev_id = 0,
			.nic_name = "ens2",
			.port_id = 180,
		});

	server_config.push_back(rdma::ServerConfig {
			.server_type = 2,
			.thread_num = FLAGS_c_thread,
			.numa_id = 1,
			.numa_size = 12,
			.dev_id = 1,
			.nic_name = "ens6",
			.port_id = 172,
		});
		
	assert(server_config.size() <= rpc::kConfigServerCnt);
	for (int i = 0; i < rpc::kConfigServerCnt; i++) {
		for (int j = 0; j < rpc::kConfigServerCnt; j++)
			global_config.matrix[i][j] = rdma::TopoType::All;
	}

	// TODO:
	test_percent[0] = FLAGS_read; 
	test_percent[1] = 100 - FLAGS_read; 
	
	int cnt = 0;
	pre_sum[0] = test_percent[0];
	for (uint i = 0; i < kTestTypeNum - 1; i++) {
		pre_sum[i + 1] = pre_sum[i] + test_percent[i + 1];
	}
	for (int i = 0; i < 100; i++) {
		while (i >= pre_sum[cnt]) {
			cnt++;
		}
		test_type[i] = (TestType)cnt;
		printf("%d-",test_type[i]);
	}
}

perf::PerfConfig perf_config = {
	.thread_cnt = rpc::kConfigMaxThreadCnt,
	.coro_cnt = rpc::kConfigCoroCnt,
	.type_cnt = 7,
};



struct KvCtx {

	enum RpcType op;
	enum TestType test_type;
	uint8_t coro_id;
	uint64_t key;
	uint8_t server_id;
	uint8_t data_s_id;
	uint8_t index_s_id;
	uint8_t thread_id;
	uint16_t magic;
	KvKeyType long_key;
	KvKeyType long_value_read;
	KvKeyType long_value_write;
	uint8_t* value;
	uint64_t log_id; // small key;
	bool index_in_switch;
	bool read_ok;
};

struct bg_write_msg {
	RawKvMsg kv_header;
	uint64_t key[3];
	rdma::QPInfo* qp;
}__attribute__((packed));

const int kMaxWriteQueue = 256;

KvKeyType* key_space;
KvKeyType* secondary_space;
uint64_t secondary_key_space_size;


class FsRpc: public rpc::Rpc
{

	rdma::QPInfo * qp_array[16];
	rdma::QPInfo * rc_qp_array[16];

	bool slow_write_flag;
	int write_cnt;
	bg_write_msg* write_queue;

public:

	void run_server_CPC() {

		init();
		
		server_prepare();
		
		connect();

		if (server_id == 1 && FLAGS_read_first) {
			write_queue = new bg_write_msg[kMaxWriteQueue + 16];
			slow_write_flag = false;
		}

		rdma::QPInfo * default_switch_qp = &agent->get_raw_qp();
		
		int null_poll = 0;
		const int kMaxNullPoll = 10;
		while (true) {
			auto wc_array = agent->cq->poll_cq_try(16);
			
			
			// int cnt = 0;
			int rc_cnt = 0;
			if (wc_array->total_wc_count == 0) 
				null_poll++;
			else 
				null_poll = 0;

			ibv_wc *wc = nullptr;
			while ((wc = wc_array->get_next_wc())) {
				auto qp = ((rdma::WrInfo *)wc->wr_id)->qp;
				// puts("??");

				// bg del
				if (qp->type == 0) {
					rc_qp_array[rc_cnt++] = qp;
					continue;
				}

				auto req = qp->get_recv_msg_addr();
				auto &req_header = *(rpc::RpcHeader *)req;
				// bg_write
				if (FLAGS_read_first && server_id == 1) {
					if (req_header.op == kEnumMultiCastPkt || req_header.op == kEnumUpdateIndexReq) {
						auto& kv_header = *(RawKvMsg*) req;
						auto& tmp = write_queue[write_cnt++];
						
						if (req_header.op == kEnumUpdateIndexReq) 
							slow_write_flag = true;
					
						tmp.kv_header = kv_header; // TODO:
						memcpy(tmp.key, kv_header.get_ith_key_addr(0), sizeof(uint64_t) * 3);
						tmp.qp = qp;
						qp->free_recv_msg();

						continue;
					}

				}

				// puts("xx");
				
				auto reply = qp->get_send_msg_addr();
				auto &reply_header = *(rpc::RpcHeader *)reply;

				reply_header = req_header;

				if (qp->type == 1) {
					qp->modify_smsg_dst_port(
									agent->get_thread_port(req_header.src_t_id, qp->qp_id));
					reply_header.dst_s_id = req_header.src_s_id;
				} else if (qp->type == 2) {
					agent->modify_ud_ah(req_header.src_s_id, req_header.src_t_id,
									qp->qp_id);
				// ah;
				}

				server_process_msg(req, reply, qp);

				qp->append_signal_smsg();
				qp->free_recv_msg();
				qp->post_appended_smsg();

				// if (qp != default_switch_qp)
			}

			// bg write
			if (FLAGS_read_first && server_id == 1) {
				if (slow_write_flag || rc_cnt != 0 || write_cnt >= kMaxWriteQueue || null_poll >= kMaxNullPoll) {
					// TODO: batch processing mathod
					for (int i = 0; i < write_cnt; i++) {
						auto qp = write_queue[i].qp;
						auto reply = qp->get_send_msg_addr();
						// auto &reply_header = *(rpc::RpcHeader *)reply;

						if (qp->type == 1) 
							qp->modify_smsg_dst_port(agent->get_thread_port(write_queue[i].kv_header.rpc_header.src_t_id, qp->qp_id));
						
						server_process_msg((void *)&write_queue[i], reply, qp);
						// reply_header.dst_s_id = (void *)&write_queue[i].src_s_id;

						qp->append_signal_smsg();
						if (qp != default_switch_qp) 
							qp->post_appended_smsg();
					}
					
					default_switch_qp->post_appended_smsg();
					// printf("write_cnt = %d\n", write_cnt);

					// clear
					slow_write_flag = false;
					write_cnt = 0;
					null_poll = 0;
				}
			}

			// background del
			if (server_id == 0) {
				if (del_queue_cnt >= kMaxDelQueue) {
					send_sindex_del();
					del_queue_cnt = 0;
				}
			}
			if (server_id == 1) {
				if (rc_cnt == 0) {
					continue;
				}
				for (int i = 0; i < rc_cnt; i++) {
					auto qp = rc_qp_array[i];
					auto req = qp->get_recv_msg_addr();
					// auto &req_header = *(rpc::RpcHeader *)req;
					process_sindex_del(req);

					qp->free_recv_msg();

				}
			} 
			
		}
	}


private:
	// server
	
	ThreadLocalIndex_masstree* sindex;
    ThreadLocalIndex* pindex;
	perf::Timer timer;
	uint64_t local_max_del_timestamp = 0;
	// ThreadLocalIndex* index;

	uint8_t* userbuf;
	KvCtx kv_ctx[kCoroWorkerNum];
	sampling::StoRandomDistribution<>* sample; /*Fast*/
	struct zipf_gen_state state;
	struct zipf_gen_state state_secondary_read, state_secondary_write;
	struct zipf_gen_state op_state;
	int warmup_insert = 0;
	std::vector<uint64_t> res;
	uint8_t key192[24];
	// uint64_t ;
	const int kMaxDelQueue = 32; 
	int del_queue_cnt = 0;
	uint64_t* del_queue;

	// int x;

	int index_buf_cnt[24] = {0};

	uint64_t get_version() {
		return (( timer.get_time_ns() << 4 ) + thread_id);
	}

	void send_sindex_del() { // server 0
		rdma::QPInfo* qp;
		RawKvMsg* msg;
		uint8_t qp_id = 0;

		qp = rpc_get_qp(0, get_index_server_id(), thread_id % FLAGS_mn_thread, qp_id);
		msg = (RawKvMsg*)qp->get_send_msg_addr();
		msg->rpc_header = {
			.dst_s_id = get_index_server_id(),
			.src_s_id = server_id,
			.src_t_id = thread_id,
			.src_c_id = 0,
			.local_id = 0,
			.op = kEnumDelSIndex,
		};
		msg->flag = 0;
		msg->switch_key = del_queue_cnt; 

		memcpy(msg->get_ith_key_addr(0), del_queue, sizeof(uint64_t) * 3 * del_queue_cnt);

		qp->modify_smsg_size(msg->multiple_args_size(-1, sizeof(KvKeyType) * 3, del_queue_cnt));
		qp->append_signal_smsg();
		qp->post_appended_smsg();

		return;
	}

	void process_sindex_del(void* req_addr) {
		auto req = (RawKvMsg*)req_addr;
		int cnt = req->switch_key; // ugly!

		for (int i = 0; i < cnt; i++) {
			void* addr = req->get_ith_key_addr(i * 3);
			local_max_del_timestamp = std::max(local_max_del_timestamp, *(uint64_t*)req->get_ith_key_addr(i * 3 + 1));
			bool res = sindex->remove(addr, 3 * sizeof(uint64_t));
			// if (i == 0)
				// printf("del(%d),%lx-%lx-%lx\n", res, *(uint64_t*)req->get_ith_key_addr(i * 3), *(uint64_t*)req->get_ith_key_addr(i * 3 + 1), *(uint64_t*)req->get_ith_key_addr(i * 3 + 2));
		}
		// puts("");
	}

	
	
	void server_process_msg(void* req_addr, void* reply_addr, rdma::QPInfo* qp) {
		auto req = (RawKvMsg*)req_addr;
		auto reply = (RawKvMsg*)reply_addr;
		reply->rpc_header = req->rpc_header;
		reply->rpc_header.dst_s_id = req->rpc_header.src_s_id;
		
		reporter->begin(thread_id, 0, 0);

		reply->key = req->key;
		reply->send_cnt = 0;
		// printf("key = %d\n", req->key);
		// sleep(1);
		KvKeyType * primary_key;
		reply->index_port = req->index_port;
		// uint64_t return_value = 0;
		int cnt;

		// printf("op = %d, magic = %d, key = %d\n", req->rpc_header.op, rdma::toBigEndian16(req->magic_in_switch), req->key);

		switch (req->rpc_header.op)
		{
		
		/*cable cache*/
		// req: header, skey, cnt, pkey array
		// reply: header, cnt, pkey array
		case kEnumReadLog: // primary-read  
			// kv.get(logid_2_locallogid(req->log_id), reply->get_log_addr());
			{
				KvKeyType* value_check = (KvKeyType *)req->get_ith_key_addr(0); 
				int cnt = *(uint64_t *)req->get_ith_key_addr(1);
				int ret_cnt = 0;
				ValueAndVersion tmp_value;
				tmp_value.value = 0;
				for (int i = 0; i < cnt; i++) {
					pindex->get_value_version(*(KvKeyType*)req->get_ith_key_addr(i + 2), &tmp_value);
					// printf("r=%lx-%lx ", *(KvKeyType*)req->get_ith_key_addr(i + 2), tmp_value);
					if (tmp_value.value == *value_check) {
						memcpy(reply->get_ith_key_addr(++ret_cnt), req->get_ith_key_addr(i + 2), sizeof(KvKeyType));
					}
				}
				*(uint64_t*) reply->get_ith_key_addr(0) = ret_cnt; 
				reply->rpc_header.op = kEnumReadReply;
				reporter->end_and_end_copy(thread_id, 0, 0, 1);
				qp->modify_smsg_size(reply->multiple_args_size(-1, sizeof(uint64_t), ret_cnt + 2));
				// printf("!= %lx \n", *value_check);
			}
			break;

		//msg: header, pkey, skey
		//reply: header, version
		case kEnumAppendReq: // primary-write  
			// [key | A] >> [A Key B Key]
			
			pindex->get_long(*(KvKeyType*)req->get_ith_key_addr(0), (KvKeyType*)reply->get_ith_key_addr(2)); // B
			{
				ValueAndVersion value, old_value; 
				value.value = *(KvKeyType*)req->get_ith_key_addr(1);
				value.version = get_version();
				bool res = pindex->get_and_put_value_version(*(KvKeyType*)req->get_ith_key_addr(0), &value, &old_value);
				if (res == 0) {
					// printf("%lx -> %lx %lx -> %lx %lx\n",*(KvKeyType*)req->get_ith_key_addr(0), value.value, value.version, old_value.value, old_value.version);
					del_queue[del_queue_cnt * 3] = old_value.value; // skey
					del_queue[del_queue_cnt * 3 + 1] = old_value.version; // version
					del_queue[del_queue_cnt * 3 + 2] = *(KvKeyType*)req->get_ith_key_addr(0); // pkey
					del_queue_cnt++;
				}
				// printf("res = %d, new=%lx,%lx,  old=%lx,%lx\n", res, value.value, value.version, old_value.value, old_value.version);
				*(uint64_t*)reply->get_ith_key_addr(0) = value.version; // version
			}
			// puts("+1");

			// printf("pkey insert %lx %lx\n", *(KvKeyType*)req->get_ith_key_addr(0), *(KvKeyType*)req->get_ith_key_addr(1));
			// memcpy(reply->get_ith_key_addr(0), req->get_ith_key_addr(0), sizeof(KvKeyType));
			
			qp->modify_smsg_size(reply->multiple_args_size(-1, sizeof(uint64_t), 1));
			reply->rpc_header.op = kEnumAppendReply;
			// reply->route_map = kIBitmap | (1<<req->rpc_header.src_s_id);
			reply->route_map = req->route_map;
			reporter->end_and_end_copy(thread_id, 0, 0, 2);

			break;
		
		//req: header, skey
		//reply: header, cnt, [pkey] array
		case kEnumReadIndexReq: // secondary-read
			/*long key*/
			res.clear();
			memset(key192, 0, sizeof(key192));
			memcpy(key192, req->get_ith_key_addr(0), sizeof(uint64_t));
			cnt = sindex->scan(key192, sizeof(uint64_t) * 3, res, 10);
			// printf("skey = %lx ", *(uint64_t *)req->get_ith_key_addr(0));

			for (int i = 0; i < cnt; i++) {
				*(KvKeyType *) reply->get_ith_key_addr(i + 1) = res[i];
				// printf("pkey[%d]= %lx ", i, res[i]);
			}
			// puts("");

			*(KvKeyType *) reply->get_ith_key_addr(0) = cnt;
			qp->modify_smsg_size(reply->multiple_args_size(-1, sizeof(uint64_t), cnt + 1));

			// reply->log_id = (uint32_t) return_value;
			
			reply->rpc_header.op = kEnumReadIndexReply;
			reporter->end_and_end_copy(thread_id, 0, 0, 1);
			break;
		
		//msg: header, skey, version, pkey
		//reply: header
		case kEnumMultiCastPkt: // secondary-write

			// A Key B Key
			/*long key*/
			primary_key = (KvKeyType *)req->get_ith_key_addr(2);
			if ( *(uint64_t *)req->get_ith_key_addr(1) > local_max_del_timestamp )
				sindex->put_vector_key((void *)req->get_ith_key_addr(0), sizeof(kKeySize) * 3, *primary_key); 
			reply->rpc_header.op = ReplySwitch;
			// puts("xx");
			
			qp->modify_smsg_size(reply->multiple_args_size(-1, 0, 0));
			
			reporter->end_and_end_copy(thread_id, 0, 0, 3);
			break;
		case kEnumUpdateIndexReq: // secondary-write
			
			/*long key*/
			primary_key = (KvKeyType *)req->get_ith_key_addr(2);
			if ( *(uint64_t *)req->get_ith_key_addr(1) > local_max_del_timestamp )
				sindex->put_vector_key((void *)req->get_ith_key_addr(0), sizeof(kKeySize) * 3, *primary_key); 
			reply->rpc_header.op = kEnumUpdateIndexReply;
			// puts("yy");
			
			qp->modify_smsg_size(reply->multiple_args_size(-1, 0, 0));
			
			reporter->end_and_end_copy(thread_id, 0, 0, 2);
			break;
		default:
			printf("error op= %d s_id=%d d_id=%d flag=%d key=%d\n", req->rpc_header.op,  req->rpc_header.src_s_id, req->rpc_header.dst_s_id, req->flag, req->key);
			exit(0);
			break;
		}
		reply->magic_in_switch = req->magic_in_switch;

		

		reply->route_port = rdma::toBigEndian16(server_config[req->rpc_header.src_s_id].port_id);
		// qp->modify_smsg_size(reply->get_size(reply->rpc_header.op));
		reply->client_port = req->client_port;

		reply->kv_g_id = req->kv_g_id;
		reply->kv_c_id = req->kv_c_id;
		reply->magic = req->magic;

		reply->switch_key = req->switch_key;
		reply->flag = req->flag;
		reply->qp_map = 0;
		reply->server_map = 0;
		reply->coro_id = req->coro_id;
	}

	void server_prepare() {
		reporter = perf::PerfTool::get_instance_ptr();
		reporter->worker_init(thread_id);

		if (server_id == 0) {
			// kv.init();
			del_queue_cnt = 0;
			del_queue = (uint64_t *) malloc(sizeof(uint64_t) * 3 * kMaxDelQueue * 2);
            pindex = ThreadLocalIndex::get_instance("cuckoo128");
			printf("thread_id=%d init primary index\n", thread_id);
		}
		else {
			sindex = ThreadLocalIndex_masstree::get_instance();
			sindex->thread_init(thread_id);
			printf("thread_id=%d init secondary index\n", thread_id);
		}
	}

	// worker
	void cworker_new_request(CoroMaster& sink, uint8_t coro_id) {
		kv_ctx[coro_id].key = mehcached_zipf_next(&state) + 1;
		kv_ctx[coro_id].long_key = key_space[mehcached_zipf_next(&state)];
		// kv_ctx[coro_id].long_value = kv_ctx[coro_id].long_key >> 48;
		kv_ctx[coro_id].long_value_read = secondary_space[mehcached_zipf_next(&state_secondary_read)];
		kv_ctx[coro_id].long_value_write = secondary_space[mehcached_zipf_next(&state_secondary_write)];
		uint16_t index_tid = get_index_thread_id_by_load(thread_id, coro_id);
		if (index_buf_cnt[index_tid] > kMaxCurrentIndexReq) {
			// printf("ohhhhhh! thread-%d %d find buf cnt=%x %d\n", thread_id, index_tid, index_buf_cnt[index_tid], index_buf_cnt[index_tid]);
			// sleep(1);
			check_req_cnt(sink, coro_id);
			return;
		}
		si_test(sink, coro_id);
	}

	// void read 
	uint8_t get_data_server_id() {
		return 0;
	}
	uint8_t get_index_server_id() {
		return 1;
	}

	uint8_t get_data_thread_id_by_key(uint32_t key) {
		return key % FLAGS_dn_thread;
	}

	uint8_t get_data_thread_id_by_load(uint8_t thread_id, uint8_t coro_id) {
		return ((uint32_t)thread_id * rpc::FLAGS_coro_num + coro_id) % FLAGS_dn_thread;
	}

	uint8_t get_index_thread_id_by_load(uint8_t thread_id, uint8_t coro_id) {
		return ((uint32_t)thread_id * rpc::FLAGS_coro_num + coro_id) % FLAGS_mn_thread;
	}

	uint8_t get_index_thread_id(uint32_t key) {
		return key % FLAGS_mn_thread;
	}

	uint32_t logid_2_threadid(uint32_t log_id) {
		return log_id / FLAGS_keyspace;
	}

	uint32_t logid_2_locallogid(uint32_t log_id) {
		return log_id % FLAGS_keyspace;
	}

	uint32_t compute_log_id(uint32_t local_id, uint32_t thread_id) {
		return thread_id * FLAGS_keyspace + local_id;
	}

	//msg: header, skey
	//reply: header, cnt, [pkey] array
	void get_secondary(CoroMaster & sink, rdma::CoroCtx & c_ctx) {
		rdma::QPInfo* qp;
		RawKvMsg* msg;
		auto& app_ctx = *(KvCtx *)c_ctx.app_ctx;
		uint8_t qp_id = app_ctx.coro_id % rpc::kConfigMaxQPCnt;
		
		// uint8_t index_tid = get_index_thread_id_by_load(thread_id, app_ctx.coro_id);
		uint8_t index_tid = get_index_thread_id(app_ctx.key);

		// KvKeyType* key_addr;
		// puts("get index");
		qp = rpc_get_qp(rpc::FLAGS_qp_type, app_ctx.index_s_id, index_tid, qp_id);
		msg = (RawKvMsg*)qp->get_send_msg_addr();
		msg->rpc_header = {
			.dst_s_id = get_index_server_id(),
			.src_s_id = server_id,
			.src_t_id = thread_id,
			.src_c_id = app_ctx.coro_id,
			.local_id = 0,
			.op = kEnumReadIndexReq,
		};
		msg->key = get_fingerprint(app_ctx.long_value_read); 

		memcpy(msg->get_ith_key_addr(0), &app_ctx.long_value_read, sizeof(KvKeyType));
		msg->flag = FLAGS_visibility ? visibility : 0;
		msg->switch_key = get_switch_key(app_ctx.long_value_read);
		msg->client_port = rdma::toBigEndian16(agent->get_thread_port(thread_id, qp_id));
		msg->route_port = rdma::toBigEndian16(server_config[server_id].port_id);
		msg->index_port = rdma::toBigEndian16(agent->get_thread_port(index_tid, qp_id));
		msg->route_map = kIBitmap | (1 << server_id);

		qp->modify_smsg_size(msg->multiple_args_size(-1, sizeof(KvKeyType), 1));
		qp->append_signal_smsg();
		qp->post_appended_smsg(&sink);

		RawKvMsg* reply = (RawKvMsg*)c_ctx.msg_ptr;
		// if (reply->key != app_ctx.key) {
		// 	if (reply->key == 0xFFFFFFFF) {
		// 		app_ctx.log_id = 0xFFFFFFFF;
		// 		return;
		// 	}
		// 	assert(reply->rpc_header.op == kEnumAppendReply);
		// 	assert(reply->key == app_ctx.key);
		// }
		if (reply->rpc_header.op == kEnumReadIndexReq) {
			app_ctx.index_in_switch = true;	
		}
		if (reply->rpc_header.op == kEnumReadIndexReq) {
			if (FLAGS_secondary_test) {
				sink();
				reply = (RawKvMsg*)c_ctx.msg_ptr;
				return;
			}
		}
		return;
	}

	//msg: header, skey, cnt, pkey array
	//reply: header, cnt, pkey
	void get_primary(CoroMaster& sink, rdma::CoroCtx & c_ctx) { // primary
		rdma::QPInfo* qp;
		RawKvMsg* msg;
		
		RawKvMsg* reply = (RawKvMsg*)c_ctx.msg_ptr;
		uint64_t cnt = *(uint64_t *) reply->get_ith_key_addr(0); 
		auto pkey_addr = (KvKeyType*) reply->get_ith_key_addr(1);
		auto& app_ctx = *(KvCtx *)c_ctx.app_ctx;

		uint8_t qp_id = app_ctx.coro_id % rpc::kConfigMaxQPCnt;
		// uint8_t index_tid = get_index_thread_id_by_load(thread_id, app_ctx.coro_id);
		uint8_t index_tid = get_index_thread_id(app_ctx.key);

		qp = rpc_get_qp(rpc::FLAGS_qp_type, app_ctx.data_s_id, logid_2_threadid(app_ctx.log_id), qp_id);
		msg = (RawKvMsg*)qp->get_send_msg_addr();
		// msg
		msg->rpc_header = {
			.dst_s_id = app_ctx.data_s_id,
			.src_s_id = server_id,
			.src_t_id = thread_id,
			.src_c_id = app_ctx.coro_id,
			.local_id = 0,
			.op = kEnumReadLog,
		};

		msg->kv_g_id = agent->get_global_id();
		msg->kv_c_id = app_ctx.coro_id;

		msg->key = app_ctx.key;
		msg->log_id = app_ctx.log_id;
		// printf("log_id = %d thread_id = %d\n", reply->log_id, logid_2_threadid(reply->log_id));
		msg->flag = FLAGS_visibility ? visibility : 0;
		msg->switch_key = get_switch_key(app_ctx.long_value_read);
		msg->index_port = rdma::toBigEndian16(agent->get_thread_port(index_tid, qp_id));
		// send
		memcpy(msg->get_ith_key_addr(0), &app_ctx.long_value_read, sizeof(KvKeyType));
		memcpy(msg->get_ith_key_addr(1), &cnt, sizeof(uint64_t));
		memcpy(msg->get_ith_key_addr(2), pkey_addr, sizeof(KvKeyType) * cnt);

		qp->modify_smsg_size(msg->multiple_args_size(-1, sizeof(KvKeyType), 2 + cnt));
		qp->append_signal_smsg();
		// printf("req(%d %d) for value=%lx cnt=%lu\n",thread_id, app_ctx.coro_id, app_ctx.long_value_write, cnt);
		qp->post_appended_smsg(&sink);
		
		// memcpy(userbuf, reply->get_value_addr(), FLAGS_kv_size_b);


		reply = (RawKvMsg*)c_ctx.msg_ptr;
		// uint64_t ret_cnt = *(uint64_t *)reply->get_ith_key_addr(0);
		

		assert(reply->rpc_header.op == kEnumReadReply);

		return;
	}

	//msg: header, skey, version, pkey
	//reply: header
	void put_secondary(CoroMaster & sink, rdma::CoroCtx & c_ctx) {
		rdma::QPInfo* qp;
		RawKvMsg* msg;
		RawKvMsg* reply = (RawKvMsg*)c_ctx.msg_ptr;
		auto& app_ctx = *(KvCtx *)c_ctx.app_ctx;
		uint8_t qp_id = app_ctx.coro_id % rpc::kConfigMaxQPCnt;
		// uint8_t index_tid = get_index_thread_id_by_load(thread_id, app_ctx.coro_id);
		uint8_t index_tid = get_index_thread_id(app_ctx.key);

		qp = rpc_get_qp(rpc::FLAGS_qp_type, app_ctx.index_s_id, index_tid, qp_id);
		msg = (RawKvMsg*)qp->get_send_msg_addr();
		// msg
		msg->rpc_header = {
			.dst_s_id = app_ctx.index_s_id,
			.src_s_id = server_id,
			.src_t_id = thread_id,
			.src_c_id = app_ctx.coro_id,
			.local_id = 0,
			.op = kEnumUpdateIndexReq,
		};

		msg->route_map = kIBitmap | (1 << server_id);
		msg->key = app_ctx.key;
		msg->magic = app_ctx.magic;
		msg->log_id = app_ctx.log_id;
		msg->flag = FLAGS_visibility ? visibility : 0;
		msg->switch_key = get_switch_key(app_ctx.long_key);
		msg->index_port = rdma::toBigEndian16(agent->get_thread_port(index_tid, qp_id));
		memcpy(msg->get_ith_key_addr(0), &app_ctx.long_value_write, sizeof(KvKeyType));
		memcpy(msg->get_ith_key_addr(1), reply->get_ith_key_addr(0), sizeof(KvKeyType));
		memcpy(msg->get_ith_key_addr(2), &app_ctx.long_key, sizeof(KvKeyType));
		msg->client_port = rdma::toBigEndian16(agent->get_thread_port(thread_id, qp_id));
		
		qp->modify_smsg_size(msg->multiple_args_size(-1, sizeof(KvKeyType), 3));
		
		qp->append_signal_smsg();
		qp->post_appended_smsg(&sink);
		// puts("write send 2");

		reply = (RawKvMsg*)c_ctx.msg_ptr;
		assert(reply->key == app_ctx.key);
		if (reply->rpc_header.src_t_id != thread_id) {
			printf("thread_id=%d\n", reply->rpc_header.src_t_id);
		}
		assert(reply->rpc_header.src_t_id == thread_id);
		if (FLAGS_visibility && reply->rpc_header.op == kEnumMultiCastPkt) { // check reply
			return;
		}

		assert(reply->rpc_header.op == kEnumUpdateIndexReply);
		// printf("write reply key=%d\n", app_ctx.key);
	}

	
	//msg: header, pkey, skey
	//reply: header, version
	void put_primary(CoroMaster & sink, rdma::CoroCtx & c_ctx) {
		rdma::QPInfo* qp;
		RawKvMsg* msg;
		auto& app_ctx = *(KvCtx *)c_ctx.app_ctx;
		uint8_t qp_id = app_ctx.coro_id % rpc::kConfigMaxQPCnt;
		// uint8_t index_tid = get_index_thread_id_by_load(thread_id, app_ctx.coro_id);
		uint8_t index_tid = get_index_thread_id(app_ctx.key);
		// puts("put value");
		qp = rpc_get_qp(rpc::FLAGS_qp_type, app_ctx.data_s_id, get_data_thread_id_by_load(thread_id, app_ctx.coro_id), qp_id);
			// qp = rpc_get_qp(rpc::FLAGS_qp_type, app_ctx.data_s_id, get_data_thread_id_by_key(app_ctx.key), qp_id);
		msg = (RawKvMsg*)qp->get_send_msg_addr();
		// msg
		msg->rpc_header = {
			.dst_s_id = app_ctx.data_s_id,
			.src_s_id = server_id,
			.src_t_id = thread_id,
			.src_c_id = app_ctx.coro_id,
			.local_id = 0,
			.op = kEnumAppendReq,
		};

		msg->kv_g_id = agent->get_global_id();
		msg->kv_c_id = app_ctx.coro_id;
		
		msg->key = app_ctx.key;
		msg->magic = app_ctx.magic;
		msg->flag = FLAGS_visibility ? visibility : 0;
		msg->switch_key = get_switch_key(app_ctx.long_key);
		msg->client_port = rdma::toBigEndian16(agent->get_thread_port(thread_id, qp_id));
		msg->route_map = kIBitmap | (1 << server_id);
		msg->index_port = rdma::toBigEndian16(agent->get_thread_port(index_tid, qp_id));
		memcpy(msg->get_ith_key_addr(0), &app_ctx.long_key, kKeySize);
		memcpy(msg->get_ith_key_addr(1), &app_ctx.long_value_write, kKeySize);
		// memcpy(msg->get_value_addr(), userbuf, FLAGS_kv_size_b);
		qp->modify_smsg_size(msg->multiple_args_size(-1, kKeySize, 2));
		// printf("send size = %d key=%lx .dst_s_id=%d\n", msg->multiple_args_size(-1, kKeySize, 2), app_ctx.long_key, msg->rpc_header.dst_s_id);
		qp->append_signal_smsg();
		qp->post_appended_smsg(&sink);
		// puts("write send 1");
		RawKvMsg* reply = (RawKvMsg*)c_ctx.msg_ptr;

		if (reply->key != app_ctx.key) {
			printf("op=%d key=%x, appkey=%lx (c_id=%d, coro=%d) (msg->skey=%d, reply->skey=%u) cnt=%d\n",
				reply->rpc_header.op, reply->key, app_ctx.key,
				reply->rpc_header.src_t_id, thread_id, msg->switch_key, reply->switch_key,
				index_buf_cnt[index_tid]);
			
			assert(reply->key == app_ctx.key);
			exit(0);
		}
	}

	void si_test(CoroMaster& sink, uint8_t coro_id) {
		auto& app_ctx = kv_ctx[coro_id];
		auto& ctx = coro_ctx[coro_id];
		
		reporter->begin(thread_id, coro_id, 0);
		
		app_ctx.test_type = test_type[mehcached_zipf_next(&op_state)];
		
		app_ctx.key = get_fingerprint(app_ctx.long_key);
		app_ctx.magic++; // Important for rpc_id !!!
		if (app_ctx.magic == 0) {
			app_ctx.magic++;
		}

		app_ctx.data_s_id = get_data_server_id();
		app_ctx.index_s_id = get_index_server_id();

		if (warmup_insert < kInitInsert) {
			app_ctx.test_type = (TestType)((app_ctx.test_type) | 1); // from read to write
			warmup_insert++;
		}

		app_ctx.index_in_switch = false;
		app_ctx.read_ok = true;
		// bool read_ok = true;
		RawKvMsg* reply;
		
		switch (app_ctx.test_type) {
			case kEnumSW:

				put_primary(sink, ctx);
				reply = (RawKvMsg*)ctx.msg_ptr;
				if (FLAGS_visibility && reply->rpc_header.op == kEnumMultiCastPkt) { // check reply
					// puts("got multicast");
					uint64_t res = reporter->end(thread_id, coro_id, 0);
					reporter->end_copy(thread_id, coro_id, 2, res);
					reporter->end_copy(thread_id, coro_id, 4, res);
					return;
				}
				
				put_secondary(sink, ctx);
				reply = (RawKvMsg*)ctx.msg_ptr;
				if (FLAGS_visibility && reply->rpc_header.op == kEnumMultiCastPkt) { // check reply
					// puts("got multicast");
					uint64_t res = reporter->end(thread_id, app_ctx.coro_id, 0);
					reporter->end_copy(thread_id, app_ctx.coro_id, 2, res);
					return;
				}
				break;
			
			case kEnumSRS:

				get_secondary(sink, ctx);
				
				get_primary(sink, ctx);
				
				break;

		default:
			exit(0);
			break;
		}
		// usleep(100000);

		uint64_t res = reporter->end(thread_id, coro_id, 0);
		if (!app_ctx.read_ok) {
			reporter->end_copy(thread_id, coro_id, 6, res);
			return;
		}

		if (app_ctx.log_id != 0xFFFFFFFF) {
			if (app_ctx.test_type % 2 == 0) {
				reporter->end_copy(thread_id, coro_id, 1, res);
				if (app_ctx.index_in_switch) {
					reporter->end_copy(thread_id, coro_id, 5, res);
				}
			}
			else
				reporter->end_copy(thread_id, coro_id, 2, res);
		}
		else
			reporter->end_copy(thread_id, coro_id, 3, res);
	}

	// cpc
	void client_prepare() {
		mehcached_zipf_init(&state, FLAGS_keyspace - 1, FLAGS_zipf, thread_id + server_id * FLAGS_c_thread + rand() % 1000);
		mehcached_zipf_init(&op_state, 100, 0, thread_id);
		mehcached_zipf_init(&state_secondary_write, (FLAGS_keyspace - 1) / average_secondary, 0, thread_id + rand() % 1000);
		mehcached_zipf_init(&state_secondary_read, (FLAGS_keyspace - 1) / average_secondary, FLAGS_zipf, thread_id + rand() % 1000);
		// sampling::StoRandomDistribution<>::rng_type rng(thread_id);
		// sample = new sampling::StoZipfDistribution<>(rng, 0, FLAGS_keyspace, FLAGS_zipf);

		reporter = perf::PerfTool::get_instance_ptr();
		reporter->worker_init(thread_id);
		userbuf = (uint8_t*)malloc(FLAGS_kv_size_b * 2);

		for (int i = 0; i < rpc::FLAGS_coro_num; i++) {
			coro_ctx[i].app_ctx = &kv_ctx[i];
			kv_ctx[i].coro_id = i;
			kv_ctx[i].value = (uint8_t *)malloc(FLAGS_kv_size_b);
		}
	}

	int8_t client_get_cid(void* reply_addr) {
		auto reply = (RawKvMsg*)reply_addr;
		return reply->rpc_header.src_c_id; // normal req
	}

	void after_process_reply(void* reply, uint8_t c_id) {
		if (FLAGS_visibility) {
			// printf("ret = %x %d %d\n", ((RawKvMsg*)reply)->send_cnt, rdma::toBigEndian16(((RawKvMsg*)reply)->index_port), ((RawKvMsg*)reply)->rpc_header.op);
			// sleep(1);
			index_buf_cnt[(uint16_t)log2(rdma::toBigEndian16(((RawKvMsg*)reply)->index_port))] 
			= (rdma::toBigEndian16(((RawKvMsg*)reply)->send_cnt));
		}
	}

	void check_req_cnt(CoroMaster& sink, uint8_t coro_id) {
		RawKvMsg* msg, * reply;
		rdma::QPInfo* qp = rpc_get_qp(1, 0, 0, 0);
		msg = (RawKvMsg*)qp->get_send_msg_addr();
		qp->modify_smsg_dst_port(agent->get_thread_port(thread_id, 0));
		msg->rpc_header = {
			.dst_s_id = server_id,
			.src_s_id = server_id,
			.src_t_id = thread_id,
			.src_c_id = coro_id,
			.local_id = 0,
			.op = kEnumReadSend,
		};
		msg->send_cnt = 0;
		msg->flag = 0;
		msg->key = 1;
		msg->index_port = rdma::toBigEndian16(agent->get_thread_port(get_index_thread_id_by_load(thread_id, coro_id), 0));
		qp->modify_smsg_size(msg->get_size(kEnumReadSend));
		qp->append_signal_smsg();
		qp->post_appended_smsg(&sink);
		reply = (RawKvMsg*)coro_ctx[coro_id].msg_ptr;
		if (reply->rpc_header.op != kEnumReadSend) {
			exit(-1);
		}
		// printf("cnt=%d update!\n", rdma::toBigEndian16(reply->send_cnt));
	}
	

public:
	FsRpc(/* args */) {}
	~FsRpc() {}
};


int main(int argc, char** argv) {

	FLAGS_logtostderr = 1;
	gflags::SetUsageMessage("Usage ./rdma_clean_client --help");
	gflags::ParseCommandLineFlags(&argc, &argv, true);
	google::InitGoogleLogging(argv[0]);

	LOG(INFO) << "Raw packet size (" << sizeof(RawKvMsg) << ") bytes.";

	config();

	int server_type = server_config[rpc::FLAGS_node_id].server_type;

	perf::PerfTool* reporter = perf::PerfTool::get_instance_ptr(&perf_config);
	if (server_type == 2) {
		if (FLAGS_cache_test) {
			reporter->new_type("all");
			reporter->new_type("cachec");
			reporter->new_type("caches");
			reporter->new_type("hot64K");
			reporter->new_type("read");
			reporter->new_type("write");
		}
		else {
			reporter->new_type("all");
			reporter->new_type("read");
			reporter->new_type("write");
			reporter->new_type("r-n-exist");
			reporter->new_type("fast write");
			reporter->new_type("fast read");
			reporter->new_type("read retry");
		}
	}
	else if (server_type == 0) { // data node
		reporter->new_type("all");
		reporter->new_type("read");
		reporter->new_type("write");
	}
	else if (server_type == 1) { // index node
		ThreadLocalIndex::get_instance();
		reporter->new_type("all");
		reporter->new_type("readi");
		reporter->new_type("writei");
		reporter->new_type("fast index");
	}
	reporter->master_thread_init();

	
	Memcached::initMemcached(rpc::FLAGS_node_id);

	int thread_num = server_config[rpc::FLAGS_node_id].thread_num;
	std::thread** th = new std::thread* [thread_num];
	FsRpc* worker_list = new FsRpc[thread_num];

	for (int i = 0; i < thread_num; i++) {
		worker_list[i].config_client(
			server_config,
			&global_config,
			rpc::FLAGS_node_id,
			i);
	}
	if (server_type == 0 || server_type == 1) {
		for (int i = 0; i < thread_num; i++)
			th[i] = new std::thread(std::bind(&FsRpc::run_server_CPC, &worker_list[i]));
	}
	else {
		key_space = (KvKeyType *)malloc(sizeof(KvKeyType) * FLAGS_keyspace);
		secondary_space = (KvKeyType *)malloc(sizeof(KvKeyType) * FLAGS_keyspace);
		srand(233);
		for (uint i = 0; i < FLAGS_keyspace; i++) {
			generate_key(i, key_space[i]);
			generate_key(i, secondary_space[i]);
			if (i < 10) {
				// printf("key%d = %lx\n", i, key_space[i]);
				print_key(i, key_space[i]);
			}
		}

		for (int i = 0; i < thread_num; i++)
			th[i] = new std::thread(std::bind(&FsRpc::run_client_CPC, &worker_list[i]));
	}

	while (true) {
		reporter->try_wait();
		if (server_type == 2)
		reporter->try_print(perf_config.type_cnt);
	}

	for (int i = 0; i < thread_num; i++)
		th[i]->join();

	return 0;
}