
#include "test.h"

DEFINE_int32(node_id, 0, "node id");
DEFINE_int32(core_id, 0, "core id");
DEFINE_double(test_size_kb, 0.25, "test size KB");
DEFINE_int32(mr_size_mb, 1, "mr size mb");
DEFINE_int32(dev_id, 0, "dev id");
DEFINE_string(nic_name, "ens2", "nic name");

rdma::RdmaCtx rdma_ctx;
rdma::RegionArr<kMaxLocalMRNum> local_mr;
rdma::ConnectGroup<kNodeNum, kMaxQPNum, kMaxRemoteMRNum> connects;
rdma::CQInfo cq;
rdma::CQInfo send_cq;
rdma::BatchWr<kBatchWRNum, 1> batch_wr[kMaxQPNum][2];



int main(int argc, char** argv) {

  FLAGS_logtostderr = 1;
  gflags::SetUsageMessage("Usage ./rdma_clean_client --help");
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);
  BindCore(FLAGS_core_id);

  // NIC Resources
  rdma_ctx.createCtx(FLAGS_dev_id, rdma::kEnumRoCE);
  cq.initCq(&rdma_ctx);
  send_cq.initCq(&rdma_ctx);

  for (int i = 0; i < kMaxLocalMRNum; i++) {
    auto addr = malloc(FLAGS_mr_size_mb * MB);
    local_mr.initRegion(&rdma_ctx, i, (uint64_t)addr, FLAGS_mr_size_mb * MB);
  }
  connects.setDefault(&rdma_ctx);
  connects.set_global_id(FLAGS_node_id);
  local_mr.set_node_id(FLAGS_node_id);

  // RC
  for (int i = 0; i < kNodeNum; i++) {
    if (i == FLAGS_node_id) continue;
    for (int j = 0; j < kMaxQPNum; j++)
      connects.defaultInitRCQP(i, j, &cq, &cq);
  }

  // Raw packet
  for (int i = 0; i < kMaxQPNum; i++)
    connects.default_init_raw_qp(i, &send_cq, &cq, FLAGS_nic_name, i, (666));

  // Connect
  Memcached::initMemcached(FLAGS_node_id);
  sleep(1);

  local_mr.publish_mrs();
  connects.publish_node_info();
  for (int i = 0; i < kNodeNum; i++)
    connects.publish_connect_info(i);

  for (int i = 0; i < kNodeNum; i++) {
    if (i == FLAGS_node_id) continue;
    connects.subscribe_nic_rcqp_mr(i);
  }
  LOG(INFO) << "Connected!";

  // server
  if (FLAGS_node_id == 0) {
    // rc qp
    for (int i = 0; i < kMaxQPNum; i++)
      connects.remote_node_[1].qp[i].init_recv_with_one_mr(local_mr.get_region(i), 0, FLAGS_test_size_kb * KB);
    
    // raw packet
    for (int i = 0; i < kMaxQPNum; i++)
      connects.raw_qp[i].init_recv_with_one_mr(local_mr.get_region(i), 0, FLAGS_test_size_kb * KB);

    while (true) {
      auto wc_array = cq->poll_cqi(1);
      ibv_wc* wc = nullptr;
      while ((wc = wc_array->get_next_wc())) {
        auto wr_info = (rdma::WrInfo*)wc->wr_id;
        wr_info->qp->process_next_msg();
        // puts("?");
      }
    }
  }

  /* client */

  for (int i = 0; i < kMaxQPNum; i++) // rc
    connects.remote_node_[0].qp[i].init_send_with_one_mr(local_mr.get_region(i), 0, FLAGS_test_size_kb * KB);
  
  for (int i = 0; i < kMaxQPNum; i++) // raw packet
    connects.raw_qp[i].init_raw_send_with_one_mr(local_mr.get_region(i), 0, FLAGS_test_size_kb * KB + sizeof(rdma::RoCEHeader));

  sleep(1);

  test_rdma(0);
  test_rdma(1);
  test_rdma(2);
  test_rdma(3);
  test_rdma(4);

  return 0;
}