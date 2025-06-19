// #include "include/index_wrapper.h"
// #include "include/test.h"
// #include "include/timestamp.h"
#include "filesystem.h"
#include "fsrpc.h"
#include "fss.h"
#include "rdma.h"
#include "rpc.h"

void config() {
  server_config.clear();
  rdma::ServerConfig item;

  global_config.rc_msg_size = DataBuffer::get_max_size();
  global_config.raw_msg_size = sizeof(MetaUpdateReq) + sizeof(MetaReadReply);

  LOG(INFO) << "Meta size = " << sizeof(file_extent_info) << " + " << sizeof(RawVMsg);
  global_config.link_type = rdma::kEnumRoCE;
  global_config.func = rdma::ConnectType::RAW | rdma::ConnectType::RC;

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
                              .thread_num = 1, // FIXME: TP only
                              .numa_id = 1,
                              .numa_size = 12,
                              .dev_id = 1,
                              .nic_name = "ens6",
                              .port_id = 136,
                          });

  server_config.push_back(rdma::ServerConfig{
      .server_type = 2,
      .thread_num = FLAGS_c_thread,
      .numa_id = 1,
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
      .nic_name = "ens6",
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
    .type_cnt = 9,
    .slowest_latency = 400,
};

int main(int argc, char **argv) {

  FLAGS_logtostderr = 1;
  gflags::SetUsageMessage("Usage ./rdma_clean_client --help");
  gflags::ParseCommandLineFlags(&argc, &argv, true);
  google::InitGoogleLogging(argv[0]);

  LOG(INFO) << "Raw packet size (" << sizeof(RawVMsg) << ") bytes.";

  config();

  Memcached::initMemcached(rpc::FLAGS_node_id);

  int server_type = server_config[rpc::FLAGS_node_id].server_type;

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
    reporter->new_type("read_bw");
    reporter->new_type("write_bw");
  } else if (server_type == 0) { // data node
    fs_server = new RPCServer(0);
    reporter->new_type("all");
    reporter->new_type("read");
    reporter->new_type("write");
  } else if (server_type == 1) { // index node
    //  ts_generator.
    fs_server = new RPCServer(1);
    fs_server->fs->rootInitialize(1);
    reporter->new_type("all");
    reporter->new_type("readi");
    reporter->new_type("writei");
    reporter->new_type("fast index");
  }
  reporter->master_thread_init();

  

  int thread_num = server_config[rpc::FLAGS_node_id].thread_num;
  std::thread **th = new std::thread *[thread_num];
  FsRpc *worker_list = new FsRpc[thread_num];

  if (server_type == 0)
    ts_generator.init();

  for (int i = 0; i < thread_num; i++) {
    worker_list[i].config_client(server_config, &global_config,
                                 rpc::FLAGS_node_id, i);
  }

  if (server_type != 0) {
    key_space = (KvKeyType *)malloc(sizeof(KvKeyType) * FLAGS_keyspace);
    
    srand(233);
    for (uint i = 0; i < FLAGS_keyspace; i++) {
      generate_key(i, key_space[i]);
      if (i < 100) {
        // printf("key%d = %lx\n", i, key_space[i]);
        print_key(i, key_space[i]);
      }
      // index->(uint32_t key, uint32_t *addr_addr)
      if (server_type == 1) {
        // index->put_long(key_space[i], 0);
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
      th[i] = new std::thread(std::bind(&FsRpc::server_batch_CPC, &worker_list[i]));
  } else if (server_type == 1) {
    for (int i = 0; i < thread_num; i++) 
      th[i] = new std::thread(std::bind(&FsRpc::server_batch_CPC, &worker_list[i]));
  } else {
    for (int i = 0; i < thread_num; i++)
      th[i] =
          new std::thread(std::bind(&FsRpc::run_client_CPC, &worker_list[i]));
  }

  while (true) {
    reporter->try_wait();
    // if (rpc::FLAGS_node_id == 2)
    #ifndef LOGDEBUG
      reporter->try_print(perf_config.type_cnt);
    #endif
  }

  for (int i = 0; i < thread_num; i++)
    th[i]->join();

  return 0;
  
}