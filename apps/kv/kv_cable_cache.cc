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

DEFINE_double(zipf, 0.99, "ZIPF");
DEFINE_int32(kv_size_b, 128, "test size B");
DEFINE_bool(cable_cache, false, "cable_cache");
DEFINE_bool(cache_test, false, "cable cache test");
DEFINE_bool(switch_cache, true, "switch_cache");
DEFINE_bool(visibility, false, "visibility");
DEFINE_uint32(read, 50, "read persentage");

constexpr int kMaxCurrentIndexReq = 1024;
constexpr int kDataThread = 16;
constexpr int kIndexThread = 16;
// constexpr uint8_t kSlowWrite = 255;

enum RpcType {
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
};

enum TestType {
	kEnumRI = 0,
	kEnumWI,
	kEnumRV,
	kEnumWV,
	kEnumR,
	kEnumW,
	kEnumSW,
	kEnumSRP,
	kEnumSRS,
};
const int kTestTypeNum = 6;
const int test_percent[kTestTypeNum] = {0,0,0,0,50,50};
int pre_sum[kTestTypeNum] = {};
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
const int kClientId = 2;

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
		

		case kEnumReadLog: return sizeof(RawKvMsg);
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
			return sizeof(RawKvMsg) + sizeof(KvKeyType) + FLAGS_kv_size_b; // max;
		default:
			return sizeof(RawKvMsg); // default size
			break;
		}
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
	server_config.clear();
	rdma::ServerConfig item;

	global_config.rc_msg_size = RawKvMsg::get_size();
	global_config.raw_msg_size = RawKvMsg::get_size();
	global_config.link_type = rdma::kEnumRoCE;
	// global_config.func = rdma::ConnectType::RAW;

	server_config.push_back(item = {
			.server_type = 0,
			.thread_num = kDataThread,
			.numa_id = 0,
			.numa_size = 12,
			.dev_id = 0,
			.nic_name = "ens2",
			.port_id = 128,
			
		});
	

	server_config.push_back(item = {
			.server_type = 1,
			.thread_num = kIndexThread,
			.numa_id = 1,
			.numa_size = 12,
			.dev_id = 0,
			.nic_name = "ens2",
			.port_id = 188,
		});

	server_config.push_back(item = {
			.server_type = 2,
			.thread_num = 8,
			.numa_id = 1,
			.numa_size = 12,
			.dev_id = 1,
			.nic_name = "ens6",
			.port_id = 136,
		});
	
	assert(server_config.size() <= rpc::kConfigServerCnt);
	for (int i = 0; i < rpc::kConfigServerCnt; i++) {
		for (int j = 0; j < rpc::kConfigServerCnt; j++)
			global_config.matrix[i][j] = rdma::TopoType::All;
	}
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
		printf("%d ",test_type[i]);
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
	uint8_t* value;
	uint64_t log_id; // small key;
	bool index_in_switch;
	bool read_ok;
};

struct CableCache {
	int64_t key[rpc::kConfigCoroCnt];
	int reorder_wait_flag[rpc::kConfigCoroCnt];
	bool running[rpc::kConfigCoroCnt];
	rdma::CoroCtx* ctx_ptr;
	int64_t magic_check[rpc::kConfigCoroCnt];
	std::vector<uint8_t> same_coro[rpc::kConfigCoroCnt];
	int64_t running_write[rpc::kConfigCoroCnt];

	CableCache() {
		memset(key, 0, sizeof(key));
		for (int i = 0; i < rpc::FLAGS_coro_num; i++) {
			same_coro[i].clear();
			same_coro[i].resize(rpc::FLAGS_coro_num);
		}
		memset(running_write, -1, sizeof(running_write));
		memset(running, false, sizeof(running));
		memset(reorder_wait_flag, 0, sizeof(reorder_wait_flag));
	}

	uint8_t check_fisrt(uint8_t coro_id) {
		if (running[coro_id] == true) 
			return coro_id;
		return false;
	}

	bool new_request(uint64_t read_key, uint8_t coro_id, enum RpcType op, uint16_t magic, uint8_t &head_id) {
		
		bool find_running_flag = false;
		int head_coro_id = 0;
		for (int i = 0; i < rpc::FLAGS_coro_num; i++) {
			if (running[i] == false)
				continue;
			if ((uint64_t)key[i] == read_key) {
				same_coro[i].push_back(coro_id);
				head_coro_id = i;
				head_id = i; 
				find_running_flag = true;
				break;
			}
		}

		if (find_running_flag && op == kEnumReadReq) // not the first read-req
			return find_running_flag;
		if (find_running_flag && op == kEnumWriteAndRead && running_write[head_coro_id] != -1) // not the first write-req
			return find_running_flag;
		if (find_running_flag && op == kEnumWriteAndRead && running_write[head_coro_id] == -1) { // 
			running_write[head_coro_id] = coro_id; // the first write, but not the first req. 
			return false;
		}

		running[coro_id] = true;
		key[coro_id] = read_key;
		same_coro[coro_id].clear();	
		head_id = coro_id;

		if (op == kEnumWriteAndRead) // the first req, and a write req. 
			running_write[coro_id] = coro_id;
		else
		 	magic_check[coro_id] = magic; 
 
		return false;
	}

	void print() {
		for (int i = 0; i < rpc::FLAGS_coro_num; i++) {
			printf("i=%d, work=%d, key=%lu, same_coro_size=%lu running_write=%ld running=%d\n",
				i,
				running[i],
				key[i],
				same_coro[i].size(),
				running_write[i],
				running[i]
			);
		}
	}

	uint8_t get_low(uint8_t coro_id) {
		return (coro_id & 0xF) - 1;
	}

	uint8_t get_high(uint8_t coro_id) {
		return ((coro_id & 0xF0) >> 4) - 1;
	}

	void extra_read_reply(uint8_t coro_id) {
		reorder_wait_flag[coro_id] -= 1;
	}

	void free_reorder_wait(uint8_t coro_id) {
		reorder_wait_flag[coro_id] -= 1;
		if (reorder_wait_flag[coro_id] == 0)
			ctx_ptr[coro_id].free = true;
	}

	void reorder_wait(uint8_t coro_id) {
		reorder_wait_flag[coro_id] += 1;
	}


	int16_t check_reply( RawKvMsg* msg, uint16_t thread_id) {
		RpcType op = (RpcType) msg->rpc_header.op;
		// LOG(INFO)<< "check_reply info, op=" << op << " key" << msg_key << "coro_id" << coro_id <<"!"; 
		
		/*
		uint8_t coro_id = ((msg->coro_id >> (thread_id * 8)) & 0xFFu) - 1;
		if (coro_id < 128 && running_write[coro_id] != -1) {
			return -1;
		}
		if (coro_id >= 128 && running_write[coro_id] == -1) {
			return -1;
		}
		if (coro_id >= 128) {
			coro_id -= 128u;
		}

		if (running[coro_id] == true) { //&& magic_check[coro_id] == magic
			return coro_id;
		} 
		*/
		uint8_t coro_id = ((msg->coro_id >> (thread_id * 8)) & 0xFFu);
		// printf("coro_id = %lx thread_id=%d, route=%x, high=%d, low=%d\n", 
		//  msg->coro_id, 
		//  thread_id,
		//  msg->route_map, get_high(coro_id), get_low(coro_id));
		if (coro_id >= 16 && get_high(coro_id) == get_low(coro_id)) {  // read + write == write
			return get_low(coro_id);
		}

		if (coro_id >= 16 && running_write[get_high(coro_id)] == get_high(coro_id)) { // write only == write
			return get_high(coro_id);
		}

		if (coro_id < 16 && reorder_wait_flag[get_low(coro_id)] == 1) { // error read reply
			// if (thread_id == 0) {
			// 	printf("thread_id=%d, coro_id=%lx, free_reorder_wait=%d\n", thread_id, msg->coro_id, get_low(coro_id));
			// 	print();
			// }
			free_reorder_wait(get_low(coro_id));
			return -1;
		}

		if (coro_id < 16 && running_write[get_low(coro_id)] == -1) { // read only
			return get_low(coro_id);
		}
		if (coro_id < 16 && running_write[get_low(coro_id)] != -1) { // read + write == read, drop wait for write
			// if (thread_id == 0) {
			// 	printf("thread_id=%d, coro_id=%lx, extra=%d\n", thread_id, msg->coro_id, get_low(coro_id));
			// 	print();
			// }
			free_reorder_wait(get_low(coro_id));
			return -1;
		}
	
		if (coro_id >= 16) { // find a error read reply
			// if (thread_id == 0) {
			// 	printf("thread_id=%d, coro_id=%lx ,reorder_wait=%d\n", thread_id, msg->coro_id, get_high(coro_id));
			// 	print();
			// }
			reorder_wait(get_high(coro_id));
			return get_high(coro_id);
		}

		print();
		// printf("coro_id = %lx thread_id=%d, route=%x\n", 
		//  msg->coro_id, 
		//  thread_id,
		//  msg->route_map);
		printf("check_reply error, op=%d, key=%u, coro_id=%d\n", op, msg->key, coro_id);
		// sleep(1);
		exit(0); 
		
	}

	void clear(uint8_t coro_id) {
		same_coro[coro_id].clear();	
		running_write[coro_id] = -1;
		running[coro_id] = false;
		magic_check[coro_id] = -1;
	}

};


struct SampleLog {
	void* buffer;
	uint64_t pos;
	const int log_size = FLAGS_kv_size_b + sizeof(KvKeyType);
	void init() {
		uint64_t size = ALIGNMENT_XB(log_size, 64) * FLAGS_logspace;
		std::cout << "key value size = " << perf::PerfTool::tp_to_string(size) << std::endl;
		buffer = malloc(size);
		memset(buffer, 0, size);
		pos = 0;
	}

	bool get(uint64_t key, void* addr) {
		void* addr_s = (char*)buffer + key * ALIGNMENT_XB(log_size, 64);
		memcpy(addr, addr_s, log_size);
		return true;
	}

	bool put(uint64_t key, void* addr) {
		void* addr_s = (char*)buffer + key * ALIGNMENT_XB(log_size, 64);
		memcpy(addr_s, addr, log_size);
		return true; 
	}

	uint64_t append(void* addr) {
		uint64_t cur_pos = pos;
		ADD_ROUND(pos, FLAGS_logspace);
		void* addr_s = (char*)buffer + cur_pos * ALIGNMENT_XB(log_size, 64);
		memcpy(addr_s, addr, log_size);
		return cur_pos;
	}
};

KvKeyType* key_space;

class FsRpc: public rpc::Rpc
{
private:
	// server

	SampleLog kv;
	
	ThreadLocalIndex_masstree* index;
	// ThreadLocalIndex* index;

	uint8_t* userbuf;
	KvCtx kv_ctx[kCoroWorkerNum];
	sampling::StoRandomDistribution<>* sample; /*Fast*/
	struct zipf_gen_state state;
	struct zipf_gen_state op_state;
	int warmup_insert = 0;
	
	// int 

	int index_buf_cnt[kIndexThread] = {0};

	CableCache cache;

	void server_process_msg(void* req_addr, void* reply_addr, rdma::QPInfo* qp) {
		auto req = (RawKvMsg*)req_addr;
		auto reply = (RawKvMsg*)reply_addr;
		reporter->begin(thread_id, 0, 0);

		reply->key = req->key;
		reply->send_cnt = 0;
		// printf("key = %d\n", req->key);
		// sleep(1);
		reply->index_port = req->index_port;
		uint64_t return_value;

		switch (req->rpc_header.op)
		{
		case kEnumReadReq: // cable cache
			// printf("read key=%d\n", req->key);
			kv.get(req->key, reply->get_value_addr());
			reply->rpc_header.op = kEnumReadReply;
			reporter->end_and_end_copy(thread_id, 0, 0, 1);
			break;
		case kEnumWriteReq: // cable cache
			kv.put(req->key, req->get_value_addr());
			reply->rpc_header.op = kEnumWriteReply;
			reporter->end_and_end_copy(thread_id, 0, 0, 2);
			break;
		case kEnumWriteAndRead: // cable cache
			kv.put(req->key, req->get_value_addr());
			reply->rpc_header.op = kEnumWriteReplyWithData;
			memcpy(reply->get_value_addr(), req->get_value_addr(), FLAGS_kv_size_b);
			
			reporter->end_and_end_copy(thread_id, 0, 0, 2);
			break;
		
		/*cable cache*/
		case kEnumReadLog: // visibility
			kv.get(logid_2_locallogid(req->log_id), reply->get_log_addr());
			reply->rpc_header.op = kEnumReadReply;
			reporter->end_and_end_copy(thread_id, 0, 0, 1);
			break;
		case kEnumAppendReq: // visibility
			reply->log_id = compute_log_id(kv.append(req->get_log_addr()), thread_id);
			memcpy(reply->get_key_addr(), req->get_key_addr(), sizeof(KvKeyType));
			reply->rpc_header.op = kEnumAppendReply;
			reply->route_map = req->route_map;
			reporter->end_and_end_copy(thread_id, 0, 0, 2);
			// printf("kEnumAppendReq pos=%d\n", reply->log_id);
			break;
		
		case kEnumReadIndexReq: // visibility
			/*long key*/
			return_value = 0;
			if (!(index->get_long(*(KvKeyType*)req->get_key_addr(), &(return_value)))) {
				reply->key = 0xFFFFFFFF;
			}
			reply->log_id = (uint32_t)return_value;
			
			/*uint32_t key*/
			// if (!(index->get(req->key, &(reply->log_id))))
			// 	reply->key = 0xFFFFFFFF;
			
			reply->rpc_header.op = kEnumReadIndexReply;
			reporter->end_and_end_copy(thread_id, 0, 0, 1);
			break;
		case kEnumMultiCastPkt:
			/*uint32_t key*/
			// index->put(req->key, req->log_id); 

			/*long key*/
			index->put_long(*(KvKeyType*)req->get_key_addr(), req->log_id); 

			reply->rpc_header.op = ReplySwitch;
			// printf("kEnumMultiCastPkt key=%d pos=%d\n", req->key, req->log_id);
			reporter->end_and_end_copy(thread_id, 0, 0, 3);
			break;
		case kEnumUpdateIndexReq:
			/*uint32_t key*/
			//index->put(req->key, req->log_id);
			
			/*long key*/
			index->put_long(*(KvKeyType*)req->get_key_addr(), req->log_id);
			reply->rpc_header.op = kEnumUpdateIndexReply;
			// printf("kEnumUpdateIndexReq key=%d pos=%d\n", req->key, req->log_id);
			reporter->end_and_end_copy(thread_id, 0, 0, 2);
			break;
		default:
			printf("error op= %d s_id=%d d_id=%d flag=%d key=%d\n", req->rpc_header.op,  req->rpc_header.src_s_id, req->rpc_header.dst_s_id, req->flag, req->key);
			exit(0);
			break;
		}
		reply->magic_in_switch = req->magic_in_switch;

		// printf("op = %d, magic = %d, key = %d\n", req->rpc_header.op, rdma::toBigEndian16(req->magic_in_switch), req->key);

		reply->route_port = rdma::toBigEndian16(server_config[kClientId].port_id);
		qp->modify_smsg_size(reply->get_size(reply->rpc_header.op));
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
			kv.init();
			printf("thread_id=%d init kv\n", thread_id);
		}
		else {
			index = ThreadLocalIndex_masstree::get_instance();
			index->thread_init(thread_id);
			// index = ThreadLocalIndex::get_instance();
			// index->thread_init(thread_id);
			printf("thread_id=%d init index\n", thread_id);
		}
	}

	// worker
	void cworker_new_request(CoroMaster& sink, uint8_t coro_id) {

		kv_ctx[coro_id].key = mehcached_zipf_next(&state) + 1;
		kv_ctx[coro_id].long_key = key_space[mehcached_zipf_next(&state)];
		uint16_t index_tid = get_index_thread_id_by_load(thread_id, coro_id);
		if (index_buf_cnt[index_tid] > kMaxCurrentIndexReq) {
			// if (coro_id == 0) {
			// printf("ohhhhhh! thread-%d %d find buf cnt=%x %d\n", thread_id, index_tid, index_buf_cnt[index_tid], index_buf_cnt[index_tid]);
			check_req_cnt(sink, coro_id);
			// }
			return;
		}
		// return cworker_new_request_kv(sink, coro_id);
		// cworker_new_request_index(sink, coro_id);
		kv_test(sink, coro_id);
	}

	// void read 
	uint8_t get_data_server_id() {
		return 0;
	}
	uint8_t get_index_server_id() {
		return 1;
	}

	uint8_t get_data_thread_id_by_key(uint32_t key) {
		return key % kDataThread;
	}

	uint8_t get_data_thread_id_by_load(uint8_t thread_id, uint8_t coro_id) {
		return ((uint32_t)thread_id * rpc::FLAGS_coro_num + coro_id) % kDataThread;
	}

	uint8_t get_index_thread_id_by_load(uint8_t thread_id, uint8_t coro_id) {
		return ((uint32_t)thread_id * rpc::FLAGS_coro_num + coro_id) % kIndexThread;
	}

	uint8_t get_index_thread_id(uint32_t key) {
		return key % kIndexThread;
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

	void send_rpc() {

	}

	void get_index(CoroMaster & sink, rdma::CoroCtx & c_ctx) {
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
		msg->key = get_fingerprint(app_ctx.long_key); 
		memcpy(msg->get_key_addr(), &app_ctx.long_key, sizeof(KvKeyType));
		msg->flag = FLAGS_visibility ? visibility : 0;
		msg->switch_key = get_switch_key(app_ctx.long_key);
		msg->client_port = rdma::toBigEndian16(agent->get_thread_port(thread_id, qp_id));
		msg->route_port = rdma::toBigEndian16(server_config[kClientId].port_id);
		msg->index_port = rdma::toBigEndian16(agent->get_thread_port(index_tid, qp_id));

		qp->modify_smsg_size(msg->get_size(msg->rpc_header.op));
		qp->append_signal_smsg();
		qp->post_appended_smsg(&sink);

		RawKvMsg* reply = (RawKvMsg*)c_ctx.msg_ptr;

		if (reply->key != app_ctx.key) {
			if (reply->key == 0xFFFFFFFF) {
				app_ctx.log_id = 0xFFFFFFFF;
				return;
			}
			assert(reply->rpc_header.op == kEnumAppendReply);
			assert(reply->key == app_ctx.key);
		}
		if (reply->rpc_header.op == kEnumReadIndexReq) {
			app_ctx.index_in_switch = true;
		}
		app_ctx.log_id = reply->log_id;
		return;
	}

	void put_index(CoroMaster & sink, rdma::CoroCtx & c_ctx) {
		rdma::QPInfo* qp;
		RawKvMsg* msg;
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

		// msg->route_map = kI;
		msg->key = app_ctx.key;
		msg->magic = app_ctx.magic;
		msg->log_id = app_ctx.log_id;
		msg->flag = FLAGS_visibility ? visibility : 0;
		msg->switch_key = get_switch_key(app_ctx.long_key);
		msg->index_port = rdma::toBigEndian16(agent->get_thread_port(index_tid, qp_id));
		memcpy(msg->get_key_addr(), &app_ctx.long_key, sizeof(KvKeyType));
		msg->client_port = rdma::toBigEndian16(agent->get_thread_port(thread_id, qp_id));
		qp->modify_smsg_size(msg->get_size(msg->rpc_header.op));
		qp->append_signal_smsg();
		qp->post_appended_smsg(&sink);
		// puts("write send 2");

		RawKvMsg* reply = (RawKvMsg*)c_ctx.msg_ptr;
		assert(reply->key == app_ctx.key);
		if (reply->rpc_header.src_t_id != thread_id) {
			printf("thread_id=%d\n", reply->rpc_header.src_t_id);
		}
		assert(reply->rpc_header.src_t_id == thread_id);
		if (FLAGS_visibility && reply->rpc_header.op == kEnumMultiCastPkt) { // check reply
			// puts("got multicast");
			uint64_t res = reporter->end(thread_id, app_ctx.coro_id, 0);
			reporter->end_copy(thread_id, app_ctx.coro_id, 2, res);
			return;
		}
		assert(reply->rpc_header.op == kEnumUpdateIndexReply);
		// printf("write reply key=%d\n", app_ctx.key);
	}

	void get_value(CoroMaster& sink, rdma::CoroCtx & c_ctx) {
		rdma::QPInfo* qp;
		RawKvMsg* msg;
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
		msg->switch_key = get_switch_key(app_ctx.long_key);
		msg->index_port = rdma::toBigEndian16(agent->get_thread_port(index_tid, qp_id));
		// send
		qp->modify_smsg_size(msg->get_size(msg->rpc_header.op));
		qp->append_signal_smsg();
		qp->post_appended_smsg(&sink);
		RawKvMsg* reply = (RawKvMsg*)c_ctx.msg_ptr;
		memcpy(userbuf, reply->get_value_addr(), FLAGS_kv_size_b);
		assert(reply->rpc_header.op == kEnumReadReply);
		KvKeyType* key_addr = (KvKeyType*)reply->get_key_addr();
		if (reply->key != 0xFFFFFFFF && (*key_addr) != app_ctx.long_key) {
			// printf("reqlkey(%lu) != replylkey(%lu), reqkey(%lu) != replykey(%u) fingerprint(%u) == fingerprint(%u)\n", app_ctx.long_key, *key_addr, 
			// app_ctx.key, reply->key, 
			// get_fingerprint(app_ctx.long_key), get_fingerprint(*key_addr)
			// );
			if (get_fingerprint(app_ctx.long_key) == get_fingerprint(*key_addr))
				app_ctx.read_ok = false;
		}
		return;
	}

	void put_value(CoroMaster & sink, rdma::CoroCtx & c_ctx) {
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
		msg->index_port = rdma::toBigEndian16(agent->get_thread_port(index_tid, qp_id));
		memcpy(msg->get_key_addr(), &app_ctx.long_key, sizeof(KvKeyType));
		memcpy(msg->get_value_addr(), userbuf, FLAGS_kv_size_b);
		qp->modify_smsg_size(msg->get_size(msg->rpc_header.op));
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

	void kv_test(CoroMaster& sink, uint8_t coro_id) {
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
		case kEnumR:
			/*phase 0*/
			get_index(sink, ctx);
			/*phase 1*/
			if (app_ctx.log_id == 0xFFFFFFFF) {
				break;
			}
			get_value(sink, ctx); 
			break;

		case kEnumW:
			/*phase 0*/
			put_value(sink, ctx);
			/*phase 1*/
			reply = (RawKvMsg*)ctx.msg_ptr;
			if (FLAGS_visibility && reply->rpc_header.op == kEnumMultiCastPkt) { // check reply
				// puts("got multicast");
				uint64_t res = reporter->end(thread_id, coro_id, 0);
				reporter->end_copy(thread_id, coro_id, 2, res);
				reporter->end_copy(thread_id, coro_id, 4, res);
				break;
			}
			put_index(sink, ctx);
			break;
		case kEnumRI:
			get_index(sink, ctx);
			break;
		case kEnumWI:
			put_index(sink, ctx);
			break;
		case kEnumRV:
			break;
		case kEnumWV:
			break;
		default:
			exit(0);
			break;
		}
		// usleep(100000);
		reply = (RawKvMsg*)ctx.msg_ptr;

		if (FLAGS_visibility && reply->rpc_header.op == kEnumMultiCastPkt) { // check reply
			// puts("got multicast");
			return;
		}

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

	void cworker_new_request_index(CoroMaster& sink, uint8_t coro_id) {
		auto& app_ctx = kv_ctx[coro_id];
		auto& ctx = coro_ctx[coro_id];

		uint8_t qp_id = coro_id % rpc::kConfigMaxQPCnt;

		// if (thread_id >= 4) {
		// 	while (true);
		// }

		reporter->begin(thread_id, coro_id, 0);

		app_ctx.op = (mehcached_zipf_next(&op_state) < FLAGS_read) ? kEnumReadReq : kEnumWriteReq;
		if (warmup_insert < kInitInsert) {
			app_ctx.op = kEnumWriteReq;
			warmup_insert++;
		}
		
		// app_ctx.long_key = app_ctx.long_key & 0xFFFF;
		app_ctx.key = get_fingerprint(app_ctx.long_key);
		app_ctx.data_s_id = get_data_server_id();
		app_ctx.index_s_id = get_index_server_id();
		app_ctx.magic++; // Important!!!

		// common

		bool index_in_switch = false;
		bool read_ok = true;

		rdma::QPInfo* qp;
		RawKvMsg* msg, * reply;
		// int count_flag = false;
		// puts("???");
		uint8_t index_tid = get_index_thread_id_by_load(thread_id, coro_id);
		// uint8_t index_tid = get_index_thread_id(app_ctx.key);
		KvKeyType* key_addr;
		switch (app_ctx.op) {
		case kEnumReadReq:
			/*phase 0*/
			qp = rpc_get_qp(rpc::FLAGS_qp_type, app_ctx.index_s_id, index_tid, qp_id);

			msg = (RawKvMsg*)qp->get_send_msg_addr();
			// msg
			msg->rpc_header = {
				.dst_s_id = app_ctx.index_s_id,
				.src_s_id = server_id,
				.src_t_id = thread_id,
				.src_c_id = coro_id,
				.local_id = 0,
				.op = kEnumReadIndexReq,
			};
			msg->key = app_ctx.key; 
			memcpy(msg->get_key_addr(), &app_ctx.long_key, sizeof(KvKeyType));
			msg->flag = FLAGS_visibility ? visibility : 0;
			msg->switch_key = get_switch_key(app_ctx.long_key);
			// fast path to reply client, 
			msg->route_port = rdma::toBigEndian16(server_config[kClientId].port_id);
			msg->client_port = rdma::toBigEndian16(agent->get_thread_port(thread_id, qp_id));
			msg->index_port = rdma::toBigEndian16(agent->get_thread_port(index_tid, qp_id));

			// send
			qp->modify_smsg_size(msg->get_size(msg->rpc_header.op));
			qp->append_signal_smsg();
			qp->post_appended_smsg(&sink);
			reply = (RawKvMsg*)ctx.msg_ptr;

			if (reply->key != app_ctx.key) {
				if (reply->key == 0xFFFFFFFF) {
					break;
				}
				assert(reply->rpc_header.op == kEnumAppendReply);
				assert(reply->key == app_ctx.key);
			}
			if (reply->rpc_header.op == kEnumReadIndexReq) {
				index_in_switch = true;
			}

			/*phase 1*/
			qp = rpc_get_qp(rpc::FLAGS_qp_type, app_ctx.data_s_id, logid_2_threadid(reply->log_id), qp_id);
			msg = (RawKvMsg*)qp->get_send_msg_addr();
			// msg
			msg->rpc_header = {
				.dst_s_id = app_ctx.data_s_id,
				.src_s_id = server_id,
				.src_t_id = thread_id,
				.src_c_id = coro_id,
				.local_id = 0,
				.op = kEnumReadLog,
			};

			msg->kv_g_id = agent->get_global_id();
			msg->kv_c_id = coro_id;

			msg->key = app_ctx.key;
			msg->log_id = reply->log_id;
			// printf("log_id = %d thread_id = %d\n", reply->log_id, logid_2_threadid(reply->log_id));
			msg->flag = FLAGS_visibility ? visibility : 0;
			msg->switch_key = get_switch_key(app_ctx.long_key);
			msg->index_port = rdma::toBigEndian16(agent->get_thread_port(index_tid, qp_id));
			// send
			qp->modify_smsg_size(msg->get_size(app_ctx.op));
			qp->append_signal_smsg();
			qp->post_appended_smsg(&sink);
			reply = (RawKvMsg*)ctx.msg_ptr;
			memcpy(userbuf, reply->get_value_addr(), FLAGS_kv_size_b);
			assert(reply->rpc_header.op == kEnumReadReply);
			key_addr = (KvKeyType*)reply->get_key_addr();
			if (reply->key != 0xFFFFFFFF && (*key_addr) != app_ctx.long_key) {
				// printf("reqlkey(%lu) != replylkey(%lu), reqkey(%lu) != replykey(%u) fingerprint(%u) == fingerprint(%u)\n", app_ctx.long_key, *key_addr, 
				// app_ctx.key, reply->key, 
				// get_fingerprint(app_ctx.long_key), get_fingerprint(*key_addr)
				// );
				read_ok = false;
			}
			break;

		case kEnumWriteReq:
			/*phase 0*/
			qp = rpc_get_qp(rpc::FLAGS_qp_type, app_ctx.data_s_id, get_data_thread_id_by_load(thread_id, coro_id), qp_id);
			// qp = rpc_get_qp(rpc::FLAGS_qp_type, app_ctx.data_s_id, get_data_thread_id_by_key(app_ctx.key), qp_id);
			msg = (RawKvMsg*)qp->get_send_msg_addr();
			// msg
			msg->rpc_header = {
				.dst_s_id = app_ctx.data_s_id,
				.src_s_id = server_id,
				.src_t_id = thread_id,
				.src_c_id = coro_id,
				.local_id = 0,
				.op = kEnumAppendReq,
			};

			msg->kv_g_id = agent->get_global_id();
			msg->kv_c_id = coro_id;

			msg->key = app_ctx.key;
			msg->magic = app_ctx.magic;
			msg->flag = FLAGS_visibility ? visibility : 0;
			msg->switch_key = get_switch_key(app_ctx.long_key);
			msg->client_port = rdma::toBigEndian16(agent->get_thread_port(thread_id, qp_id));
			msg->index_port = rdma::toBigEndian16(agent->get_thread_port(index_tid, qp_id));
			memcpy(msg->get_key_addr(), &app_ctx.long_key, sizeof(KvKeyType));
			memcpy(msg->get_value_addr(), userbuf, FLAGS_kv_size_b);
			qp->modify_smsg_size(msg->get_size(app_ctx.op));
			qp->append_signal_smsg();
			qp->post_appended_smsg(&sink);
			// puts("write send 1");
			reply = (RawKvMsg*)ctx.msg_ptr;

			if (reply->key != app_ctx.key) {
				printf("op=%d key=%x, appkey=%lx (c_id=%d, coro=%d) (msg->skey=%d, reply->skey=%u) cnt=%d\n",
					reply->rpc_header.op, reply->key, app_ctx.key,
					reply->rpc_header.src_t_id, thread_id, msg->switch_key, reply->switch_key,
					index_buf_cnt[index_tid]);
				
				assert(reply->key == app_ctx.key);
				exit(0);
			}

			if (FLAGS_visibility && reply->rpc_header.op == kEnumMultiCastPkt) { // check reply
				// puts("got multicast");
				uint64_t res = reporter->end(thread_id, coro_id, 0);
				reporter->end_copy(thread_id, coro_id, 2, res);
				reporter->end_copy(thread_id, coro_id, 4, res);
				return;
			}

			if (reply->rpc_header.op != kEnumAppendReply) {
				printf("op = %d\n", reply->rpc_header.op);
				assert(reply->rpc_header.op == kEnumAppendReply);
			}
			assert(reply->rpc_header.src_t_id == thread_id);

			/*phase 1*/
			qp = rpc_get_qp(rpc::FLAGS_qp_type, app_ctx.index_s_id, index_tid, qp_id);
			msg = (RawKvMsg*)qp->get_send_msg_addr();
			// msg
			msg->rpc_header = {
				.dst_s_id = app_ctx.index_s_id,
				.src_s_id = server_id,
				.src_t_id = thread_id,
				.src_c_id = coro_id,
				.local_id = 0,
				.op = kEnumUpdateIndexReq,
			};
			// msg->route_port = rdma::toBigEndian16(app_ctx.data_s_id);
			// msg->route_map = kIndexBitmap;
			msg->key = app_ctx.key;
			msg->magic = app_ctx.magic;
			msg->log_id = reply->log_id;
			msg->flag = FLAGS_visibility ? visibility : 0;
			msg->switch_key = get_switch_key(app_ctx.long_key);
			msg->index_port = rdma::toBigEndian16(agent->get_thread_port(index_tid, qp_id));
			memcpy(msg->get_key_addr(), &app_ctx.long_key, sizeof(KvKeyType));
			msg->client_port = rdma::toBigEndian16(agent->get_thread_port(thread_id, qp_id));
			qp->modify_smsg_size(msg->get_size(app_ctx.op));
			qp->append_signal_smsg();
			qp->post_appended_smsg(&sink);
			// puts("write send 2");

			reply = (RawKvMsg*)ctx.msg_ptr;
			assert(reply->key == app_ctx.key);
			if (reply->rpc_header.src_t_id != thread_id) {
				printf("thread_id=%d\n", reply->rpc_header.src_t_id);
			}
			assert(reply->rpc_header.src_t_id == thread_id);
			if (FLAGS_visibility && reply->rpc_header.op == kEnumMultiCastPkt) { // check reply
				// puts("got multicast");
				uint64_t res = reporter->end(thread_id, coro_id, 0);
				reporter->end_copy(thread_id, coro_id, 2, res);
				return;
			}
			assert(reply->rpc_header.op == kEnumUpdateIndexReply);
			// printf("write reply key=%d\n", app_ctx.key);

			break;
		default:
			exit(0);
			break;
		}
		// usleep(100000);

		uint64_t res = reporter->end(thread_id, coro_id, 0);
		if (!read_ok) {
			reporter->end_copy(thread_id, coro_id, 6, res);
			// return;
		}

		if (reply->key != 0xFFFFFFFF) {
			if (app_ctx.op == kEnumReadReq) {
				reporter->end_copy(thread_id, coro_id, 1, res);
				if (index_in_switch) {
					reporter->end_copy(thread_id, coro_id, 5, res);
				}
			}
			else
				reporter->end_copy(thread_id, coro_id, 2, res);
		}
		else
			reporter->end_copy(thread_id, coro_id, 3, res);
	}


	void cworker_new_request_kv(CoroMaster& sink, uint8_t coro_id) {

		// if (thread_id == 0 ) return; 
		auto& app_ctx = kv_ctx[coro_id];
		auto& ctx = coro_ctx[coro_id];
		app_ctx.magic++;

		bool cache_tag = false;
		uint8_t qp_id = coro_id % rpc::kConfigMaxQPCnt;

		reporter->begin(thread_id, coro_id, 0);
		// workloads
		app_ctx.op = (mehcached_zipf_next(&op_state) < FLAGS_read) ? kEnumReadReq : kEnumWriteReq;
		app_ctx.key = mehcached_zipf_next(&state) + 1;
		// app_ctx.key = sample->sample();
		app_ctx.server_id = 0;
		app_ctx.thread_id = 0;

		// qp
		rdma::QPInfo* qp = rpc_get_qp(rpc::FLAGS_qp_type,
			app_ctx.server_id,
			app_ctx.thread_id,
			qp_id);
		RawKvMsg* msg = (RawKvMsg*)qp->get_send_msg_addr();


		msg->flag = (app_ctx.key < 0xFFFFu) && FLAGS_cable_cache && FLAGS_switch_cache;
		if (msg->flag && app_ctx.op == kEnumWriteReq) {
			app_ctx.op = kEnumWriteAndRead; // write the data and the reply the data
		}

		// header
		msg->rpc_header = {
			.dst_s_id = app_ctx.server_id,
			.src_s_id = server_id,
			.src_t_id = thread_id,
			.src_c_id = coro_id,
			.local_id = 0,
			.op = app_ctx.op
		};


		msg->magic = app_ctx.magic;
		// msg
		msg->key = app_ctx.key;
		msg->log_id = msg->key;
		
		// sleep(1);
		// switch 
		msg->qp_map = (qp->steering_port);
		msg->switch_key = app_ctx.key & 0xFFFFu;
		

		msg->coro_id = msg->generate_coro_id(coro_id, app_ctx.op, thread_id);
		
		msg->kv_g_id = agent->get_global_id();
		msg->kv_c_id = coro_id;
		msg->client_port = rdma::toBigEndian16(agent->get_thread_port(thread_id, qp_id));

		uint8_t head_id = coro_id;

				// send
		if (msg->flag && cache.new_request(msg->key, coro_id, app_ctx.op, msg->magic, head_id)) {
			// printf("thread_id(%d,%d), cache!  op=%d , (coro_id=%lx) read key=%d\n", thread_id, coro_id, app_ctx.op, msg->coro_id, msg->key);
			// printf("thread_id(%d), op=%d , coro_id(%lx)(coro_id=%d) read key=%d\n", thread_id, app_ctx.op,  msg->coro_id, coro_id, msg->key);
			cache_tag = true; // client cache
			sink();
		}
		else {
			// if (msg->flag && !cache.check_fisrt(coro_id)) {
			if (msg->flag) {
				msg->coro_id = msg->generate_coro_id(head_id, app_ctx.op, thread_id);
			}
			// printf("thread_id(%d,%d), op=%d , (coro_id=%lx) read key=%d\n", thread_id, coro_id, app_ctx.op, msg->coro_id, msg->key);
			// }
			cache_tag = false;
			qp->modify_smsg_size(msg->get_size(app_ctx.op));
			qp->append_signal_smsg(true);
			qp->post_appended_smsg(&sink);
		}

		// recv + check
		auto reply = (RawKvMsg*)ctx.msg_ptr;
		if (reply->key != app_ctx.key) {
			printf("op=%d reply_op=%d I want %lu, but i get %u\n", app_ctx.op, reply->rpc_header.op, app_ctx.key, reply->key);
			printf("thread(%d,%d) coro_id=%lx \n", thread_id, coro_id, reply->coro_id);
			cache.print();
			exit(0);
		}
		// printf("thread_id(%d,%d), op=%d coro_id=%lx, get reply key=%u \n", thread_id, coro_id, reply->rpc_header.op, reply->coro_id, reply->key);

		uint64_t res = reporter->end(thread_id, coro_id, 0);
		if (app_ctx.op == kEnumReadReq) 
			reporter->end_copy(thread_id, coro_id, 4, res);
		else 
			reporter->end_copy(thread_id, coro_id, 5 , res);
		if (cache_tag)
			reporter->end_copy(thread_id, coro_id, 1, res);
		else if (reply->rpc_header.src_s_id != server_id || reply->rpc_header.src_t_id != thread_id)
			reporter->end_copy(thread_id, coro_id, 2, res);
		if (app_ctx.key < 0xFFFF)
			reporter->end_copy(thread_id, coro_id, 3, res);
	}

	// cpc
	void client_prepare() {
		mehcached_zipf_init(&state, FLAGS_keyspace - 1, FLAGS_zipf, thread_id + rand() % 1000);
		mehcached_zipf_init(&op_state, 100, 0, thread_id);
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
		cache.ctx_ptr = coro_ctx;
	}

	int8_t client_get_cid(void* reply_addr) {
		auto reply = (RawKvMsg*)reply_addr;
		// if (reply->rpc_header.src_s_id != server_id || reply->rpc_header.src_t_id != thread_id)
		// else
			
		if (reply->flag == 1) {
			
			// printf("thread_id(%d) coro_id(%lx), c_id = %d key = %d\n", thread_id, reply->coro_id,c_id, reply->key);
			return cache.check_reply(reply, thread_id);
		}
		else {
			return reply->rpc_header.src_c_id; // normal req
		}
		
	}

	void after_process_reply(void* reply, uint8_t c_id) {
		if (FLAGS_cable_cache && ((RawKvMsg *)reply)->flag) {
			for (size_t c = 0; c < cache.same_coro[c_id].size(); c++) {
				auto cache_id = cache.same_coro[c_id][c];
				coro_ctx[cache_id].msg_ptr = reply;
				(*coro_slave[cache_id])(); // client cache
				if (cache.reorder_wait_flag[cache_id] == 1) {
					coro_ctx[cache_id].free = false;
				}
			}
			if (cache.reorder_wait_flag[c_id] == 1) {
				coro_ctx[c_id].free = false;
			}

			cache.clear(c_id);
		}
		if (FLAGS_visibility) {
			// printf("ret = %x %d\n", ((RawKvMsg*)reply)->send_cnt, rdma::toBigEndian16(((RawKvMsg*)reply)->index_port));
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
		srand(233);
		for (uint i = 0; i < FLAGS_keyspace; i++) {
			generate_key(i, key_space[i]);
			if (i < 100) {
				// printf("key%d = %lx\n", i, key_space[i]);
				print_key(i, key_space[i]);
			}
		}

		for (int i = 0; i < thread_num; i++)
			th[i] = new std::thread(std::bind(&FsRpc::run_client_CPC, &worker_list[i]));
	}

	while (true) {
		reporter->try_wait();
		reporter->try_print(perf_config.type_cnt);
	}

	for (int i = 0; i < thread_num; i++)
		th[i]->join();

	return 0;
}