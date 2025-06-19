#include "bindcore.h"
#ifdef OBATCH
#include "include/index_wrapper.h"
#endif


/*for batch index*/
#define CTX_NUM 16


#ifdef COMT
#include "sm-coroutine.h"
#include "masstree_wrapper.h"
#endif

#include "include/test.h"
#include "include/timestamp.h"
#include "qp_info.h"
#include "rpc.h"
#include "sample.h"
// #include "timestamp.hh"
#include "zipf.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <gflags/gflags.h>
#include <sys/types.h>


DEFINE_uint64(keyspace, 100 * MB, "key space");
DEFINE_uint64(logspace, 10 * MB, "key space");

DEFINE_double(zipf, 0.99, "ZIPF");
DEFINE_int32(kv_size_b, 128, "test size B");
DEFINE_bool(visibility, false, "visibility");
DEFINE_bool(batch, false, "batch index and batch rpc");
DEFINE_uint32(batch_size, 16, "batch size");
DEFINE_uint32(read, 50, "read persentage");

DEFINE_int32(c_thread, 1, "client thread");
DEFINE_int32(dn_thread, 1, "dn_thread");
DEFINE_int32(mn_thread, 1, "mn_thread");

constexpr int kMaxCurrentIndexReq = 1500;


#ifdef COMT
MasstreeWrapper* global_tree_;
#endif
// constexpr int kDataThread = 1;
// constexpr int kIndexThread = 1;

enum RpcType {
  kEnumReadReq = 0,
  kEnumWriteReq,
  kEnumReadReply,
  kEnumWriteReply,

  kEnumAppendReq,
  kEnumAppendReply, // 5
  kEnumReadLog,

  ReplySwitch = 10,
  kEnumUpdateIndexReq,
  kEnumUpdateIndexReply,

  kEnumReadIndexReq,   // 13
  kEnumReadIndexReply, // 14

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
int test_percent[kTestTypeNum] = {0, 0, 0, 0, 50, 50};
int pre_sum[kTestTypeNum] = {};
TestType test_type[100];

uint32_t get_fingerprint(KvKeyType &key) {
  // return (key & 0xFFFFFFFFu) % (0xFFFFFFFFu - 1) + 1;
  return ((key >> 16) & 0xFFFFFFFFu) % (0xFFFFFFFFu - 1) + 1;
}
uint16_t get_switch_key(KvKeyType &key) { return key & 0xFFFFu; }
inline void generate_key(uint i, KvKeyType &key) {
  uint64_t tmp = rand();
  // key_space[i] = ((uint64_t)(std::hash<uint>()(i)) << 32) +
  // std::hash<uint>()(i);
  key = (tmp << 32) + rand();
  // key = i + 1;
}
inline void print_key(int i, KvKeyType &key) {
  printf("key%d = %lx\n", i, key);
}

rdma::GlobalConfig<rpc::kConfigServerCnt> global_config;
std::vector<rdma::ServerConfig> server_config;
const int kClientId = 2;

struct RawKvMsg {

  rpc::RpcHeader rpc_header;
  uint8_t flag;
  uint16_t switch_key;
  uint16_t kv_g_id; // [s:4][t:4]
  uint8_t kv_c_id; // [c:8]
  uint8_t magic;
  uint16_t server_map;
  uint16_t qp_map;  // 2 * 8  bitmap
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
  /*for each thread: [xxxx] [xxxx] : [write_coro_id: 0~14] [read_coro_id:
   * 0~14]*/
  uint64_t generate_coro_id(uint8_t c_id_, RpcType op, uint8_t thread_id) {
    // return ((uint64_t)(c_id_ + 1 ) | (op == kEnumReadReq ? 0ull : 0x80ull))
    // << (thread_id*8ull);
    return ((uint64_t)(c_id_ + 1))
           << (thread_id * 8ull + (op == kEnumReadReq ? 0 : 4));
  }

  static uint32_t get_size(int op = -1) {

    switch (op) {
    case kEnumReadLog:
      return sizeof(RawKvMsg);
    case kEnumReadReply:
      return sizeof(RawKvMsg) + sizeof(KvKeyType) + FLAGS_kv_size_b;
    case kEnumAppendReq:
      return sizeof(RawKvMsg) + sizeof(KvKeyType) + FLAGS_kv_size_b;
    case kEnumAppendReply:
      return sizeof(RawKvMsg) + sizeof(KvKeyType);

    case kEnumReadIndexReq:
      return sizeof(RawKvMsg) + sizeof(KvKeyType);
    case kEnumReadIndexReply:
      return sizeof(RawKvMsg);

    case kEnumMultiCastPkt:
      return sizeof(RawKvMsg) + sizeof(KvKeyType);
    case ReplySwitch:
      return sizeof(RawKvMsg);

    case kEnumUpdateIndexReq:
      return sizeof(RawKvMsg) + sizeof(KvKeyType);
    case kEnumUpdateIndexReply:
      return sizeof(RawKvMsg);

    case -1:
      return sizeof(RawKvMsg) + sizeof(KvKeyType) + FLAGS_kv_size_b; // max;
    default:
      return sizeof(RawKvMsg); // default size
      break;
    }
  }

  void *get_value_addr() {
    return (char *)this + sizeof(KvKeyType) + sizeof(RawKvMsg);
  }

  void *get_key_addr() { return (char *)this + sizeof(RawKvMsg); }

  void *get_log_addr() { return (char *)this + sizeof(RawKvMsg); }
  void print() {
    // printf("op=%d c_id=%d key=%x\n", rpc_header.op, rpc_header.src_c_id,
    // key);
  }

} __attribute__((packed));

void config() {
  server_config.clear();
  rdma::ServerConfig item;

  global_config.rc_msg_size = RawKvMsg::get_size();
  global_config.raw_msg_size = RawKvMsg::get_size();
  global_config.link_type = rdma::kEnumRoCE;
  // global_config.func = rdma::ConnectType::RAW;

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
                              .nic_name = "ens6",
                              .port_id = 188,
                          });

  server_config.push_back(item = {
                              .server_type = 2,
                              .thread_num = 1,
                              .numa_id = 1,
                              .numa_size = 21,
                              .dev_id = 1,
                              .nic_name = "ens6",
                              .port_id = 136,
                          });

  server_config.push_back(rdma::ServerConfig{
      .server_type = 2,
      .thread_num = FLAGS_c_thread,
      .numa_id = 0,
      .numa_size = 12,
      .dev_id = 0,
      .nic_name = "ens2",
      .port_id = 180,
  });

  server_config.push_back(rdma::ServerConfig{
      .server_type = 2,
      .thread_num = FLAGS_c_thread,
      .numa_id = 1,
      .numa_size = 12,
      .dev_id = 1,
      .nic_name = "ens6np0",
      .port_id = 172,
  });

  assert(server_config.size() <= rpc::kConfigServerCnt);
  for (int i = 0; i < rpc::kConfigServerCnt; i++) {
    for (int j = 0; j < rpc::kConfigServerCnt; j++)
      global_config.matrix[i][j] = rdma::TopoType::All;
  }
  int cnt = 0;
  test_percent[4] = FLAGS_read;
  test_percent[5] = 100 - FLAGS_read;
  pre_sum[0] = test_percent[0];

  for (uint i = 0; i < kTestTypeNum - 1; i++) {
    pre_sum[i + 1] = pre_sum[i] + test_percent[i + 1];
  }
  for (int i = 0; i < 100; i++) {
    while (i >= pre_sum[cnt]) {
      cnt++;
    }
    test_type[i] = (TestType)cnt;
    // printf("%d ",test_type[i]);
  }
}

perf::PerfConfig perf_config = {
    .thread_cnt = rpc::kConfigMaxThreadCnt,
    .coro_cnt = rpc::kConfigCoroCnt,
    .type_cnt = 8,
    .slowest_latency = 500,
};
Timestamp32* ts_generator;

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
  uint8_t *value;
  uint64_t log_id; // small key;
  bool index_in_switch;
  bool read_ok;

  uint64_t large_zipf;
  uint32_t timestamp;
};

struct SampleLog {
  char *buffer;
  uint64_t pos;
  const int log_size = FLAGS_kv_size_b + sizeof(KvKeyType);
  void init() {
    uint64_t size = ALIGNMENT_XB(log_size, 64) * FLAGS_logspace;
    std::cout << "key value size = " << perf::PerfTool::tp_to_string(size)
              << std::endl;
    buffer = (char*)malloc(size + 64);
    buffer = (char*) (((uint64_t)buffer + 63) & ~63);
    memset(buffer, 0, size);
    pos = 0;
  }

  bool get(uint64_t key, void *addr) {
    void *addr_s = (char *)buffer + key * ALIGNMENT_XB(log_size, 64);
    memcpy(addr, addr_s, log_size);
    return true;
  }

  bool put(uint64_t key, void *addr) {
    void *addr_s = (char *)buffer + key * ALIGNMENT_XB(log_size, 64);
    memcpy(addr_s, addr, log_size);
    return true;
  }

  uint64_t append(void *addr) {
    uint64_t cur_pos = pos;
    ADD_ROUND(pos, FLAGS_logspace);
    void *addr_s = (char *)buffer + cur_pos * ALIGNMENT_XB(log_size, 64);
    memcpy(addr_s, addr, log_size);
    return cur_pos;
  }
};

KvKeyType *key_space;

class KvRpc : public rpc::Rpc {

private:
  // server

  SampleLog kv;

  #ifdef OBATCH
  ThreadLocalIndex_masstree *index;
  #endif

  #ifdef COMT
  MasstreeWrapper* tree_;
  #endif

  // ThreadLocalIndex* index;

  uint8_t *userbuf;
  KvCtx kv_ctx[kCoroWorkerNum];
  sampling::StoRandomDistribution<>::rng_type *rng_ptr;
  struct zipf_gen_state state;
  struct zipf_gen_state op_state;

  uint16_t my_index_bitmap = 2;

  // int

  int index_buf_cnt[rpc::kConfigMaxThreadCnt] = {0}; //
  bool waiting[rpc::kConfigMaxThreadCnt] = {0};

  void server_process_msg(void *req_addr, void *reply_addr, rdma::QPInfo *qp) {
    auto req = (RawKvMsg *)req_addr;
    auto reply = (RawKvMsg *)reply_addr;
    reporter->begin(thread_id, 0, 0);

    reply->key = req->key;
    reply->send_cnt = req->send_cnt;
    // printf("key = %d\n", req->key);
    // sleep(1);
    reply->timestamp = req->timestamp;
    reply->index_port = req->index_port;
    uint64_t return_value;

    switch (req->rpc_header.op) {

    /*visibility*/
    case kEnumReadLog: // visibility
      kv.get(logid_2_locallogid(req->log_id), reply->get_log_addr());
      reply->rpc_header.op = kEnumReadReply;
      reporter->end_and_end_copy(thread_id, 0, 0, 1);
      break;
    case kEnumAppendReq: // visibility

      #ifdef LOGDEBUG
        printf("dn key = %llx\n", req->key);
      #endif
      {
      reply->log_id = compute_log_id(kv.append(req->get_log_addr()), thread_id);
      memcpy(reply->get_key_addr(), req->get_key_addr(), sizeof(KvKeyType));
      reply->rpc_header.op = kEnumAppendReply;
      reply->route_map = req->route_map;
      volatile uint32_t  tmpt = 0;
      do {
        tmpt = ts_generator->get_next_ts(req->switch_key);
        // printf("%u\n",reply->timestamp);
        // tmpt=1;
      } while (tmpt == 0);
      reply->timestamp = tmpt;

      reporter->end_and_end_copy(thread_id, 0, 0, 2);
      // printf("kEnumAppendReq pos=%d\n", reply->log_id);
      break;
      }
    #ifdef OBATCH

    case kEnumReadIndexReq: // visibility
      /*long key*/
      return_value = 0;
      if (!(index->get_long(*(KvKeyType *)req->get_key_addr(),
                            &(return_value)))) {
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
      index->put_long(*(KvKeyType *)req->get_key_addr(), req->log_id);

      reply->rpc_header.op = ReplySwitch;

      // printf("kEnumMultiCastPkt key=%x pos=%x ts=%d\n", req->key,
      // req->switch_key, req->timestamp);
      reporter->end_and_end_copy(thread_id, 0, 0, 3);
      break;
    case kEnumUpdateIndexReq:
      /*uint32_t key*/
      // index->put(req->key, req->log_id);

      /*long key*/
      index->put_long(*(KvKeyType *)req->get_key_addr(), req->log_id);
      reply->rpc_header.op = kEnumUpdateIndexReply;
      // printf("kEnumUpdateIndexReq key=%d pos=%d\n", req->key, req->log_id);
      // printf("SLOW key=%x pos=%x ts=%d\n", req->key, req->switch_key,
      // req->timestamp);
      reporter->end_and_end_copy(thread_id, 0, 0, 2);
      break;
    #endif
    default:
      printf("error op= %d s_id=%d d_id=%d flag=%d key=%d\n",
             req->rpc_header.op, req->rpc_header.src_s_id,
             req->rpc_header.dst_s_id, req->flag, req->key);
      exit(0);
      break;
    }
    reply->magic_in_switch = req->magic_in_switch;

    // printf("op = %d, magic = %d, key = %d\n", req->rpc_header.op,
    // rdma::toBigEndian16(req->magic_in_switch), req->key);

    reply->route_port =
        rdma::toBigEndian16(server_config[req->rpc_header.src_s_id].port_id);
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

    // printf("c_id=%d op=%d skey=%x, key=%lx %x/%x
    // shard_id=%x\n",req->rpc_header.src_c_id, req->rpc_header.op, req->key,
    // *(uint64_t*)req->get_key_addr(), reply->timestamp, req->timestamp,
    // ts_generator.get_shard_id(reply->switch_key));
  }

  void server_prepare() {
    reporter = perf::PerfTool::get_instance_ptr();
    reporter->worker_init(thread_id);

    if (server_id == 0) {
      kv.init();
      printf("thread_id=%d init kv\n", thread_id);
    } else {
      
  #ifdef OBATCH
      index = ThreadLocalIndex_masstree::get_instance();
      index->thread_init(thread_id);
  #endif

      printf("thread_id=%d init index\n", thread_id);
    }
  }

  // worker
  void cworker_new_request(CoroMaster &sink, uint8_t coro_id) {

    // if (server_id == 2) {
    //   sleep(10);
    // }

    if (FLAGS_zipf > 0.992) {
      // uint32_t x = sample->sample() % FLAGS_keyspace;
      // printf("x = %lu %lu %lu\n", x, FLAGS_keyspace, key_space[x]);
      kv_ctx[coro_id].long_key = key_space[sample->sample()];
    } else {
      kv_ctx[coro_id].key = mehcached_zipf_next(&state) + 1;
      kv_ctx[coro_id].long_key = key_space[mehcached_zipf_next(&state)];
    }

    uint16_t index_tid = get_index_thread_id(get_switch_key(kv_ctx[coro_id].long_key));
    if (index_buf_cnt[index_tid] > kMaxCurrentIndexReq ) {
      // if (coro_id == 0) {
      // printf("ohhhhhh! thread-%d %d find buf cnt=%x %d\n", thread_id,
      // index_tid, index_buf_cnt[index_tid], index_buf_cnt[index_tid]);
      if (!waiting[index_tid]) {
        waiting[index_tid] = true;
        check_req_cnt(sink, coro_id);
        waiting[index_tid] = false;
      }

      // }
      return;
    }
    // return cworker_new_request_kv(sink, coro_id);
    // cworker_new_request_index(sink, coro_id);
    kv_test(sink, coro_id);
  }

  // void read
  uint8_t get_data_server_id() { return 0; }
  uint8_t get_index_server_id() { return 1; }

  uint8_t get_data_thread_id_by_key(uint32_t key) {
    return key % FLAGS_dn_thread;
  }

  uint8_t get_data_thread_id_by_load(uint8_t thread_id, uint8_t coro_id) {
    return ((uint32_t)thread_id * rpc::FLAGS_coro_num + coro_id) % FLAGS_dn_thread;
  }

  uint8_t get_index_thread_id_by_load(uint8_t thread_id, uint8_t coro_id) {
    return ((uint32_t)thread_id * rpc::FLAGS_coro_num + coro_id) % FLAGS_mn_thread;
  }

  uint8_t get_index_thread_id(uint32_t key) { return key % FLAGS_mn_thread; }

  uint32_t logid_2_threadid(uint32_t log_id) { return log_id / FLAGS_keyspace; }

  uint32_t logid_2_locallogid(uint32_t log_id) {
    return log_id % FLAGS_keyspace;
  }

  uint32_t compute_log_id(uint32_t local_id, uint32_t thread_id) {
    return thread_id * FLAGS_keyspace + local_id;
  }

  void send_rpc() {}

  void get_index(CoroMaster &sink, rdma::CoroCtx &c_ctx) {
    rdma::QPInfo *qp;
    RawKvMsg *msg;
    auto &app_ctx = *(KvCtx *)c_ctx.app_ctx;
    uint8_t qp_id = app_ctx.coro_id % rpc::kConfigMaxQPCnt;
    // uint8_t index_tid = get_index_thread_id_by_load(thread_id,
    // app_ctx.coro_id);
    uint8_t index_tid = get_index_thread_id(get_switch_key(app_ctx.long_key));

    qp = rpc_get_qp(rpc::FLAGS_qp_type, app_ctx.index_s_id, index_tid, qp_id);
    msg = (RawKvMsg *)qp->get_send_msg_addr();
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
    msg->client_port =
        rdma::toBigEndian16(agent->get_thread_port(thread_id, qp_id));
    msg->route_port = rdma::toBigEndian16(server_config[server_id].port_id);
    msg->index_port = rdma::toBigEndian16(agent->get_thread_port(index_tid, qp_id));
    msg->timestamp = 0;
    qp->modify_smsg_size(msg->get_size(msg->rpc_header.op));
    qp->append_signal_smsg();
    qp->post_appended_smsg(&sink);

    RawKvMsg *reply = (RawKvMsg *)c_ctx.msg_ptr;
    reply->print();
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

  void put_index(CoroMaster &sink, rdma::CoroCtx &c_ctx) {
    rdma::QPInfo *qp;
    RawKvMsg *msg;
    auto &app_ctx = *(KvCtx *)c_ctx.app_ctx;
    uint8_t qp_id = app_ctx.coro_id % rpc::kConfigMaxQPCnt;
    // uint8_t index_tid = get_index_thread_id_by_load(thread_id,
    // app_ctx.coro_id);
    uint8_t index_tid = get_index_thread_id(get_switch_key(app_ctx.long_key));

    qp = rpc_get_qp(rpc::FLAGS_qp_type, app_ctx.index_s_id, index_tid, qp_id);
    msg = (RawKvMsg *)qp->get_send_msg_addr();
    // msg
    msg->rpc_header = {
        .dst_s_id = app_ctx.index_s_id,
        .src_s_id = server_id,
        .src_t_id = thread_id,
        .src_c_id = app_ctx.coro_id,
        .local_id = 0,
        .op = kEnumUpdateIndexReq,
    };

    msg->route_map = my_index_bitmap;
    msg->key = app_ctx.key;
    msg->magic = app_ctx.magic;
    msg->log_id = app_ctx.log_id;
    
		msg->flag = 0;

    msg->switch_key = get_switch_key(app_ctx.long_key);
    msg->index_port = rdma::toBigEndian16(agent->get_thread_port(index_tid, qp_id));
    memcpy(msg->get_key_addr(), &app_ctx.long_key, sizeof(KvKeyType));
    msg->client_port =
        rdma::toBigEndian16(agent->get_thread_port(thread_id, qp_id));
    msg->timestamp = app_ctx.timestamp;
    qp->modify_smsg_size(msg->get_size(msg->rpc_header.op));
    qp->append_signal_smsg();
    qp->post_appended_smsg(&sink);
    // puts("write send 2");

    RawKvMsg *reply = (RawKvMsg *)c_ctx.msg_ptr;
    reply->print();

    #ifdef LOGDEBUG
      printf("xxxx: op=%d appkey=%lx %lx\n", reply->rpc_header.op, reply->key, app_ctx.key);
    #endif

    assert(reply->key == app_ctx.key);
    if (reply->rpc_header.src_t_id != thread_id) {
      printf("thread_id=%d\n", reply->rpc_header.src_t_id);
    }
    assert(reply->rpc_header.src_t_id == thread_id);
    if (FLAGS_visibility &&
        reply->rpc_header.op == kEnumMultiCastPkt) { // check reply
      // puts("got multicast");
      uint64_t res = reporter->end(thread_id, app_ctx.coro_id, 0);
      reporter->end_copy(thread_id, app_ctx.coro_id, 2, res);
      return;
    }
    assert(reply->rpc_header.op == kEnumUpdateIndexReply);
    // printf("write reply key=%d\n", app_ctx.key);
  }

  void get_value(CoroMaster &sink, rdma::CoroCtx &c_ctx) {
    rdma::QPInfo *qp;
    RawKvMsg *msg;
    auto &app_ctx = *(KvCtx *)c_ctx.app_ctx;
    uint8_t qp_id = app_ctx.coro_id % rpc::kConfigMaxQPCnt;
    // uint8_t index_tid = get_index_thread_id_by_load(thread_id,
    // app_ctx.coro_id);
    uint8_t index_tid = get_index_thread_id(get_switch_key(app_ctx.long_key));

    qp = rpc_get_qp(rpc::FLAGS_qp_type, app_ctx.data_s_id,
                    logid_2_threadid(app_ctx.log_id), qp_id);
    msg = (RawKvMsg *)qp->get_send_msg_addr();
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
    msg->timestamp = 0;

    msg->key = app_ctx.key;
    msg->log_id = app_ctx.log_id;
    // printf("log_id = %d thread_id = %d\n", reply->log_id,
    // logid_2_threadid(reply->log_id));
    msg->flag = FLAGS_visibility ? visibility : 0;
    msg->switch_key = get_switch_key(app_ctx.long_key);
    msg->index_port = rdma::toBigEndian16(agent->get_thread_port(index_tid, qp_id));
    // send
    qp->modify_smsg_size(msg->get_size(msg->rpc_header.op));
    qp->append_signal_smsg();
    qp->post_appended_smsg(&sink);
    RawKvMsg *reply = (RawKvMsg *)c_ctx.msg_ptr;
    reply->print();
    memcpy(userbuf, reply->get_value_addr(), FLAGS_kv_size_b);
    assert(reply->rpc_header.op == kEnumReadReply);
    KvKeyType *key_addr = (KvKeyType *)reply->get_key_addr();
    if (reply->key != 0xFFFFFFFF && (*key_addr) != app_ctx.long_key) {
      // printf("reqlkey(%lu) != replylkey(%lu), reqkey(%lu) != replykey(%u)
      // fingerprint(%u) == fingerprint(%u)\n", app_ctx.long_key, *key_addr,
      // app_ctx.key, reply->key,
      // get_fingerprint(app_ctx.long_key), get_fingerprint(*key_addr)
      // );
      if (get_fingerprint(app_ctx.long_key) == get_fingerprint(*key_addr))
        app_ctx.read_ok = false;
    }
    return;
  }

  void put_value(CoroMaster &sink, rdma::CoroCtx &c_ctx) {
    rdma::QPInfo *qp;
    RawKvMsg *msg;
    auto &app_ctx = *(KvCtx *)c_ctx.app_ctx;
    uint8_t qp_id = app_ctx.coro_id % rpc::kConfigMaxQPCnt;

    uint8_t index_tid = get_index_thread_id(app_ctx.long_key);

    qp = rpc_get_qp(rpc::FLAGS_qp_type, app_ctx.data_s_id,
                    get_data_thread_id_by_key(app_ctx.long_key),
                    qp_id);
    // qp = rpc_get_qp(rpc::FLAGS_qp_type, app_ctx.data_s_id,
    // get_data_thread_id_by_key(app_ctx.key), qp_id);
    msg = (RawKvMsg *)qp->get_send_msg_addr();
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
    msg->client_port =
        rdma::toBigEndian16(agent->get_thread_port(thread_id, qp_id));
    msg->index_port = rdma::toBigEndian16(agent->get_thread_port(index_tid, qp_id));
    msg->route_map = my_index_bitmap;
    msg->timestamp = 0;
    memcpy(msg->get_key_addr(), &app_ctx.long_key, sizeof(KvKeyType));
    memcpy(msg->get_value_addr(), userbuf, FLAGS_kv_size_b);
    qp->modify_smsg_size(msg->get_size(msg->rpc_header.op));
    qp->append_signal_smsg();
    // puts("write send 2");
    qp->post_appended_smsg(&sink);
    // puts("write send 1");
    RawKvMsg *reply = (RawKvMsg *)c_ctx.msg_ptr;
    reply->print();

    app_ctx.timestamp = reply->timestamp;
    app_ctx.log_id = reply->log_id;

    #ifdef LOGDEBUG
      printf("xxxx: op=%d appkey=%lx %lx\n", reply->rpc_header.op, reply->key, app_ctx.key);
    #endif

    if (reply->key != app_ctx.key) {
      printf("op=%d key=%x, appkey=%lx %lx (c_id=%d, coro=%d) (msg->skey=%d, "
             "reply->skey=%u) cnt=%d\n",
             reply->rpc_header.op, reply->key, app_ctx.key, app_ctx.long_key,
             reply->rpc_header.src_t_id, thread_id, msg->switch_key,
             reply->switch_key, index_buf_cnt[index_tid]);

      assert(reply->key == app_ctx.key);
      exit(0);
    }
  }

  void kv_test(CoroMaster &sink, uint8_t coro_id) {
    auto &app_ctx = kv_ctx[coro_id];
    auto &ctx = coro_ctx[coro_id];

    reporter->begin(thread_id, coro_id, 0);

    app_ctx.test_type = test_type[mehcached_zipf_next(&op_state)];

    app_ctx.key = get_fingerprint(app_ctx.long_key);
    app_ctx.magic++; // Important for rpc_id !!!
    if (app_ctx.magic == 0) {
      app_ctx.magic++;
    }

    app_ctx.data_s_id = get_data_server_id();
    app_ctx.index_s_id = get_index_server_id();

    // if (warmup_insert < kInitPercentage * FLAGS_keyspace / 100 /
    // agent->get_thread_num()) { 	app_ctx.test_type =
    // (TestType)((app_ctx.test_type) | 1); // from read to write
    // 	warmup_insert++;
    // }

    app_ctx.index_in_switch = false;
    app_ctx.read_ok = true;
    // bool read_ok = true;
    RawKvMsg *reply;

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
      reply = (RawKvMsg *)ctx.msg_ptr;

      #ifdef LOGDEBUG 
        printf("w r1: op=%d\n", reply->rpc_header.op);
      #endif
      /*phase 1*/
      if (FLAGS_visibility &&
          reply->rpc_header.op == kEnumMultiCastPkt) { // check reply
        // puts("got multicast");
        uint64_t res = reporter->end(thread_id, coro_id, 0);
        reporter->end_copy(thread_id, coro_id, 2, res);
        reporter->end_copy(thread_id, coro_id, 4, res);
        break;
      }
      put_index(sink, ctx);

      reply = (RawKvMsg *)ctx.msg_ptr;

      #ifdef LOGDEBUG 
        printf("w r2: op=%d\n", reply->rpc_header.op);
      #endif

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
    reply = (RawKvMsg *)ctx.msg_ptr;

    if (FLAGS_visibility &&
        reply->rpc_header.op == kEnumMultiCastPkt) { // check reply
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
      } else
        reporter->end_copy(thread_id, coro_id, 2, res);
    } else
      reporter->end_copy(thread_id, coro_id, 3, res);
  }

  // cpc
  void client_prepare() {
    my_index_bitmap |= (1 << server_id);
    mehcached_zipf_init(&op_state, 100, 0, thread_id);
    mehcached_zipf_init(&state, FLAGS_keyspace - 1, FLAGS_zipf,
                        thread_id + rand() % 1000);

    memset(index_buf_cnt, 0, sizeof(index_buf_cnt));
    memset(waiting, 0, sizeof(waiting));

    reporter = perf::PerfTool::get_instance_ptr();
    reporter->worker_init(thread_id);
    userbuf = (uint8_t *)malloc(FLAGS_kv_size_b * 2);

    for (int i = 0; i < rpc::FLAGS_coro_num; i++) {
      coro_ctx[i].app_ctx = &kv_ctx[i];
      kv_ctx[i].coro_id = i;
      kv_ctx[i].value = (uint8_t *)malloc(FLAGS_kv_size_b);
    }
  }

  int8_t client_get_cid(void *reply_addr) {
    auto reply = (RawKvMsg *)reply_addr;
    return reply->rpc_header.src_c_id; // normal req
  }

  void after_process_reply(void *reply, uint8_t c_id) {
    if (FLAGS_visibility) {
      // printf("ret = %x %d\n", ((RawKvMsg*)reply)->send_cnt,
      // rdma::toBigEndian16(((RawKvMsg*)reply)->index_port)); sleep(1);
      index_buf_cnt[(uint16_t)log2(
          rdma::toBigEndian16(((RawKvMsg *)reply)->index_port))] =
          (rdma::toBigEndian16(((RawKvMsg *)reply)->send_cnt));
    }
  }

  void check_req_cnt(CoroMaster &sink, uint8_t coro_id) {
    reporter->begin(thread_id, coro_id, 7);
    RawKvMsg *msg, *reply;
    rdma::QPInfo *qp = rpc_get_qp(1, 0, 0, 0);
    msg = (RawKvMsg *)qp->get_send_msg_addr();
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
    uint8_t index_tid =
        get_index_thread_id(get_switch_key(kv_ctx[coro_id].long_key));
    msg->index_port = rdma::toBigEndian16(agent->get_thread_port(index_tid, 0));
    qp->modify_smsg_size(msg->get_size(kEnumReadSend));
    qp->append_signal_smsg();
    qp->post_appended_smsg(&sink);
    reply = (RawKvMsg *)coro_ctx[coro_id].msg_ptr;
    if (reply->rpc_header.op != kEnumReadSend) {
      exit(-1);
    }
    reporter->end(thread_id, coro_id, 7);
    // printf("cnt=%d update!\n", rdma::toBigEndian16(reply->send_cnt));
  }

public:
  KvRpc(/* args */) {}
  ~KvRpc() {}
  sampling::StoRandomDistribution<> *sample; /*Fast*/

  struct batchEntry {
      rdma::QPInfo *qp;
      RawKvMsg *reply_addr;
      uint64_t key; 
      uint64_t timestamp;
      bool operator < (const batchEntry y) const {
        if (key == y.key)  
          return timestamp > y.timestamp;
        return key < y.key;
      }

  };
  batchEntry* batch;

  struct Ctx {
    bool free;
  };

  Ctx ctx[CTX_NUM + rdma::CQInfo::kMaxWcCnt];

#ifdef COMT
inline PROMISE(void) worker(int coro_id) {
    int cnt = 0;

    SUSPEND;

    // puts("??");

    while (true)
    {
        ctx[coro_id].free = false;
        auto reply = batch[coro_id].reply_addr;

        auto op = reply->rpc_header.op;
        if (op == ReplySwitch) {
            auto ret_value = *(uint64_t*)reply->get_value_addr();
            AWAIT tree_->insert(batch[coro_id].key, ret_value);
        }
        else if (op == kEnumUpdateIndexReply) {
            auto ret_value = *(uint64_t*)reply->get_value_addr();
            // AWAIT tree_->search(batch[coro_id]->key, ret_value);
            AWAIT tree_->insert(batch[coro_id].key, ret_value);
        }
        else if (op == kEnumReadIndexReply) {
            auto& ret_value = *(uint64_t*)reply->get_value_addr();
            AWAIT tree_->search(batch[coro_id].key, ret_value);
        }

        ctx[coro_id].free = true;

        SUSPEND;
    }
    RETURN;
}
#endif

  void server_batch_CPC() {
    init();
    server_prepare();

    #ifdef COMT
    tree_ = global_tree_;
    tree_->thread_init(thread_id);
    std::vector<ermia::coro::task<void>> tasks(CTX_NUM + rdma::CQInfo::kMaxWcCnt);
    for (int i = 0; i < FLAGS_batch_size + rdma::CQInfo::kMaxWcCnt; i++) {
        // puts("?");
        tasks[i] = worker(i);
        tasks[i].start();
        ctx[i].free = true;
    }
    #endif

    connect();

    uint32_t max_size = FLAGS_batch_size + rdma::CQInfo::kMaxWcCnt;
    int batch_cnt = 0;

    
    batch = new batchEntry[max_size];
		bool onpath = false;

    while (true) {

      ibv_wc *wc = nullptr;
			onpath = false;

      auto wc_array = agent->cq->poll_cq_try(rdma::CQInfo::kMaxWcCnt);
      reporter->many_end(wc_array->total_wc_count, thread_id, 0, 0);
      while ((wc = wc_array->get_next_wc())) {
        auto qp = ((rdma::WrInfo *)wc->wr_id)->qp;
        auto req = qp->get_recv_msg_addr();
        auto reply = qp->get_send_msg_addr();

        auto &req_header = *(RawKvMsg *)req;
        auto &reply_header = *(RawKvMsg *)reply;

				memcpy(reply, req, RawKvMsg::get_size(req_header.rpc_header.op));
        
        #ifdef LOGDEBUG
          printf("req op = %d, key = %llx\n", req_header.rpc_header.op, ((RawKvMsg *)req)->key);
        #endif

				switch (req_header.rpc_header.op) {
					case kEnumReadIndexReq: // visibility
						onpath = true;
						reply_header.rpc_header.op = kEnumReadIndexReply;
						break;
					case kEnumMultiCastPkt:
						reply_header.rpc_header.op = ReplySwitch;
						break;
					case kEnumUpdateIndexReq:
						onpath = true;
						reply_header.rpc_header.op = kEnumUpdateIndexReply;
						break;
					default:
						puts("?");
						exit(0);
						break;
        }
				
				qp->modify_smsg_size(reply_header.get_size(reply_header.rpc_header.op));

				qp->modify_smsg_dst_port(
            agent->get_thread_port(req_header.rpc_header.src_t_id, qp->qp_id));
        reply_header.rpc_header.dst_s_id = req_header.rpc_header.src_s_id;
				// reply_header.rpc_header.src_s_id = server_id;
        batch[batch_cnt].qp = qp;
        batch[batch_cnt].key = *(uint64_t *)((RawKvMsg *)req)->get_key_addr();
        batch[batch_cnt].timestamp = __builtin_bswap32(((RawKvMsg *)req)->timestamp);
        // batch[batch_cnt]. = req_header.rpc_header.op;
        batch[batch_cnt++].reply_addr = (RawKvMsg *)reply;

        auto reply_ = (RawKvMsg *)reply;

        reply_->route_port = rdma::toBigEndian16(server_config[reply_->rpc_header.dst_s_id].port_id);
        reply_->qp_map = 0;
        reply_->server_map = 0;
        // reply_->send_cnt = 0;
        reply_->index_port = ((RawKvMsg *)req)->index_port;

				qp->free_recv_msg();
				qp->append_signal_smsg();
        // server_process_msg(req, reply, qp);

        // qp->append_signal_smsg();
      }
      if (batch_cnt < FLAGS_batch_size && !onpath && FLAGS_batch) {
        continue;
      }

      #ifdef LOGDEBUG
        printf("batch = %d\n", batch_cnt);
      #endif

			std::sort(batch, batch + batch_cnt);

			uint64_t last_key = 0;

      #ifdef OBATCH
      for (int i = 0; i < batch_cnt; i++) {

        auto reply = batch[i].reply_addr;

        reply->send_cnt = 0;

        uint64_t return_value;

        switch (reply->rpc_header.op) {
					case kEnumReadIndexReply: // visibility
						return_value = 0;
						if (!(index->get_long(*(KvKeyType *)reply->get_key_addr(),
																	&(return_value)))) {
							reply->key = 0xFFFFFFFF;
						}
						reply->log_id = (uint32_t)return_value;
						break;
					case ReplySwitch:
						if (*(KvKeyType *)reply->get_key_addr() != last_key)
							index->put_long(*(KvKeyType *)reply->get_key_addr(), reply->log_id);
						break;
					case kEnumUpdateIndexReply:
						if (*(KvKeyType *)reply->get_key_addr() != last_key)
							index->put_long(*(KvKeyType *)reply->get_key_addr(), reply->log_id);
						break;
					default:
						puts("?");
						exit(0);
						break;
        }
				last_key = *(KvKeyType *)reply->get_key_addr();
      }

      #endif

      #ifdef COMT
      int cnt = 0;

      for (int i = 0; i < batch_cnt; i++) {
          if (last_key != batch[i].key || (batch[i].reply_addr->rpc_header.op) == kEnumReadIndexReq) {
              #ifdef LOGDEBUG
                printf("%llx key=%llx\n", batch[i].key, batch[i].reply_addr->key);
              #endif
              
              cnt++;
              tasks[i].resume();
              if (ctx[i].free == true) {
                  cnt--;
              }
              last_key = batch[i].key;
          }
      }
        
      while (cnt > 0) {
          for (int i = 0; i < batch_cnt; i++) {
              if (ctx[i].free != true) {
                  tasks[i].resume();
                  if (ctx[i].free == true) {
                      cnt--;
                  }
              }
          }
      }
      #endif


			batch_cnt = 0;

      for (int i = 0; i < rpc::kConfigMaxQPCnt; i++) {
        agent->get_raw_qp(i).post_appended_smsg();
      }
    }
  }
};

int main(int argc, char **argv) {

  FLAGS_logtostderr = 1;
  gflags::SetUsageMessage("Usage ./rdma_clean_client --help");
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  

  LOG(INFO) << "Raw packet size (" << sizeof(RawKvMsg) << ") bytes.";

  config();

  int server_type = server_config[rpc::FLAGS_node_id].server_type;

  if (server_type == 0) {
    rdma::FLAGS_qp_len = 1024;
  }
  else if (server_type == 1) {
    rdma::FLAGS_qp_len = 2048 + ALIGNMENT_XB(rpc::FLAGS_coro_num * FLAGS_c_thread * 2, POSTPIPE);
  }
  else {
    rdma::FLAGS_qp_len = 256;
  }
  rdma::FLAGS_cq_len = rdma::FLAGS_qp_len;


  perf_config.thread_cnt = server_config[rpc::FLAGS_node_id].thread_num;

  perf::PerfTool *reporter = perf::PerfTool::get_instance_ptr(&perf_config);
  if (server_type == 2) {
    reporter->new_type("all");
    reporter->new_type("read");
    reporter->new_type("write");
    reporter->new_type("r-n-exist");
    reporter->new_type("fast write");
    reporter->new_type("fast read");
    reporter->new_type("read retry");
    reporter->new_type("waiting");
  } else if (server_type == 0) { // data node
    reporter->new_type("all");
    reporter->new_type("read");
    reporter->new_type("write");
  } else if (server_type == 1) { // index node
  #ifdef OBATCH
    ThreadLocalIndex::get_instance();
  #endif
    reporter->new_type("all");
    reporter->new_type("readi");
    reporter->new_type("writei");
    reporter->new_type("fast index");
  }
  reporter->master_thread_init();

  Memcached::initMemcached(rpc::FLAGS_node_id);

  int thread_num = server_config[rpc::FLAGS_node_id].thread_num;
  std::thread **th = new std::thread *[thread_num];
  KvRpc *worker_list = new KvRpc[thread_num];

  if (server_type == 0) {
    ts_generator = new Timestamp32();
    ts_generator->init();
  }

  for (int i = 0; i < thread_num; i++) {
    worker_list[i].config_client(server_config, &global_config,
                                 rpc::FLAGS_node_id, i);
  }

  BindCore(server_config[rpc::FLAGS_node_id].numa_id * server_config[rpc::FLAGS_node_id].numa_size);

  if (server_type != 0) {
    key_space = (KvKeyType *)malloc(sizeof(KvKeyType) * FLAGS_keyspace);
    
    #ifdef OBATCH 
    ThreadLocalIndex_masstree *index;
    #endif

    #ifdef COMT
    MasstreeWrapper* tree_;
    #endif

    if (server_type == 1) {
      #ifdef OBATCH 
      index = ThreadLocalIndex_masstree::get_instance();
      index->thread_init(FLAGS_mn_thread);
      #endif

      #ifdef COMT
      global_tree_ = new MasstreeWrapper();
      tree_ = global_tree_;
      tree_->thread_init(FLAGS_mn_thread);
      #endif

    }
    srand(233);
    for (uint i = 0; i < FLAGS_keyspace; i++) { 
      generate_key(i, key_space[i]);
      if (i < 100) {
        // printf("key%d = %lx\n", i, key_space[i]);
        print_key(i, key_space[i]);
      }
      // index->(uint32_t key, uint32_t *addr_addr)
      if (server_type == 1) {
        #ifdef OBATCH
        index->put_long(key_space[i], 0);
        #endif

        #ifdef COMT
        sync_wait_coro(tree_->insert(key_space[i], 0));
        #endif
        
      }
    }
  }

  sampling::StoRandomDistribution<>::rng_type *rng_ptr;
  sampling::StoZipfDistribution<> *sample = nullptr;

  if (server_type == 2) {
    if (FLAGS_zipf > 0.992) {
      rng_ptr = new sampling::StoRandomDistribution<>::rng_type(0);
      sample = new sampling::StoZipfDistribution<>(
          *rng_ptr, 0, FLAGS_keyspace - 1, FLAGS_zipf);
    }
    for (int i = 0; i < thread_num; i++)
      worker_list[i].sample = sample;
  }

  if (server_type == 0) {
    for (int i = 0; i < thread_num; i++)
      th[i] =
          new std::thread(std::bind(&KvRpc::run_server_CPC, &worker_list[i]));
  } else if (server_type == 1) {
    for (int i = 0; i < thread_num; i++) {
      #ifdef OBATCH
      if (FLAGS_batch) 
        th[i] = new std::thread(std::bind(&KvRpc::server_batch_CPC, &worker_list[i]));
      else
        th[i] = new std::thread(std::bind(&KvRpc::run_server_CPC, &worker_list[i]));
      #endif
      #ifdef COMT
      th[i] = new std::thread(std::bind(&KvRpc::server_batch_CPC, &worker_list[i]));
      #endif
		}
  } else {
    for (int i = 0; i < thread_num; i++)
      th[i] =
          new std::thread(std::bind(&KvRpc::run_client_CPC, &worker_list[i]));
  }
  int cnt = 0;
  puts("bind main");
  BindCore(server_config[rpc::FLAGS_node_id].numa_id * server_config[rpc::FLAGS_node_id].numa_size + thread_num);
  reporter->try_wait();
  while (true) {
    reporter->try_wait();
    if (reporter->try_print(perf_config.type_cnt))
      cnt++;
    if (cnt > 12) {
      return 0;
    }
  }

  for (int i = 0; i < thread_num; i++)
    th[i]->join();

  return 0;
}