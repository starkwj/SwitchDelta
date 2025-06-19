#include "rdma_perf.h"
#include "perfv2.h"
#include "hack_onchip.h"
#include "memcached_mgr.h"

namespace rdmaperf
{

DEFINE_int32(mr_size, 256, "memory region size KB");
DEFINE_int32(dev_id, 0, "device id mlx5_x?");
DEFINE_int32(lat_n, 100000, "lat test count");
DEFINE_int32(numa_size, 12, "numa_size");

DEFINE_string(mmap, "LM", "LM: LocalMem, RM: RemoteMem");
DEFINE_bool(is_server, false, "is server?");
DEFINE_int32(link_type, 1, "1:IB 2:ROCE");

static int test_size[50] = { 8, 63, 64, 65,
                            127, 128, 129,
                            511, 512, 513,
                            1023, 1024, 1025, 2048,
                            4095, 4096, 4097 };

RdmaPerf::RdmaPerf()
{
    kMmSize = FLAGS_mr_size * KB;
    kDevId = FLAGS_dev_id;
    kCountNum = FLAGS_lat_n;
}

/**
 * @brief create Ctx and cq, then create two local memory region.
 *
 */
void RdmaPerf::RdmaInitCtx()
{
    // Two regions, one is for test, one is for sync the write.

    local_mem.region[0].size = local_mem.region[1].size = kMmSize;

    rdma::CreateCtxSimple(&context, kDevId, (rdma::LinkType)FLAGS_link_type);
    rdma::CreateCq(&cq, &context);

    /*The first region*/
    if (NodeType::kEnumClient == node_type) {
        LOG(INFO) << "Client";
        BindCore(kDevId * FLAGS_numa_size);
        InitMemMr(0);
    }
    else {
        switch (mr_location)
        {
        case kEnumLocalMem: {
            BindCore(kDevId * FLAGS_numa_size);
            InitMemMr(0);
            break;
        }
        case kEnumRemoteMem: {
            BindCore(kDevId * FLAGS_numa_size + FLAGS_numa_size);
            InitMemMr(0);
            break;
        }
        case kEnumLocalChip: {
            struct ibv_exp_dm* dm_ptr;
            message_mr[0] = CreateMemoryRegionOnChip(0, kMmSize, &(context), &dm_ptr);
            local_mem.region[0].addr = (uint64_t)message_mr[0]->addr;
            local_mem.region[0].key = message_mr[0]->lkey;
            break;
        }
        case kEnumRemoteChip: {
            struct ibv_exp_dm* dm_ptr = NULL;
            rdma::CreateCtx(&context_dev, 1, 0, !kDevId);
            onchip_mr = CreateMemoryRegionOnChip(0, kMmSize, &(context_dev), &dm_ptr);
            struct mlx5_dm* mlx5_dm = (struct mlx5_dm*)dm_ptr;
            printf("chip = %lu %lu\n", (uint64_t)mlx5_dm->mmap_va, *(uint64_t*)mlx5_dm->mmap_va);
            local_mem.region[0].addr = (uint64_t)mlx5_dm->mmap_va;
            memset((void*)local_mem.region[0].addr, 0, kMmSize);

            message_mr[0] = rdma::CreateMr(&(context), local_mem.region[0].addr, kMmSize);
            local_mem.region[0].key = message_mr[0]->lkey;
            break;
        }
        case kEnumFile: {
            int fd_ = open(FLAGS_mmap.c_str(), O_RDWR);
            local_mem.region[0].addr = (uint64_t)mmap((void*)0x400000000, kMmSize, PROT_READ | PROT_WRITE,
                MAP_SHARED | MAP_POPULATE | MAP_FIXED, fd_, 0);
            // printf("fd = %d local_mem.region[0].addr =%d\n",fd_, local_mem.region[0].addr);
            LOG(INFO) << "kEnumFile";
            assert((void*)local_mem.region[0].addr != MAP_FAILED);
            memset((void*)local_mem.region[0].addr, 0, kMmSize);

            message_mr[0] = rdma::CreateMr(&(context), local_mem.region[0].addr, kMmSize);
            local_mem.region[0].key = message_mr[0]->lkey;
            break;
        }
        default:
            assert(0 == 1);
            break;
        }
    }

    /*The second region*/
    BindCore(kDevId * FLAGS_numa_size);
    InitMemMr(1);

    return;
}


std::string getPerfKey(){
    return FLAGS_is_server?"perfclient":"perfserver";
}

std::string setPerfKey(){
    return FLAGS_is_server?"perfserver":"perfclient";
}

/**
 * @brief Connect two nodes (perf_client and perf_server)
 *
 */
void RdmaPerf::connectTwoNode()
{
    auto &local = connect.local;
    auto &remote = connect.remote;
    
    rdma::create_qp(&connect.qp, &(context), cq, cq, IBV_QPT_RC);
    local.qp_num = connect.qp->qp_num;
    local.lid = context.lid;
    memcpy(local.gid, &context.gid, 16);
    local.region[0] =  local_mem.region[0];
    local.region[1] = local_mem.region[1];

    if (FLAGS_is_server)
        Memcached::initMemcached(0);

    std::string set_key = setPerfKey();
    std::string get_key = getPerfKey();
    Memcached::get_instance().memcachedSet(set_key, (char *)&local, sizeof(local));
    Memcached::get_instance().memcachedGet(get_key, (char *)&remote, sizeof(local));

    /*
    uint64_t p_[2];
    memcpy((uint8_t*)p_, connect.local.gid, 16);
    printf("%u %lu %u %lu %u %u %lx %lx\n",
        local_mem.region[0].key, local_mem.region[0].addr,
        local_mem.region[1].key, local_mem.region[1].addr,
        connect.local.lid, connect.qp->qp_num, p_[0], p_[1]);

    scanf("%u %lu %u %lu %hu %u %lx %lx",
        &connect.remote.region[0].key, &connect.remote.region[0].addr,
        &connect.remote.region[1].key, &connect.remote.region[1].addr,
        &connect.remote.lid, &connect.remote.qp_num, &p_[0], &p_[1]);
    memcpy(connect.remote.gid, (uint8_t*)p_, 16);
    */

    rdma::modifyQPtoInit(&context, connect.qp);
    rdma::modifyQPtoRTR(&context, connect.qp, remote.qp_num, remote.lid, remote.gid);
    rdma::modifyQPtoRTS(connect.qp);
}

void RdmaPerf::rdma_try_write_read(int offset, int size)
{
    uint64_t* data = (uint64_t*)(local_mem.region[0].addr + offset);
    data[0] = kCountNum;
    odp_test_wr.wr_n = 2;
    odp_test_wr.get_wr(0)
        .set_source_addr(local_mem.region[0].addr + offset)
        .set_l_key(local_mem.region[0].key)
        .set_dest_addr(connect.remote.region[0].addr + offset)
        .set_r_key(connect.remote.region[0].key)
        .set_size(size)
        .set_write_op()
        .rc_done();
    
    odp_test_wr.get_wr(1)
        .set_source_addr(local_mem.region[1].addr + offset)
        .set_l_key(local_mem.region[1].key)
        .set_dest_addr(connect.remote.region[1].addr + offset)
        .set_r_key(connect.remote.region[1].key)
        .set_size(1)
        .set_read_op()
        .rc_done();

    odp_test_wr.generate_and_issue_batch_signal(connect.qp);
}

void RdmaPerf::rdma_try_read(int offset, int size) {
    uint64_t* data = (uint64_t*)(local_mem.region[0].addr + offset);
    data[0] = kCountNum;
    odp_test_wr.wr_n = 1;
    odp_test_wr.get_wr(0)
        .set_source_addr(local_mem.region[0].addr + offset)
        .set_l_key(local_mem.region[0].key)
        .set_dest_addr(connect.remote.region[0].addr + offset)
        .set_r_key(connect.remote.region[0].key)
        .set_size(size)
        .set_write_op()
        .rc_done();
    odp_test_wr.generate_and_issue_batch_signal(connect.qp);
}

void RdmaPerf::rdma_try_write(int offset, int size) {
    uint64_t* data = (uint64_t*)(local_mem.region[0].addr + offset);
    data[0] = kCountNum;
    odp_test_wr.wr_n = 1;
    odp_test_wr.get_wr(0)
        .set_source_addr(local_mem.region[0].addr + offset)
        .set_l_key(local_mem.region[0].key)
        .set_dest_addr(connect.remote.region[0].addr + offset)
        .set_r_key(connect.remote.region[0].key)
        .set_size(size)
        .set_read_op()
        .rc_done();
    odp_test_wr.generate_and_issue_batch_signal(connect.qp);
}
/**
 * @brief 
 * 
 * @param type [0:read | 1:write | 2:write+read1]
 * @param test_num the head `test_num` size in test_size
 */
void RdmaPerf::test(int type, int test_num)
{
    for (int test_i = 0; test_i < test_num; test_i++)
    {
        perf::PerfTool* perf = perf::PerfTool::get_instance_ptr();
        // perf->enableLatency().newPerfTool();
        perf->worker_init(0);

        warm_up(type, 8);
        size_ = test_size[test_i];
        timespec v_s, v_e;
        clock_gettime(CLOCK_REALTIME, &v_s);

        int kOffsetSize = (size_ - 1) * 2;
        if (size_ % 4 == 0)
        {
            kOffsetSize = size_;
        }
        else if ((size_ + 1) % 4 == 0)
        {
            kOffsetSize = size_ + 1;
        }
        int kCntPerBuffer = kMmSize / kOffsetSize;
        int offset = 0;

        for (int i = 0; i < kCountNum; i++)
        {
            offset = (kOffsetSize) * (i % kCntPerBuffer);
            // perf.begin(0);
            if (type == 0)
                rdma_try_read(offset, size_);
            else if (type == 1)
                rdma_try_write(offset, size_);
            else if (type == 2)
                rdma_try_write_read(offset, size_);
            rdma_wait(0);
            // perf.end(0);
        }
        clock_gettime(CLOCK_REALTIME, &v_e);
        // uint64_t fuck_time = (v_e.tv_sec * 1000000000ull + v_e.tv_nsec) - (v_s.tv_sec * 1000000000ull + v_s.tv_nsec);
        // printf("size: %d %s %lf\n", size_,
        //     type == 0 ? "rlat" : (type == 1 ? "wlat" : "wrlat"),
        //     fuck_time * 1.0 / kCountNum);
        // if (mr_location != kEnumFile)
        //     perf->print_latency_file((FLAGS_mmap + std::to_string((int)mr_location) + 
        //         (type == 0 ? "rlat" : (type == 1 ? "wlat" : "wrlat")) +
        //         std::to_string(size_)).c_str(),
        //         0, 0);
        // else {
        //     perf->print_latency_file(std::string("FPGA") + std::to_string((int)mr_location) +
        //         (type == 0 ? "rlat" : (type == 1 ? "wlat" : "wrlat")) +
        //         std::to_string(size_).c_str(),
        //         0, 0);
        // }
    }
    return;
}


void RdmaPerf::rdma_wait(int id)
{
    ibv_wc wc;
    while (rdma::poll_a_wc(cq, &wc) == 0)
        ;
}

void RdmaPerf::warm_up(int type, int x)
{
    size_ = x * KB;
    // puts("warm_up");
    for (int i = 0; i < 2; i++)
    {
        if (type == 0)
            rdma_try_read(0, size_);
        else
            rdma_try_write(0, size_);
        rdma_wait(0);
    }
    // puts("begin_test");
}

}
