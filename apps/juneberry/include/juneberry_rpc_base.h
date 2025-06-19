#if !defined(_JUNEBERRY_RPC_BASE_H_)
#define _JUNEBERRY_RPC_BASE_H_

#include "operation.h"
#include "rpc.h"
#include "timer.h"
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <gperftools/profiler.h>
#include <infiniband/verbs_exp.h>

namespace juneberry {

class JuneberryRPCBase {
public:
  rdma::Rdma<rpc::kConfigServerCnt, rpc::kConfigMaxThreadCnt,
             rpc::kConfigMaxQPCnt> *agent;

  perf::PerfTool *reporter;
  uint8_t node_id;
  uint8_t thread_id;
  char *buffer;

  void init() {
    node_id = agent->get_server_id();
    thread_id = agent->get_thread_id();
    BindCore(agent->server.get_core_id(thread_id));

    reporter = perf::PerfTool::get_instance_ptr();
    reporter->worker_init(thread_id);
  }

  void connect() {

    uint32_t mm_size = agent->global_config->one_sided_rw_size * MB;
    void *mm_addr = malloc(mm_size);
    buffer = (char *)mm_addr;
    agent->config_rdma_region(0, (uint64_t)mm_addr, mm_size);

    uint32_t recv_size = agent->get_recv_buf_size();
    void *recv_addr = nullptr;
    [[maybe_unused]] auto ret = posix_memalign(&recv_addr, 64, recv_size);
    agent->config_recv_region((uint64_t)recv_addr, recv_size);
    printf("all recv buffer size: %d bytes\n", recv_size);

    uint32_t send_size = agent->get_send_buf_size();
    void *send_addr = nullptr;
    ret = posix_memalign(&send_addr, 64, recv_size);
    agent->config_send_region((uint64_t)send_addr, send_size);
    printf("all send buffer size: %d bytes\n", send_size);

    agent->connect();

    reporter->thread_begin(thread_id);
  }
};

} // namespace juneberry

#endif // _JUNEBERRY_RPC_BASE_H_
