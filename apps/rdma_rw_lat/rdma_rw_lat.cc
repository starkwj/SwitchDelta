

#include "rdma_perf.h"

using namespace std;

int main(int argc, char* argv[])
{
    FLAGS_logtostderr = 1;
    gflags::SetUsageMessage("Usage ./rdma_rw_lat --help");
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    google::InitGoogleLogging(argv[0]);

    rdmaperf::RdmaPerf rdma;

    if (rdmaperf::FLAGS_is_server == true)
        rdma.set_node_type(rdmaperf::kEnumServer);
    else
        rdma.set_node_type(rdmaperf::kEnumClient);

    if (!strcmp(rdmaperf::FLAGS_mmap.c_str(), "LM"))
        rdma.set_mr_location(rdmaperf::kEnumLocalMem);
    else if (!strcmp(rdmaperf::FLAGS_mmap.c_str(), "RM"))
        rdma.set_mr_location(rdmaperf::kEnumRemoteMem);
    else if (!strcmp(rdmaperf::FLAGS_mmap.c_str(), "LC"))
        rdma.set_mr_location(rdmaperf::kEnumLocalChip);
    else if (!strcmp(rdmaperf::FLAGS_mmap.c_str(), "RC"))
        rdma.set_mr_location(rdmaperf::kEnumRemoteChip);
    else {
        // rdmaperf::FLAGS_mmap = "/sys/bus/pci/devices/0000:83:00.0/resource0";
        LOG(INFO) << rdmaperf::FLAGS_mmap;
        rdma.set_mr_location(rdmaperf::kEnumFile);
    }

    rdma.RdmaInitCtx();
    rdma.connectTwoNode();

    if (rdmaperf::FLAGS_is_server == true)
    {
        LOG(INFO) << "server sleep";
        while (true)
        {
            sleep(2);
        }
    }
    else
    {
        LOG(INFO) << "client test";
        rdma.test(0, 17);
        rdma.test(1, 17);
        rdma.test(2, 17);
    }

    return 0;
}
