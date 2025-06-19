
#include "common_config.h"

namespace juneberry {

DEFINE_int32(client_thread, 47, "client_thread");
DEFINE_int32(server_thread, 1, "server_thread");
DEFINE_int32(client_cnt, 3, "client_cnt");

rdma::GlobalConfig<rpc::kConfigServerCnt> global_config;
std::vector<rdma::ServerConfig> machine_config;

perf::PerfConfig perf_config = {
    .thread_cnt = rpc::kConfigMaxThreadCnt,
    .coro_cnt = rpc::kConfigCoroCnt,
    .type_cnt = 3,
};

} // namespace juneberry
