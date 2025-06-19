#pragma once
#include "../operation/operation.h"
#include "memcached_mgr.h"
#include "common.h"
#include <assert.h>


namespace rdma {

struct Region
{
    ibv_mr* mr;
    uint64_t addr; // void *
    uint32_t key;
    uint32_t size;
};

template<int kMaxRegNum>
class RegionArr {
private:
    uint global_id_;
public:
    Region* region;

    RegionArr() {
        region = new Region[kMaxRegNum];
        for (int i = 0; i < kMaxRegNum; i++) {
            region[i].mr = nullptr;
        }
    }

    Region* get_region(int region_id){
        return &region[region_id];
    }

    void setRegion(int region_id, uint64_t addr, uint32_t key, uint32_t size);

    void initRegion(RdmaCtx* rdma_ctx, uint region_id, uint64_t addr, uint32_t size);

    void set_node_id(int global_id) {
        global_id_ = global_id;
        return;
    }

    inline uint64_t get_addr(int region_id) {
        return region[region_id].addr;
    }

    void publish_mrs() {
        std::string set_key = setKey(global_id_, global_id_, "mr");
        Memcached::get_instance().memcachedSet(set_key, (char*)region, sizeof(region));
    }

    void subscribe_mrs(uint remote_node_id) {
        std::string get_key = getKey(remote_node_id, remote_node_id, "mr");
        Memcached::get_instance().memcachedGet(get_key, (char*)region, sizeof(region));
    }

};

template<int kMaxMR>
struct RCQPEndPoint {
    uint16_t lid;
    uint8_t gid[16];
    uint32_t qp_num;
    Region* region;
    RCQPEndPoint() {
        region = new Region[kMaxMR];
    }
};


template<int kMaxRegNum>
void RegionArr<kMaxRegNum>::setRegion(int region_id, uint64_t addr, uint32_t key, uint32_t size) {
    auto& r = region[region_id];
    r.addr = addr;
    r.key = key;
    r.size = size;
    return;
}

template<int kMaxRegNum>
void RegionArr<kMaxRegNum>::initRegion(RdmaCtx* rdma_ctx, uint region_id, uint64_t addr, uint32_t size) {
    if (region[region_id].mr != NULL) {
        // TODO: Free the old region.

    }
    auto& mr = region[region_id].mr;
    mr = rdma::CreateMr(rdma_ctx, addr, size);
    setRegion(region_id, addr, mr->lkey, size);
    return;
}

}