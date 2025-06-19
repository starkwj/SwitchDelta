#ifndef _OPERATION_H_
#define _OPERATION_H_


#include <cstddef>
#include <infiniband/verbs.h>
#include <infiniband/verbs_exp.h>
#include <gflags/gflags.h>
#include <glog/logging.h>
#include "hack_onchip.h"

#define PSN 3185
#define UD_PKEY 0x1111111
#define DCT_ACCESS_KEY 3185

namespace rdma
{


DECLARE_int32(cq_len);
DECLARE_int32(qp_len);
DECLARE_bool(port_func);
enum LinkType {
    kEnumIB = 1,
    kEnumRoCE = 2,
};

struct SrqConfig {
    int32_t normal_srq_len{0};
    bool mp_flag{false};
	uint32_t log_num_of_strides;
	uint32_t log_stride_size;
	uint32_t num_of_stride_groups;
};

struct NodeInfo;
struct RdmaCtx;

inline void fillSgeWr(ibv_sge& sg, ibv_send_wr& wr, uint64_t source,
    uint64_t size, uint32_t lkey);

// IN rdma.cc: THE RDMA INIT CODES

bool CreateCtx(RdmaCtx* rdma_ctx,
    uint8_t port = 1, int gid_index = 3, uint8_t dev_index = -1);
bool CreateCtxSimple(RdmaCtx* rdma_ctx, uint8_t dev_index, LinkType link_type);
bool destory_ctx(RdmaCtx* rdma_ctx); // TODO:

bool CreateCq(ibv_cq** cq, RdmaCtx* rdma_ctx, int cqe = -1);

ibv_mr* CreateMr(RdmaCtx* rdma_ctx,
    uint64_t mr_addr, size_t mr_size);
ibv_mr* CreateMemoryRegionOnChip(uint64_t mm, uint64_t mmSize,
    RdmaCtx* ctx, ibv_exp_dm** dm_ptr);
void init_srq(RdmaCtx* rdma_ctx, ibv_srq** srq, ibv_cq* recv_cq = nullptr, SrqConfig* srq_config_ = nullptr);

bool createDCTarget(ibv_exp_dct** dct, ibv_cq* cq, rdma::RdmaCtx* rdma_ctx,
    uint32_t qpsMaxDepth = 128, uint32_t maxInlineData = 0);
bool create_qp(ibv_qp** qp,
    RdmaCtx* rdma_ctx, ibv_cq* send_cq, ibv_cq* recv_cq,
    ibv_qp_type mode, int32_t qpr_max_depth = -1, ibv_srq* srq = nullptr, int32_t qps_max_depth = -1, int32_t qp_max_sge = 1);
bool modifyQPtoInit(RdmaCtx* rdma_ctx, struct ibv_qp* qp);
bool modifyQPtoRTR(RdmaCtx* rdma_ctx, struct ibv_qp* qp,
    uint32_t remote_qpn, uint16_t remote_lid, uint8_t* remote_gid);
bool modifyQPtoRTS(struct ibv_qp* qp);
bool modifyUDtoRTS(RdmaCtx* rdma_ctx, struct ibv_qp *qp);
int poll_a_wc(ibv_cq* cq, struct ibv_wc* wc);

void steeringWithMacUdp(ibv_qp *qp, rdma::RdmaCtx *ctx, const uint8_t mac[6], 
        uint16_t dstPort, uint16_t srcPort,uint16_t dst_port_mask = 0xFFFF, uint16_t priority = 0);
void steeringWithMacUdp_exp(ibv_qp *qp, rdma::RdmaCtx *ctx, const uint8_t mac[6], 
        uint16_t dstPort, uint16_t srcPort,uint16_t dst_port_mask = 0xFFFF);
bool create_ah(ibv_ah **ah_ptr, rdma::RdmaCtx* rdma_ctx, uint32_t remote_lid, uint8_t* remote_gid);

bool modifyDCtoRTS(struct ibv_qp *qp, uint16_t remoteLid, uint8_t *remoteGid, RdmaCtx* rdma_ctx, uint64_t interface_id);

inline uint16_t toBigEndian16(uint16_t v)
{
    uint16_t res;

    uint8_t* a = (uint8_t*)&v;
    uint8_t* b = (uint8_t*)&res;

    b[0] = a[1];
    b[1] = a[0];

    return res;
}




inline void fillSgeWr(ibv_sge& sg, ibv_send_wr& wr, uint64_t source,
    uint64_t size, uint32_t lkey)
{
    memset(&sg, 0, sizeof(sg));
    sg.addr = (uintptr_t)source;
    sg.length = size;
    sg.lkey = lkey;

    // memset(&wr, 0, sizeof(wr));
    // wr.wr_id = 0;
    wr.sg_list = &sg;
    wr.num_sge = 1;
}

struct RdmaCtxLevelInfo
{
    int ctx_signal_num = 0;
};

struct NodeInfo{
    uint16_t lid;
    union ibv_gid gid;
};

struct RdmaCtx
{
    ibv_context* ctx;
    ibv_pd* pd;

    uint8_t dev_index;
    uint8_t port;
    int gid_index;
    NodeInfo nic_id;

    RdmaCtxLevelInfo user_info;

    RdmaCtx()
        : ctx(NULL), pd(NULL)
    {
    }

    void createCtx(int dev_index, LinkType link_type = rdma::kEnumIB){
        CreateCtxSimple(this, dev_index, link_type);
        return;
    }
};





} /*namespace rdma*/

#endif