#include "common_config.h"
#include "juneberry_rpc_base.h"
#include "request_handler_base.h"

extern "C" {
void init_embedded_memcached(size_t max_bytes, int num_threads,
                             uint8_t hash_power);
void embedded_put(char *key, int nkey, char *value, int vlen, int thread_id);
bool embedded_get(char *key, int nkey, char *value, int *vlen, int thread_id);
}

using namespace juneberry;

constexpr size_t kMpSrqLength = POSTPIPE;

struct OrderedWQ {
  const uint32_t kWrSize = 1 << kLogNumOfStrides;
  const uint32_t kStrideSize = 1 << kLogStrideSize;
  const uint32_t kStrideNum = kWrSize * kStrideSize;
  uint64_t head;
  uint64_t post_head;

  uint64_t bitmap[kMpSrqLength * (1 << (kLogNumOfStrides - 6))];
  uint32_t stride_cnt[kMpSrqLength * (1 << kLogNumOfStrides)];
  uint32_t byte_len[kMpSrqLength * (1 << kLogNumOfStrides)];
  uint32_t free_cnt[kMpSrqLength];
  bool free_flag[kMpSrqLength];
  char *base;

  ibv_cq *cq;
  rdma::QPInfo *srq;

  void init(ibv_cq *cq_, rdma::QPInfo *srq_) {
    cq = cq_;
    srq = srq_;
    base = (char *)(srq_->rpc_recv_wr[0]->get_addr());
    head = 0;
    post_head = 0;

    memset(bitmap, 0, sizeof(bitmap));
    memset(stride_cnt, 0, sizeof(stride_cnt));
    memset(byte_len, 0, sizeof(byte_len));
    memset(free_cnt, 0, sizeof(free_cnt));
    memset(free_flag, 0, sizeof(free_flag));
  }

  bool msg_ok(uint32_t pos) {
    return (bitmap[pos / 64] & (1ull << (pos % 64))) != 0;
  }

  char *change_pos_to_addr(uint32_t pos) { return (base + pos * kStrideSize); }

  std::pair<char *, uint32_t> get_request() {
    ibv_exp_wc wc;

    while (msg_ok(head) == false) {
      // poll
      int ret = ibv_exp_poll_cq(cq, 1, &wc, sizeof(wc));
      if (ret == 0) {
        continue;
      }

      auto wr_info = (rdma::WrInfo *)(wc.wr_id);
      auto pos = wr_info->b_id * kWrSize + wc.mp_wr.strides_offset;

      if (wc.exp_opcode == IBV_EXP_WC_TM_RECV ||
          wc.exp_opcode == IBV_EXP_WC_TM_NO_TAG) {
        byte_len[pos] = wc.mp_wr.byte_len;
        bitmap[pos / 64] |= (1ull << (pos % 64));
        stride_cnt[pos] =
            ALIGNMENT_XB(wc.mp_wr.byte_len, kStrideSize) / kStrideSize;
        if (wc.exp_wc_flags & IBV_EXP_WC_MP_WR_CONSUMED) {
          free_flag[wr_info->b_id] = true;
          stride_cnt[pos] = kWrSize - (pos % kWrSize);
        }
      } else if (wc.exp_opcode == IBV_EXP_WC_RECV_NOP) {
        free_flag[wr_info->b_id] = true;
        if (head / kWrSize == (uint64_t)wr_info->b_id) {
          head = ALIGNMENT_XB(head, kWrSize) % kStrideNum;
        }
      }
    }

    auto ret = std::make_pair(change_pos_to_addr(head), byte_len[head]);

    bitmap[head / 64] &= ~(1ull << (head % 64));
    ADD_ROUND_M(head, kStrideNum, stride_cnt[head]);
    return ret;
  }

  void re_post() {
    if (free_flag[post_head] == false) {
      return;
    }

    srq->rpc_recv_wr[post_head]->post_batch();
    for (uint i = 0; i < kWrSize / 64; i++) {
      bitmap[post_head * kWrSize / 64 + i] = 0;
    }
    free_cnt[post_head] = 0;
    free_flag[post_head] = false;
    ADD_ROUND(post_head, kMpSrqLength);
  }
};

class JuneberryRPCServer : public JuneberryRPCBase {
public:
  void ordered_server() {

    OrderedWQ wq;
    wq.init(agent->cq.cq, agent->tsrq->srq);

<<<<<<< HEAD
    request_handler = new MockRequestHandler(node_id, thread_id);

=======
    ExponentialDistribution dist(thread_id, 1500);
>>>>>>> 7702194... fix
    while (true) {
      auto req_pair = (wq.get_request());
<<<<<<< HEAD

      reporter->begin(thread_id, 0, 0);
      auto req = (Message *)req_pair.first;

      rdma::QPInfo *send_qp =
          &agent->get_thread_srq_qp(req->node_id, req->thread_id, 0);

#ifdef EMULATE_HW_ACK
      if (req->hw_resp && !req->is_open_loop) {
        auto reply = (Message *)send_qp->get_send_msg_addr();
        reply->clear();
        send_qp->modify_smsg_size(reply->get_size());
=======
      auto req = (Message *)req_pair.first;

#ifdef EMULATE_HW_ACK
      if (req->hw_flag && !req->is_open_loop) {
        rdma::QPInfo *send_qp =
            &agent->get_thread_srq_qp(req->src_s_id, req->src_t_id, 0);

        auto reply = (Message *)send_qp->get_send_msg_addr();
        (void)reply;
        send_qp->modify_smsg_size(sizeof(Message));
>>>>>>> 7702194... fix
        send_qp->append_signal_smsg();
        send_qp->post_appended_smsg();
      }
#endif

#ifndef NO_RESP_FOR_OPEN_LOOP
      req->is_open_loop = false;
#endif

<<<<<<< HEAD
      Message *reply = nullptr;
      bool need_sw_resp = !req->hw_resp && !req->is_open_loop;
      if (need_sw_resp) {
        reply = (Message *)send_qp->get_send_msg_addr();
        reply->clear();
      }

      request_handler->handle_request(req, reply);

      if (need_sw_resp) {
        send_qp->modify_smsg_size(reply->get_size());
=======
      if (!req->hw_flag && !req->is_open_loop) {
        rdma::QPInfo *send_qp =
            &agent->get_thread_srq_qp(req->src_s_id, req->src_t_id, 0);
        auto reply = (Message *)send_qp->get_send_msg_addr();
        (void)reply;
        send_qp->modify_smsg_size(sizeof(Message));
>>>>>>> 7702194... fix
        send_qp->append_signal_smsg();
        send_qp->post_appended_smsg();
      }

      wq.re_post();
      reporter->end(thread_id, 0, 0);
    }
  }

  void run_server() {
    init();
    connect();
    ordered_server();
  }

<<<<<<< HEAD
private:
  RequestHandlerBase *request_handler;
};

=======
>>>>>>> 7702194... fix
int main(int argc, char **argv) {

  FLAGS_logtostderr = 1;
  gflags::SetUsageMessage("Usage ./server --help");
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  rpc::FLAGS_node_id = 0;

  /* config the node info & memcached */
  config();
  Memcached::initMemcached(rpc::FLAGS_node_id);

  /* init threads */
  int thread_num = machine_config[rpc::FLAGS_node_id].thread_num;
  std::thread **th = new std::thread *[thread_num];
  auto *worker_list = new JuneberryRPCServer[thread_num];
  for (int i = 0; i < thread_num; i++) {
    worker_list[i].agent =
        new rdma::Rdma<rpc::kConfigServerCnt, rpc::kConfigMaxThreadCnt,
                       rpc::kConfigMaxQPCnt>(machine_config, &global_config,
                                             rpc::FLAGS_node_id, i);
  }

  perf_config.slowest_latency = 30;

  /* init the perf tool */
  perf_config.thread_cnt = thread_num;
  perf::PerfTool *reporter = perf::PerfTool::get_instance_ptr(&perf_config);
  reporter->new_type("all");
  reporter->master_thread_init();

  /* run threads in my node */
  for (int i = 0; i < thread_num; i++) {
    th[i] = new std::thread(
        std::bind(&JuneberryRPCServer::run_server, &worker_list[i]));
  }

  /* main thread keeps merging and showing performance */
  int cnt = 0;
  while (cnt < 300) {
    reporter->try_wait();
    reporter->try_print(perf_config.type_cnt);
    cnt++;
  }

  for (int i = 0; i < thread_num; i++)
    th[i]->join();

  return 0;
}