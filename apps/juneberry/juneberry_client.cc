#include "common_config.h"
#include "juneberry_rpc_base.h"
#include "request_generator_base.h"

using namespace juneberry;

DEFINE_int64(target_tp, 1000, "target throughput (Kops/s)");
DEFINE_int64(hw_resp, 20, "ratio of hardware response");

class JuneberryRPCClient : public JuneberryRPCBase {
public:
  void before_run() {
    init();
    connect();

<<<<<<< HEAD
    request_generator =
        new MockRequestGenerator(node_id, thread_id, FLAGS_server_thread);
  }

  void run_client_closed_loop() {

    before_run();
=======
    unsigned int random_seed = thread_id;
>>>>>>> 7702194... fix
    while (true) {
      reporter->begin(thread_id, 0, 0);

      int reply_type = rpc_send_one_request(false);
      (reply_type == kHWAck) ? wait_hw_ack() : wait_reply();

      uint64_t ret = reporter->end(thread_id, 0, 0);
      reporter->end_copy(thread_id, 0, (reply_type == kHWAck) ? 1 : 2, ret);
    }
    return;
  }

  void run_client_open_loop() {

    before_run();
    uint64_t ops_per_thread =
        FLAGS_target_tp * 1000 / (FLAGS_client_cnt * FLAGS_client_thread - 1);

    uint64_t ns_per_op = (1ull * 1000 * 1000 * 1000) / ops_per_thread;
    printf("%ld ns for a request\n", ns_per_op);

    uint64_t start_time = perf::Timer::get_time_ns();

    while (true) {
      auto cur_time = perf::Timer::get_time_ns();
      if (cur_time - start_time >= ns_per_op) {
        start_time = cur_time;
        int reply_type = rpc_send_one_request(true);

#ifndef NO_RESP_FOR_OPEN_LOOP
        (reply_type == kHWAck) ? wait_hw_ack() : wait_reply();
#endif
        (void)(reply_type);
      }
    }
    return;
  }

private:
  rdma::CoroCtx signal_ctx;

  const int kHWAck = 1;
  const int kSWAck = 2;

  RequestGeneratorBase *request_generator;

<<<<<<< HEAD
  int rpc_send_one_request(bool is_open_loop) {

    rdma::QPInfo *qp = &agent->get_thread_srq_qp(
        0, request_generator->next_target_thread_id(), 0);

    auto msg = (Message *)qp->get_send_msg_addr();
    msg->clear();
    msg->node_id = node_id;
    msg->thread_id = thread_id;
=======
    rdma::QPInfo *qp = &agent->get_thread_srq_qp(
        0, rand_r(random_seed) % FLAGS_server_thread, 0);
    auto msg = (Message *)qp->get_send_msg_addr();

    msg->src_s_id = node_id;
    msg->src_t_id = thread_id;
    msg->hw_flag = rand_r(random_seed) % 100 < FLAGS_hw_resp;
>>>>>>> 7702194... fix
    msg->is_open_loop = is_open_loop;

    request_generator->fill_request(msg);

#ifdef NO_RESP_FOR_OPEN_LOOP
    if (is_open_loop) {
      msg->hw_resp = false;
    }
#endif

<<<<<<< HEAD
    qp->modify_smsg_size(msg->get_size());
=======
    // fflush()
    // asm volatile("mfence" : : : "memory");
    qp->modify_smsg_size(sizeof(Message));
>>>>>>> 7702194... fix

#ifdef NO_RESP_FOR_OPEN_LOOP
    if (is_open_loop) { // no signal
      qp->append_signal_smsg();
    } else {
#ifdef EMULATE_HW_ACK
      qp->append_signal_smsg();
#else
      qp->append_signal_smsg(msg->hw_resp,
                             msg->hw_resp ? &signal_ctx : nullptr);
#endif
    }
#else
    qp->append_signal_smsg(msg->hw_resp, msg->hw_resp ? &signal_ctx : nullptr);
#endif

    qp->post_appended_smsg(nullptr);

    return msg->hw_resp ? kHWAck : kSWAck; /* for reply */
  }

  bool wait_hw_ack() {
#ifdef EMULATE_HW_ACK
    return wait_reply();
#else
    while (!signal_ctx.over())
      agent->send_cq.poll_cq_a_wc();
    signal_ctx.init();
    return true;
#endif
  }

  bool wait_reply() {

    auto wc = agent->cq.poll_cq_a_wc();
    auto qp = ((rdma::WrInfo *)wc->wr_id)->qp;
    void *reply = qp->get_recv_msg_addr();
    (void)reply; /*process */
    qp->free_recv_msg();

    return true;
  }
};

int main(int argc, char **argv) {

  FLAGS_logtostderr = 1;
  gflags::SetUsageMessage("Usage ./client --help");
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  /* config the node info & memcached */
  config();
  Memcached::initMemcached(rpc::FLAGS_node_id);

  /* init threads */
  int thread_num = machine_config[rpc::FLAGS_node_id].thread_num;
  std::thread **th = new std::thread *[thread_num];
  auto client_list = new JuneberryRPCClient[thread_num];
  for (int i = 0; i < thread_num; i++) {
    client_list[i].agent =
        new rdma::Rdma<rpc::kConfigServerCnt, rpc::kConfigMaxThreadCnt,
                       rpc::kConfigMaxQPCnt>(machine_config, &global_config,
                                             rpc::FLAGS_node_id, i);
  }

  perf_config.slowest_latency = 1000;

  /* init the perf tool */
  perf_config.thread_cnt = thread_num;
  perf::PerfTool *reporter = perf::PerfTool::get_instance_ptr(&perf_config);

  reporter->new_type("all");
  reporter->new_type("hw");
  reporter->new_type("sw");
  reporter->master_thread_init();

  for (int i = 0; i < thread_num; i++) {
    if (i == 0 && rpc::FLAGS_node_id == 1) { // closed_loop
      th[i] = new std::thread(std::bind(
          &JuneberryRPCClient::run_client_closed_loop, &client_list[i]));
    } else {
      th[i] = new std::thread(std::bind(
          &JuneberryRPCClient::run_client_open_loop, &client_list[i]));
    }
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