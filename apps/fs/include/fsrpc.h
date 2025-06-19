#pragma once

#include "common.h"
#include "fscommon.h"
#include "fsconfig.h"
#include "pkt.h"
#include "qp_info.h"
#include "rdma.h"
#include "rpc.h"
#include "sample.h"
#include "timestamp.h"
#include "zipf.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <gflags/gflags.h>
#include <string>
#include <sys/types.h>
#include "fss.h"

// constexpr int kDataThread = 1;
// constexpr int kIndexThread = 1;
extern RPCServer * fs_server;
extern Timestamp32 ts_generator;

inline uint32_t get_fingerprint(KvKeyType &key) {
  // return (key & 0xFFFFFFFFu) % (0xFFFFFFFFu - 1) + 1;
  return ((key >> 16) & 0xFFFFFFFFu) % (0xFFFFFFFFu - 1) + 1;
}
inline uint16_t get_switch_key(KvKeyType &key) { return key & 0xFFFFu; }
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

struct FsCtx {
  enum RpcType op;
  enum TestType test_type;
  uint8_t coro_id;
  uint64_t key;
  uint8_t server_id;
  uint8_t data_s_id;
  uint8_t data_t_id;

  uint8_t index_s_id;
  uint8_t index_t_id;

  uint8_t thread_id;
  uint16_t magic;
  KvKeyType long_key;
  uint8_t *value;
  uint64_t log_id; // small key;
  bool index_in_switch;
  bool read_ok;

  uint32_t ret_type;

  uint64_t large_zipf;
  uint32_t timestamp;

  char _file[MAX_FILE_NAME_LENGTH];
  UniqueHash hashUnique;
  const void *buffer;
  uint64_t size;
  uint64_t offset;
  uint64_t count;
  std::string prefix;
  // uint64_t total_cnt;
};

struct SampleLog {
  void *buffer;
  uint64_t pos;
  const int log_size = FLAGS_kv_size_b + sizeof(KvKeyType);
  void init() {
    uint64_t size = ALIGNMENT_XB(log_size, 64) * FLAGS_logspace;
    std::cout << "key value size = " << perf::PerfTool::tp_to_string(size)
              << std::endl;
    buffer = malloc(size);
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

class FsRpc : public rpc::Rpc {

private:
  // server
  /*octopus*/
  // struct  timeval start1, end1;
  typedef char *nrfsFile;
  int nrfsWrite(CoroMaster &sink, rdma::CoroCtx &c_ctx, int cnt = 1);
  int nrfsRead(CoroMaster &sink, rdma::CoroCtx &c_ctx, int cnt = 1);
  void fill_raw_header(RawVMsg *raw, FsCtx *app_ctx);
  // int nrfsRead(nrfsFile _file, void *buffer, uint64_t size, uint64_t offset,
  //              CoroMaster &sink, rdma::CoroCtx &c_ctx);

  SampleLog kv;

  uint8_t *userbuf;
  FsCtx fs_ctx[kCoroWorkerNum];
  sampling::StoRandomDistribution<>::rng_type *rng_ptr;
  struct zipf_gen_state state;
  struct zipf_gen_state op_state;

  uint16_t my_index_bitmap = 2;

  // int

  int index_buf_cnt[rpc::kConfigMaxThreadCnt] = {0}; //

  void server_process_msg(void *req_addr, void *reply_addr, rdma::QPInfo *qp) {
    return;
  }

  void server_prepare() {
    reporter = perf::PerfTool::get_instance_ptr();
    reporter->worker_init(thread_id);

    if (server_id == 0) {

    } else {
    }
    // LOG(INFO) <<"server_prepare ( "<< thread_id <<" )";
  }

  // worker
  void cworker_new_request(CoroMaster &sink, uint8_t coro_id) {

    if (FLAGS_zipf > 0.992) {
      // uint32_t x = sample->sample() % FLAGS_keyspace;
      // printf("x = %lu %lu %lu\n", x, FLAGS_keyspace, key_space[x]);
      fs_ctx[coro_id].long_key = key_space[sample->sample()];
    } else {
      fs_ctx[coro_id].key = mehcached_zipf_next(&state) + 1;
      // fs_ctx[coro_id].long_key = key_space[mehcached_zipf_next(&state)];
      fs_ctx[coro_id].long_key = key_space[mehcached_zipf_next(&state) % MAX_DIRECTORY_COUNT];
      std::string tmp =
          fs_ctx[coro_id].prefix + std::to_string(fs_ctx[coro_id].long_key);
      memset(fs_ctx[coro_id]._file, 0, MAX_FILE_NAME_LENGTH);
      memcpy(fs_ctx[coro_id]._file, tmp.c_str(), tmp.length());

      fs_ctx[coro_id].size = std::min((uint32_t)BLOCK_SIZE, FLAGS_block_size);
      fs_ctx[coro_id].offset = 0;
    }
    HashTable::getUniqueHash(fs_ctx[coro_id]._file, strlen(fs_ctx[coro_id]._file), &fs_ctx[coro_id].hashUnique);
    auto &app_ctx = fs_ctx[coro_id];
    app_ctx.data_s_id = get_data_server_id();
    app_ctx.data_t_id = get_data_thread_id(get_switch_key(fs_ctx[coro_id].hashUnique.value[3]));
    app_ctx.index_s_id = get_index_server_id();
    app_ctx.index_t_id = get_index_thread_id(get_switch_key(fs_ctx[coro_id].hashUnique.value[3]));

    #ifdef LOGDEBUG
          // LOG(INFO) << "count " << (uint64_t)fs_ctx[coro_id].hashUnique.value[3];
          printf("hash %lx\n", fs_ctx[coro_id].hashUnique.value[3]);
    #endif

    if (index_buf_cnt[app_ctx.index_t_id] > kMaxCurrentIndexReq) {
      // if (coro_id == 0) {
      // printf("ohhhhhh! thread-%d %d find buf cnt=%x %d\n", thread_id,
      // index_tid, index_buf_cnt[index_tid], index_buf_cnt[index_tid]);
      #ifdef LOGDEBUG
          LOG(INFO) << "count " << (int)index_buf_cnt[app_ctx.index_t_id];
      #endif
      check_req_cnt(sink, coro_id);
      // }
      return;
    }
    // return cworker_new_request_kv(sink, coro_id);
    // cworker_new_request_index(sink, coro_id);
    fs_test(sink, coro_id);
  }

  // void read
  uint8_t get_data_server_id() { return 0; }
  uint8_t get_index_server_id() { return 1; }

  uint8_t get_data_thread_id(uint32_t key) { return key % FLAGS_dn_thread; }

  uint8_t get_data_thread_id_by_load(uint8_t thread_id, uint8_t coro_id) {
    return (thread_id * rpc::FLAGS_coro_num + coro_id) % FLAGS_dn_thread;
  }

  uint8_t get_index_thread_id_by_load(uint8_t thread_id, uint8_t coro_id) {
    return (thread_id * rpc::FLAGS_coro_num + coro_id) % FLAGS_mn_thread;
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

  void fs_test(CoroMaster &sink, uint8_t coro_id) {
    auto &app_ctx = fs_ctx[coro_id];
    auto &ctx = coro_ctx[coro_id];
    

    app_ctx.test_type = test_type[mehcached_zipf_next(&op_state)];

    app_ctx.key = get_fingerprint(app_ctx.hashUnique.value[3]);

    app_ctx.magic++; // Important for rpc_id !!!
    if (app_ctx.magic == 0) {
      app_ctx.magic++;
    }

    app_ctx.index_in_switch = false;
    app_ctx.read_ok = true;
    // bool read_ok = true;
    // RawVMsg *reply;

    uint64_t ret_size = 0;

    switch (app_ctx.test_type) {
    case kEnumR: {
      reporter->begin(thread_id, coro_id, 7);
      int cnt = (FLAGS_block_size + BLOCK_SIZE - 1) / BLOCK_SIZE; 
      ret_size = nrfsRead(sink, ctx, cnt);
      reporter->many_end(ret_size, thread_id, coro_id, 7);
      break;
    }
    case kEnumW: {
      reporter->begin(thread_id, coro_id, 8);
      int cnt = (FLAGS_block_size + BLOCK_SIZE - 1) / BLOCK_SIZE; 
      ret_size = nrfsWrite(sink, ctx, cnt);
      reporter->many_end(ret_size, thread_id, coro_id, 8);
      break;
    }
    default:
      exit(0);
      break;
    }
    // usleep(100000);
  }

  // cpc
  void client_prepare() {
    my_index_bitmap |= (1 << server_id);
    mehcached_zipf_init(&op_state, 100, 0, thread_id);
    mehcached_zipf_init(&state, FLAGS_keyspace - 1, FLAGS_zipf,
                        thread_id + rand() % 1000);

    memset(index_buf_cnt, 0, sizeof(index_buf_cnt));

    reporter = perf::PerfTool::get_instance_ptr();
    reporter->worker_init(thread_id);
    userbuf = (uint8_t *)malloc(FLAGS_kv_size_b * 2);

    for (int i = 0; i < rpc::FLAGS_coro_num; i++) {
      std::string tmps = "/s" + std::to_string(server_id) + "/t" +
                         std::to_string(thread_id) + "/c" + std::to_string(i) + "/n";
      coro_ctx[i].app_ctx = &fs_ctx[i];
      fs_ctx[i].coro_id = i;
      fs_ctx[i].value = (uint8_t *)malloc(FLAGS_kv_size_b);
      fs_ctx[i].prefix = tmps;
      fs_ctx[i].offset = 0;
      fs_ctx[i].count = 0;
    }
  }

  int8_t client_get_cid(void *reply_addr) {
    auto reply = (RawVMsg *)reply_addr;
    return reply->rpc_header.src_c_id; // normal req
  }

  void after_process_reply(void *reply, uint8_t c_id) {
    if (FLAGS_visibility) {
      // printf("ret = %x %d\n", ((RawKvMsg*)reply)->send_cnt,
      // rdma::toBigEndian16(((RawKvMsg*)reply)->index_port)); sleep(1);
      index_buf_cnt[(uint16_t)log2(
          rdma::toBigEndian16(((RawVMsg *)reply)->index_port))] =
          (rdma::toBigEndian16(((RawVMsg *)reply)->send_cnt));
    }
  }

  void check_req_cnt(CoroMaster &sink, uint8_t coro_id) {
    RawVMsg *msg, *reply;
    rdma::QPInfo *qp = rpc_get_qp(1, 0, 0, 0);
    msg = (RawVMsg *)qp->get_send_msg_addr();
    qp->modify_smsg_dst_port(client->get_thread_port(thread_id, 0));
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
        get_index_thread_id(get_switch_key(fs_ctx[coro_id].hashUnique.value[3]));
    msg->index_port = rdma::toBigEndian16(client->get_thread_port(index_tid, 0));
    qp->modify_smsg_size(msg->get_size(kEnumReadSend));
    qp->append_signal_smsg();
    qp->post_appended_smsg(&sink);
    reply = (RawVMsg *)coro_ctx[coro_id].msg_ptr;
    if (reply->rpc_header.op != kEnumReadSend) {
      exit(-1);
    }
    // printf("cnt=%d update!\n", rdma::toBigEndian16(reply->send_cnt));
  }

  void stack_mkdir(char * path) {
    char *parent = (char *)malloc(255 + 1);
    char *tmp = (char *)malloc(255 + 1);
    fs_server->fs->getParentDirectory(path, parent);
    fs_server->fs->getParentDirectory(parent, tmp);

    // LOG(INFO) <<"?"<< parent;
    if (!fs_server->fs->access(tmp)) {
      stack_mkdir(parent);
    }
    // LOG(INFO) << "!" << parent;
    fs_server->fs->mkdircd(parent);
    free(parent);
    free(tmp);
  }

public:
  FsRpc(/* args */) {}
  ~FsRpc() {}
  sampling::StoRandomDistribution<> *sample; /*Fast*/
  void server_batch_CPC() {
    init();
    server_prepare();
    connect();

    uint32_t max_size = FLAGS_batch_size + rdma::CQInfo::kMaxWcCnt;
    uint32_t batch_cnt = 0;

    struct batchEntry {
      rdma::QPInfo *qp;
      RawVMsg *reply_addr;
      bool operator<(const batchEntry y) const {
        if (*(uint64_t *)(reply_addr)->get_key_addr() ==
            *(uint64_t *)y.reply_addr->get_key_addr()) {
          return reply_addr->timestamp > y.reply_addr->timestamp;
        }
        return (*(uint64_t *)reply_addr->get_key_addr() <
                *(uint64_t *)y.reply_addr->get_key_addr());
      }
    };
    auto batch = new batchEntry[max_size];
    bool onpath = false;

    while (true) {

      ibv_wc *wc = nullptr;
      onpath = false;

      auto wc_array = client->cq->poll_cq_try(rdma::CQInfo::kMaxWcCnt);
      while ((wc = wc_array->get_next_wc())) {
        auto qp = ((rdma::WrInfo *)wc->wr_id)->qp;
        rdma::QPInfo *ret_qp = nullptr;
        auto req = qp->get_recv_msg_addr();
        auto &req_header = *(RawVMsg *)req;
        void *reply = nullptr;
        RawVMsg *reply_header = nullptr;

        uint32_t msg_size = 0;
        reporter->end_copy(thread_id, 0, 0);

        // get appropriate qp
        switch (req_header.rpc_header.op) {
        case kEnumReadIndexReq: { // visibility
          reporter->end_copy(thread_id, 0, 1);
          // sleep(1); 
          // read meta
          onpath = true;
          msg_size = sizeof(MetaReadReply);

          ret_qp = qp;
          
          reply = ret_qp->get_send_msg_addr();
          memcpy(reply, req, sizeof(RawVMsg)); // TODO

          reply_header = (RawVMsg* ) reply;
          reply_header->rpc_header.op = kEnumReadIndexReply;

          auto req_msg = (MetaReadReq *) req;
          auto reply_msg = (MetaReadReply *)reply;
          
          if (req_msg->meta_op == kEnumFsCreate || req_msg->meta_op == kEnumFsMetaRead) { // open
            // create
            bool exist = false;
            if ((exist = fs_server->fs->access(req_msg->path)) == false) {
              exist = fs_server->fs->mknodcd(req_msg->path);
            }

            if (!exist) {
              stack_mkdir(req_msg->path);
              exist = fs_server->fs->mknodcd(req_msg->path);
            }
            fs_server->fs->getattr(req_msg->path, &reply_msg->attribute);
            Debug::debugItem("count = %d size = %d\n", reply_msg->attribute.count, reply_msg->attribute.size);
          }

          break;
        }
        case kEnumMultiCastPkt: {
          reporter->end_copy(thread_id, 0, 3);
          msg_size = sizeof(GeneralReceiveBuffer); // reply real size
          uint32_t msg_size_cpy = sizeof(MetaUpdateReq); // copy req to reply for batching;
          ret_qp = qp;

          reply = ret_qp->get_send_msg_addr();
          memcpy(reply, req, msg_size_cpy); // TODO


          reply_header = (RawVMsg *)reply;
          reply_header->log_id = 0;
          reply_header->rpc_header.op = ReplySwitch; 
          break;
        }

        case kEnumUpdateIndexReq: {
          reporter->end_copy(thread_id, 0, 2);

          onpath = true;
          msg_size = sizeof(GeneralReceiveBuffer); // reply real size
          // uint32_t msg_size_cpy = sizeof(MetaUpdateReq); // copy req to reply for batching;

          ret_qp = qp;
          reply = ret_qp->get_send_msg_addr();
          memcpy(reply, req, sizeof(RawVMsg)); 

          MetaUpdateReq* mur = (MetaUpdateReq*) req;
          fs_server->fs->extentMetaWrite(mur->path, mur->size, mur->offset,  &(mur->fei));

          reply_header = (RawVMsg *)reply;
          reply_header->log_id = 0;
          reply_header->rpc_header.op = kEnumUpdateIndexReply;
          break;
        }

        case kEnumReadLog: { // fs: read data

          onpath = true;
          
          ret_qp = rpc_get_qp(rdma::LOGRC, req_header.rpc_header.src_s_id,
                              req_header.rpc_header.src_t_id, 0);
          reply = ret_qp->get_send_msg_addr();
          memcpy(reply, req, sizeof(RawVMsg)); 
          // TODO:
          reply_header = (RawVMsg *)reply;
          reply_header->rpc_header.op = kEnumReadReply;

          file_pos_info fpi;

          auto req_msg = (DataReadReq *) req;
          auto reply_msg = (DataReadReply *) reply;

          reply_msg->size = req_msg->size;
          msg_size = reply_msg->get_size();

          #ifdef LOGDEBUG 
            LOG(INFO) << "path: "<< req_msg->path <<", count=" << req_msg->fei.len;
            sleep (1);
          #endif

          // malloc data
          reporter->many_end_copy(req_msg->size, thread_id, 0, 1);
          fs_server->fs->extentRead(req_msg->path, req_msg->size, req_msg->offset,  &(req_msg->fei), &fpi); 

          uint64_t offset = 0;
          for (uint i = 0; i < fpi.len; i++){
            memcpy(reply_msg->data_buffer() + offset,(char *)fs_server->mem->getDataAddress() + fpi.tuple[i].offset, fpi.tuple[i].size);
            offset += fpi.tuple[i].size;
          }
          break;
        }

        case kEnumAppendReq: { // fs: write data
          

          onpath = true;
          msg_size = sizeof(MetaUpdateReq);

          auto req_msg = (DataBuffer *) req;
          
          ret_qp = rpc_get_qp(rdma::LOGRAW, req_header.rpc_header.src_s_id,
                              req_header.rpc_header.src_t_id, 0);
          reply = ret_qp->get_send_msg_addr();

          auto reply_msg = (MetaUpdateReq *) reply;
          memcpy(reply, req, sizeof(MetaUpdateReq)); // TODO

          reply_header = (RawVMsg *)reply;
          reply_header->rpc_header.op = kEnumAppendReply;
          reply_header->log_id = UINT64_MAX; 

          do {
            reply_header->timestamp = ts_generator.get_next_ts(req_header.switch_key);
            // printf("%u\n",reply->timestamp);
          } while (reply_header->timestamp == 0);

              
          file_pos_info fpi;
          file_extent_info *fei_in = &req_msg->fei;
          file_extent_info *fei_out = &reply_msg->fei;

          #ifdef LOGDEBUG 
            LOG(INFO) << "path: "<< req_msg->path <<", size=" << req_msg->size <<", offset=" << req_msg->offset;
            sleep (1);
          #endif

          // malloc data!

          reporter->many_end_copy(req_msg->size, thread_id, 0, 2);

          fs_server->fs->extentWrite(req_msg->path, req_msg->size, req_msg->offset, &fpi, fei_in, fei_out, fs_server->mem->getDataAddress()); 
          // write data!
          uint64_t offset = 0;
          for (uint i = 0; i < fpi.len; i++){
            memcpy((char *)fs_server->mem->getDataAddress() + fpi.tuple[i].offset, req_msg->data_buffer() + offset, fpi.tuple[i].size);
            offset += fpi.tuple[i].size;
          }
          break;
        }
        default:
          puts("exit");
          exit(-1);
          break;
        }

#ifdef LOGDEBUG
        LOG(INFO) << "new op: " << (int)req_header.rpc_header.op;
#endif

        ret_qp->modify_smsg_size(msg_size);
        ret_qp->modify_smsg_dst_port(
            client->get_thread_port(req_header.rpc_header.src_t_id, qp->qp_id));
        reply_header->rpc_header.dst_s_id = req_header.rpc_header.src_s_id;
        // reply_header.rpc_header.src_s_id = server_id;

        qp->free_recv_msg();
        ret_qp->append_signal_smsg();

        // qp->append_signal_smsg();
        batch[batch_cnt].qp = ret_qp;
        batch[batch_cnt++].reply_addr = (RawVMsg *)reply;
      }

      if (batch_cnt < FLAGS_batch_size && !onpath &&
          client->server.server_type == 1 && FLAGS_batch) {
        continue;
      }

      if (client->server.server_type == 0 && FLAGS_batch)
        std::sort(batch, batch + batch_cnt);

      uint64_t last_key = 0;

      // batch index msg
      for (uint i = 0; i < batch_cnt; i++) {

        auto reply = (RawVMsg *)batch[i].reply_addr;

        reply->send_cnt = 0;

        uint64_t return_value;

#ifdef LOGDEBUG
        LOG(INFO) << "return op:" << (uint64_t)reply->rpc_header.op << ", coro_id:" << (uint64_t)reply->rpc_header.src_c_id << ", server_id:" << (uint64_t)reply->rpc_header.dst_s_id;
#endif

        switch (reply->rpc_header.op) {
        case kEnumReadIndexReply: // visibility
          return_value = 0;
          // if (!(index->get_long(*(KvKeyType *)reply->get_key_addr(),
          // 											&(return_value))))
          // { 	reply->key = 0xFFFFFFFF;
          // }
          reply->log_id = (uint32_t)return_value;
          break;
        case ReplySwitch: {
          if (*(KvKeyType *)reply->get_key_addr() != last_key) {
          }

          MetaUpdateReq* mur = (MetaUpdateReq*) reply;
          fs_server->fs->extentMetaWrite(mur->path, mur->size, mur->offset, &(mur->fei));
          // index->put_long(*(KvKeyType *)reply->get_key_addr(),
          // reply->log_id);
          break;
        }
        case kEnumUpdateIndexReply:
          break;
        default:

          break;
        }
        last_key = *(KvKeyType *)reply->get_key_addr();

        reply->route_port = rdma::toBigEndian16(
            server_config[reply->rpc_header.dst_s_id].port_id);
        reply->qp_map = 0;
        reply->server_map = 0;
      }

      for (uint i = 0; i < batch_cnt; i++) {
        batch[i].qp->post_appended_smsg();
      }
      batch_cnt = 0;

      // for (int i = 0; i < rpc::kConfigMaxQPCnt; i++) {
      //   client->get_raw_qp(i).post_appended_smsg();
      // }
    }
  }
};
