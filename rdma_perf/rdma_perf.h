/**
 * @file rdma_perf.h
 * @author Junru Li (15668076331@163.com)
 * @brief 
 * @version 0.1
 * @date 2022-06-07
 * 
 * @copyright Copyright (c) 2022
 * 
 */
#pragma once

#ifndef _RDMA_PERF_H_
#define _RDMA_PERF_H_

#include "rdma.h"
#include "bindcore.h"
#include "size_def.h"

namespace rdmaperf
{
DECLARE_bool(is_server);
DECLARE_string(mmap);

enum RegionLocation
{
    kEnumLocalMem = 0,
    kEnumRemoteMem,
    kEnumLocalChip,
    kEnumRemoteChip,
    kEnumFile
};

enum RdmaTestType
{
    kEnumReadLat = 0,
    kEnumWriteLat,
    kEnumFlushWriteLat
};

enum NodeType
{
    kEnumServer,
    kEnumClient
};

class RdmaPerf
{
private:
    /* data */
    rdma::RdmaCtx context;
    rdma::RdmaCtx context_dev;

    int kDevId;
    uint64_t kMmSize;
    RdmaTestType test_type;
    NodeType node_type = NodeType::kEnumClient;
    RegionLocation mr_location = kEnumLocalMem;

    rdma::RegionArr<2> local_mem;
    ibv_mr* message_mr[2];
    ibv_mr* onchip_mr;

    ibv_cq* cq;

    rdma::RwConnect<2> connect;

    // uint64_t mm;
    int64_t kCountNum;
    uint64_t remote_addr = 0;
    struct rdma::BatchWr<2> odp_test_wr;
    int size_;
    // uint64_t p[2];
    uint64_t read_mm;

public:
    RdmaPerf(/* args */);

    void set_node_type(enum NodeType node_type_x)
    {
        node_type = node_type_x;
    }

    void set_mr_location(enum RegionLocation mr_location_x)
    {
        mr_location = mr_location_x;
    }

    inline void InitMemMr(int region_id)
    {
        auto& rg = local_mem.region[region_id];
        rg.addr = (uint64_t)malloc(kMmSize);
        memset((void*)local_mem.region[region_id].addr, 0, kMmSize);
        message_mr[region_id] = rdma::CreateMr(&(context), rg.addr, kMmSize);
        rg.key = message_mr[region_id]->lkey;
    }


    void RdmaInitCtx();
    void connectTwoNode();
    void rdma_try_write_read(int offset, int size);
    void rdma_try_write(int offset, int size);
    void rdma_try_read(int offset, int size);

    void rdma_wait(int id);
    void warm_up(int type, int x);
    void test(int type, int test_num);

    ~RdmaPerf()
    {
    }
};

}; // namespace rdmaperf
#endif
