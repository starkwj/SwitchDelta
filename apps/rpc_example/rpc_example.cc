#include "operation.h"
#include "qp_info.h"
#include "rdma.h"
#include "rpc.h"
#include "size_def.h"
#include "timer.h"
#include "zipf.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <gperftools/profiler.h>
#include <infiniband/verbs_exp.h>

// DEFINE_int32(client_thread, 1, "client_thread");
// DEFINE_int32(server_thread, 1, "server_thread");
// DEFINE_int32(client_cnt, 1, "client_cnt");
// DEFINE_int64(rpc_batch, 1, "target throughput (Kops/s)");

// DEFINE_int32(client_thread, 47, "client_thread");
// DEFINE_int32(server_thread, 16, "server_thread");
// DEFINE_int32(client_cnt, 3, "client_cnt");
// DEFINE_int64(rpc_batch, 4, "target throughput (Kops/s)");

DEFINE_int32(client_thread, 8, "client_thread");
DEFINE_int32(server_thread, 1, "server_thread");
DEFINE_int32(client_cnt, 3, "client_cnt");
DEFINE_int64(rpc_batch, 2, "target throughput (Kops/s)");

DEFINE_int32(rpc_qp, 0, "0:RC 1:Raw 2:UD 3:TSRQ 4:GSRQ 5:DCT");
DEFINE_int64(target_tp, 1000, "target throughput (Kops/s)");

struct RawKvMsg {
  uint8_t src_s_id;
  uint8_t src_t_id;
  bool hw_resp;
  bool is_open_loop;
  uint8_t pad[60];
} __attribute__((packed));
// static_assert(sizeof(RawKvMsg) == 64, "XXX");

#define kLogNumOfStrides 10
#define kLogStrideSize 6

struct OrderedWQ{
  const uint32_t kWrSize = 1 << kLogNumOfStrides;
  const uint32_t kStrideSize = 1 << kLogStrideSize;
  const uint32_t kStrideNum = kWrSize * kStrideSize;
  uint64_t head;
  uint64_t post_head;
  // rdma::SrqConfig srq_config;
  uint64_t bitmap[POSTPIPE * (1<<(kLogNumOfStrides-6))];
  uint32_t stride_cnt[POSTPIPE * (1<<kLogNumOfStrides)];
  uint32_t byte_len[POSTPIPE * (1<<kLogNumOfStrides)];
  uint32_t free_cnt[POSTPIPE];
  char* base;


  ibv_cq* cq;
  rdma::QPInfo* srq;

  void init(ibv_cq* cq_, rdma::QPInfo* srq_) {
    cq = cq_;
    srq = srq_;
    base = (char *)(srq_->rpc_recv_wr[0]->get_addr());
    head = 0;
    post_head = 0;

    memset(bitmap, 0, sizeof(bitmap));
    memset(stride_cnt, 0, sizeof(stride_cnt));
    memset(byte_len, 0, sizeof(byte_len));
    memset(free_cnt, 0, sizeof(free_cnt));
  }

  bool msg_ok(uint32_t pos) {
    return (bitmap[pos / 64] & (1ull << (pos % 64))) != 0;
  }
  
  char* change_pos_to_addr(uint32_t pos) {
    return (base + pos * kStrideSize);
  }

  std::pair<char*, uint32_t> get_request() {
    ibv_exp_wc wc;

    while (msg_ok(head) == false) {
      // poll
      int ret = ibv_exp_poll_cq(cq, 1, &wc, sizeof(wc));
      if (ret == 0) {
        continue;
      }

      // sleep(1);
      auto wr_info = (rdma::WrInfo*)(wc.wr_id);
      auto pos = wr_info->b_id * kWrSize + wc.mp_wr.strides_offset;
      // printf("wc.exp_opcode %d %d, b_id = %d, pos=%d\n", wc.exp_opcode, IBV_EXP_WC_RECV_NOP, wr_info->b_id, pos);
      
      if (wc.exp_opcode == IBV_EXP_WC_TM_RECV || wc.exp_opcode == IBV_EXP_WC_TM_NO_TAG) {
        byte_len[pos] = wc.mp_wr.byte_len;
        bitmap[pos / 64] |= (1ull << (pos % 64));
        stride_cnt[pos] = ALIGNMENT_XB(wc.mp_wr.byte_len, kStrideSize) / kStrideSize;
      }
      else if (wc.exp_opcode == IBV_EXP_WC_RECV_NOP) {
        if (head == stride_cnt[pos] + pos) {
          head = ALIGNMENT_XB(head, kWrSize) % kStrideNum;
          free_cnt[wr_info->b_id] += stride_cnt[pos];
        }
        stride_cnt[pos] = kWrSize - pos;
      }
    
    }
    
    auto ret = std::make_pair(change_pos_to_addr(head), byte_len[head]);

    bitmap[head / 64] &= ~(1ull << (head % 64));
    free_cnt[head / kWrSize] += stride_cnt[head];
    ADD_ROUND_M(head, kStrideNum, stride_cnt[head]);
    return ret;
  }

  void re_post() {
    if (free_cnt[post_head] != kWrSize) return;
    srq->rpc_recv_wr[post_head]->post_batch();
    free_cnt[post_head] = 0;
    ADD_ROUND(post_head, POSTPIPE);
    // printf("post %d\n", post_head);
    return;
  }
};

rdma::GlobalConfig<rpc::kConfigServerCnt> global_config;
std::vector<rdma::ServerConfig> machine_config;
void config() {
  machine_config.clear();
  rdma::ServerConfig item;

  global_config.rc_msg_size = sizeof(RawKvMsg); // for rc qp without srq  
  global_config.srq_config.mp_flag = true;
  global_config.srq_config.normal_srq_len = 2048;
  global_config.srq_config.log_stride_size = kLogStrideSize; // 64B;
  global_config.srq_config.log_num_of_strides = kLogNumOfStrides; // 1024 * 64
  // global_config.num_of_stride_groups = 4;

  global_config.ud_msg_size = sizeof(RawKvMsg);
  global_config.raw_msg_size = sizeof(RawKvMsg);
  global_config.one_sided_rw_size = 4;
  global_config.link_type = rdma::kEnumRoCE;
  global_config.func = (rdma::ConnectType::T_LOCAL_SRQ) | (rdma::ConnectType::RC);

  // global_config.func = (rdma::ConnectType::RC) |
  //                      (rdma::ConnectType::T_LOCAL_SRQ) |
  //                      (rdma::ConnectType::UD);

  if (global_config.link_type == rdma::kEnumIB) {
    global_config.func &= (rdma::ConnectType::ALL) - (rdma::ConnectType::RAW);
  }

  machine_config.push_back(item = {
                               .server_type = 0,
                               .thread_num = FLAGS_server_thread,
                               .numa_id = 0,
                               .numa_size = 12,
                               .dev_id = 0,
                               .nic_name = "ens2", // for raw packet
                               .port_id = 128,     // for raw packet
                               .srq_receiver = 1,
                               .srq_sender = 0,
                           });


  for (int i = 0; i < FLAGS_client_cnt; ++i) {
    machine_config.push_back(item = {
                                 .server_type = 1,
                                 .thread_num = FLAGS_client_thread,
                                 .numa_id = 0,
                                 .numa_size = 12,
                                 .dev_id = 0,
                                 .nic_name = "ens6", // for raw packet
                                 .port_id = 188,     // for raw packet
                                 .srq_receiver = 0,
                                 .srq_sender = 1,
                             });
  }

  if (FLAGS_client_cnt == 4) {
    machine_config[4].dev_id = 2;
  }

  for (int i = 0; i < rpc::kConfigServerCnt; i++) {
    for (int j = 0; j < rpc::kConfigServerCnt; j++) {
      if (i != 0 && j != 0) {
        continue;
      }
      global_config.matrix[i][j] = rdma::TopoType::All;
      global_config.matrix_TSRQ[i][j] = rdma::TopoType::All;
      // global_config.matrix_GSRQ[i][j] = rdma::TopoType::All;
    }
  }
  assert(machine_config.size() <= rpc::kConfigServerCnt);
}

class SimpleRpc {
public:
  rdma::Rdma<rpc::kConfigServerCnt, rpc::kConfigMaxThreadCnt,
             rpc::kConfigMaxQPCnt> *agent;

  perf::PerfTool *reporter;
  uint8_t node_id;
  uint8_t thread_id;
  char *buffer;

  void ordered_server() {
    OrderedWQ wq;
    wq.init(agent->cq->cq, agent->tsrq->srq);
    while (true) {
      reporter->begin(thread_id, 0, 0);
      auto req_pair = (wq.get_request());
      auto req = (RawKvMsg *)req_pair.first;
      
      // if (!req->hw_resp && !req->is_open_loop) {
        rdma::QPInfo *send_qp =
            rpc_get_qp(rdma::LOGTSRQ, req->src_s_id, req->src_t_id, 0);

        auto reply = (RawKvMsg *)send_qp->get_send_msg_addr();

        (void)reply;
        send_qp->modify_smsg_size(sizeof(RawKvMsg));
        send_qp->append_signal_smsg();
        send_qp->post_appended_smsg();
      // }
      
      wq.re_post();
      reporter->end(thread_id, 0, 0);
    }

  }

  void run_server() {

    init();
    server_prepare();
    connect();

    // ordered_server();

    while (true) {


      reporter->begin(thread_id, 0, 0);

      struct ibv_wc* wc;

      // auto wc_array = agent->cq->poll_cq_try(16);
      // while ((wc = wc_array->get_next_wc())) {
      wc = agent->cq->poll_cq_a_wc();
      if (wc){

        auto wc_info = ((rdma::WrInfo *)wc->wr_id);
        auto recv_qp = wc_info->qp;

        auto req = (RawKvMsg *)recv_qp->get_recv_msg_addr(wc);

        rdma::QPInfo *send_qp =
            rpc_get_qp(recv_qp->type, req->src_s_id, req->src_t_id, 0);
        // rdma::QPInfo *send_qp = &agent->get_ud_qp(0);
        // agent->modify_ud_ah(req->src_s_id, req->src_t_id, 0);


        auto reply = (RawKvMsg *)send_qp->get_send_msg_addr();

        (void)reply;
        send_qp->modify_smsg_size(sizeof(RawKvMsg));
        send_qp->append_signal_smsg();
        // send_qp->add_smsg_flag(IBV_SEND_INLINE);
        send_qp->post_appended_smsg();
      
        recv_qp->free_recv_msg(wc);
        reporter->end(thread_id, 0, 0);
      }

      // agent->get_ud_qp(0).post_appended_smsg();
    }
  }

  void run_client_closed_loop() {

    init();
    client_prepare();
    connect();

    unsigned int random_seed = thread_id;

    size_t batchSize = FLAGS_rpc_batch;
    for (size_t k = 0; k < batchSize; ++k) {
      rpc_send_an_request(&random_seed, false);
    }

    while (true) {

      
      wait_reply();
      reporter->end(thread_id, 0, 0);
      reporter->begin(thread_id, 0, 0);
      rpc_send_an_request(&random_seed, false);
      
      // int reply_type = rpc_send_an_request(&random_seed, false);
      // (reply_type == kHWAck) ? wait_hw_ack() : wait_reply();

      // uint64_t ret = reporter->end(thread_id, 0, 0);
    }
    return;
  }

  void run_client_open_loop() {
    init();
    client_prepare();
    connect();

    unsigned int random_seed = thread_id;

    uint64_t ops_per_thread =
        FLAGS_target_tp * 1000 / (FLAGS_client_cnt * FLAGS_client_thread - 1);

    uint64_t ns_per_op = (1ull * 1000 * 1000 * 1000) / ops_per_thread;
    printf("%ld ns for a request\n", ns_per_op);

    uint64_t start_time = perf::Timer::get_time_ns();

    while (true) {
      auto cur_time = perf::Timer::get_time_ns();
      if (cur_time - start_time >= ns_per_op) {
        start_time = cur_time;
        int reply_type = rpc_send_an_request(&random_seed, true);
        (void)reply_type;
      }
    }
    return;
  }

private:
  rdma::CoroCtx signal_ctx;

  const int kHWAck = 1;
  const int kSWAck = 2;

  int rpc_send_an_request(unsigned int *random_seed, bool is_open_loop) {

    rdma::QPInfo *qp = rpc_get_qp(
      FLAGS_rpc_qp,
      0,
        // (thread_id % FLAGS_server_thread) ? 0 : 3, 0,
        // FLAGS_rpc_qp, 0, /* remote server id */
        // thread_id % FLAGS_server_thread,
        rand_r(random_seed) % FLAGS_server_thread, /* remote thread id */
        0 /* qp_id in [0, rpc::kConfigMaxQPCnt - 1] */);
    RawKvMsg *msg = (RawKvMsg *)qp->get_send_msg_addr();
    if ((uint64_t)msg % 64 != 0) {
      printf("align error\n");
      exit(-1);
    }

    (*msg) = {
        .src_s_id = node_id,
        .src_t_id = thread_id,
        .hw_resp = false, 
    };

    qp->modify_smsg_size(sizeof(RawKvMsg));
    qp->append_signal_smsg();
    // qp->add_smsg_flag(IBV_SEND_INLINE);

    qp->post_appended_smsg(nullptr);

    return msg->hw_resp ? kHWAck : kSWAck; /* for reply */
  }

  bool wait_hw_ack() {
    while (!signal_ctx.over())
      agent->send_cq->poll_cq_a_wc();
    signal_ctx.init();
    return true;
  }

  bool wait_reply() {

    auto wc = agent->cq->poll_cq_a_wc();
    auto qp = ((rdma::WrInfo *)wc->wr_id)->qp;
    void *reply = qp->get_recv_msg_addr();
    (void)reply; /*process */
    qp->free_recv_msg();

    return true;
  }

  void server_prepare() {
    reporter = perf::PerfTool::get_instance_ptr();
    reporter->worker_init(thread_id);
  }

  void client_prepare() {
    reporter = perf::PerfTool::get_instance_ptr();
    reporter->worker_init(thread_id);
  }

  void init() {
    // puts("init");
    node_id = agent->get_server_id();
    thread_id = agent->get_thread_id();
    BindCore(agent->server.get_core_id(thread_id));
  }
  void connect() {
    // BindCore(agent->server.numa_id * 12);
    uint32_t mm_size = agent->global_config->one_sided_rw_size * MB;
    void *mm_addr = malloc(mm_size);
    buffer = (char *)mm_addr;
    agent->config_rdma_region(0, (uint64_t)mm_addr, mm_size);
    // puts("?xxxx");

    uint32_t recv_size = agent->get_recv_buf_size();
    void *recv_addr = malloc(recv_size);
    // recv_addr = (void *)(((uint64_t)recv_addr + 63) & (~63ull));
    int ret = posix_memalign(&recv_addr, 64, recv_size);
    if ((uint64_t)recv_addr % 64 != 0 || ret != 0) {
      printf("XXXX\n");
      *(int*)0 = 0;
    }
    agent->config_recv_region((uint64_t)recv_addr, recv_size);
    // puts("?xxxx");
    agent->connect();

    printf("all recv buffer size: %d bytes\n", recv_size);

    uint32_t send_size = agent->get_send_buf_size();
    void *send_addr = malloc(send_size);
    // send_addr = (void *)(((uint64_t)send_addr + 63) & (~63ull));
    ret = posix_memalign(&send_addr, 64, recv_size);
    if ((uint64_t)send_addr % 64 != 0 || ret != 0) {
      printf("XXXX\n");
      *(int*)0 = 0;
    }
    agent->config_send_region((uint64_t)send_addr, send_size);

    printf("all send buffer size: %d bytes\n", send_size);

    if (reporter != nullptr)
      reporter->thread_begin(thread_id);
  }

  inline rdma::QPInfo *rpc_get_qp(uint8_t type, uint8_t node_id,
                                  uint8_t thread_id, uint8_t qp_id) {
    rdma::QPInfo *qp;
    switch (type) {
    case (rdma::LOGRC): {
      qp = &agent->get_rc_qp(node_id, thread_id, qp_id);
      return qp;
    }
    case (rdma::LOGUD): {
      qp = &agent->get_ud_qp(qp_id);
      agent->modify_ud_ah(node_id, thread_id, qp_id);
      return qp;
    }
    case (rdma::LOGTSRQ): {
      qp = &agent->get_thread_srq_qp(node_id, thread_id, qp_id);
      return qp;
    }
    }
    return nullptr;
  }
};

perf::PerfConfig perf_config = {
    .thread_cnt = rpc::kConfigMaxThreadCnt,
    .coro_cnt = rpc::kConfigCoroCnt,
    .type_cnt = 3,
    .slowest_latency = 10,
};

int main(int argc, char **argv) {

  FLAGS_logtostderr = 1;
  gflags::SetUsageMessage("Usage ./rdma_clean_client --help");
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  /* config the node info & memcached */
  config();
  Memcached::initMemcached(rpc::FLAGS_node_id);

  /* init threads */
  int thread_num = machine_config[rpc::FLAGS_node_id].thread_num;
  int server_type = machine_config[rpc::FLAGS_node_id].server_type;
  std::thread **th = new std::thread *[thread_num];
  SimpleRpc *worker_list = new SimpleRpc[thread_num];
  for (int i = 0; i < thread_num; i++) {
    worker_list[i].agent =
        new rdma::Rdma<rpc::kConfigServerCnt, rpc::kConfigMaxThreadCnt,
                       rpc::kConfigMaxQPCnt>(machine_config, &global_config,
                                             rpc::FLAGS_node_id, i);
  }

  /* init the perf tool */
  perf_config.thread_cnt = thread_num;
  perf::PerfTool *reporter = perf::PerfTool::get_instance_ptr(&perf_config);
  if (server_type == 0) {
    reporter->new_type("server");
  } else if (server_type == 1) {
    reporter->new_type("client");
    reporter->new_type("hw");
    reporter->new_type("sw");
  }
  reporter->master_thread_init();

  /* run threads in my node */
  for (int i = 0; i < thread_num; i++) {
    if (server_type == 0) { // server
      th[i] =
          new std::thread(std::bind(&SimpleRpc::run_server, &worker_list[i]));
    } else {                                   // client
      if (i == 0 && rpc::FLAGS_node_id == 1) { // closed_loop
        th[i] = new std::thread(
            std::bind(&SimpleRpc::run_client_closed_loop, &worker_list[i]));
      } else {
        th[i] = new std::thread(
            std::bind(&SimpleRpc::run_client_closed_loop, &worker_list[i]));
      }
    }
  }

  /* main thread keeps merging and showing performance */
  int cnt = 0;
  while (cnt < 300) {
    reporter->try_wait();
    std::string name = "gperf" + std::to_string(rpc::FLAGS_node_id) + "gperf";
    ProfilerStart(name.c_str());
    reporter->try_print(perf_config.type_cnt);
    cnt++;
  }

  ProfilerStop();

  for (int i = 0; i < thread_num; i++)
    th[i]->join();

  return 0;
}
// node0 (server, please run node-0 first): ./rpc_s2 --node_id=0
// node1 (client): ./rpc_s2 --node_id=1 --qp_type=3