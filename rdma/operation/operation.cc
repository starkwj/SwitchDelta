#include "operation.h"
#include <infiniband/verbs.h>
#include <infiniband/mlx5dv.h>
#include <infiniband/verbs_exp.h>

namespace rdma
{


// DEFINE_int32(qp_len, 128, "The default length of qp");
DEFINE_int32(qp_len, 2048, "The default length of qp");
DEFINE_int32(cq_len, 512 * 128, "The default length of cq");
DEFINE_bool(port_func, false, "steering rule true: bitmap, false: id");

bool CreateCtx(rdma::RdmaCtx* rdma_ctx,
    uint8_t port, int gid_index, uint8_t dev_index)
{

    ibv_device* dev_ = NULL;
    ibv_context* ctx_ = NULL;
    ibv_pd* pd_ = NULL;
    ibv_port_attr port_attr_;

    /* get device names in the system */
    int devices_num_;
    int dev_id_ = 0;
    struct ibv_device** device_list_ = ibv_get_device_list(&devices_num_);
    if (!device_list_)
    {
        LOG(ERROR) << "Failed to get IB devices list :(";
        goto CreateResourcesExit;
    }
    if (devices_num_ == 0) // if there isn't any IB device in host
    {
        LOG(ERROR) << "No IB device :(";
        goto CreateResourcesExit;
    }

    /* open the mlx5_[${dev_index}] */
    for (int i = 0; i < devices_num_; ++i)
    {
        if (ibv_get_device_name(device_list_[i])[5] == '0' + dev_index)
        {
            dev_id_ = i;
            break;
        }
    }
    if (dev_id_ >= devices_num_)
    {
        LOG(ERROR) << ("IB device wasn't found :(");
        goto CreateResourcesExit;
    }
    dev_ = device_list_[dev_id_];
    LOG(INFO) << "I open " << ibv_get_device_name(dev_) << ":)";

    /* Create the context */
    ctx_ = ibv_open_device(dev_);
    if (!ctx_)
    {
        LOG(ERROR) << "failed to open device";
        goto CreateResourcesExit;
    }

    

    /* We are now done with device list, free it */
    ibv_free_device_list(device_list_);
    device_list_ = NULL;

    /* query port properties */
    if (ibv_query_port(ctx_, port, &port_attr_))
    {
        LOG(ERROR) << "ibv_query_port failed";
        goto CreateResourcesExit;
    }

    // allocate Protection Domain
    pd_ = ibv_alloc_pd(ctx_);
    if (!pd_)
    {
        LOG(ERROR) << "ibv_alloc_pd failed";
        goto CreateResourcesExit;
    }

    if (ibv_query_gid(ctx_, port, gid_index, &rdma_ctx->nic_id.gid))
    {
        LOG(ERROR) << "could not get gid for port: " << port << ", gidIndex: " << gid_index;
        goto CreateResourcesExit;
    }

    LOG(INFO) << "gidIndex: " << gid_index;
    // printf("port-%d\n",port);
    // Success :)
    rdma_ctx->dev_index = dev_index;
    rdma_ctx->gid_index = gid_index;
    rdma_ctx->port = port;
    rdma_ctx->ctx = ctx_;
    rdma_ctx->pd = pd_;
    rdma_ctx->nic_id.lid = port_attr_.lid;

    return true;

    /* Error encountered, cleanup */
CreateResourcesExit:
    LOG(ERROR) << "Error Encountered, Cleanup ...";

    if (pd_)
    {
        ibv_dealloc_pd(pd_);
        pd_ = NULL;
    }
    if (ctx_)
    {
        ibv_close_device(ctx_);
        ctx_ = NULL;
    }
    if (device_list_)
    {
        ibv_free_device_list(device_list_);
        device_list_ = NULL;
    }

    return false;
}

bool CreateCtxSimple(RdmaCtx* rdma_ctx, uint8_t dev_index, LinkType link_type){
    if (link_type == kEnumRoCE)
        return CreateCtx(rdma_ctx, 1, 3, dev_index);
    else 
        return CreateCtx(rdma_ctx, 1, 0, dev_index);
}

ibv_mr* CreateMr(rdma::RdmaCtx* rdma_ctx,
    uint64_t mr_addr, size_t mr_size)
{
    ibv_mr* mr_ = NULL;
    mr_ = ibv_reg_mr(rdma_ctx->pd, (void*)mr_addr, mr_size,
        IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE);

    if (!mr_)
    {
        LOG(ERROR) << "Memory registration failed @" << mr_addr <<", size:"<< mr_size << ", error:" << strerror(errno);
    }
    return mr_;
}


ibv_mr *CreateMemoryRegionOnChip(uint64_t mm, uint64_t mmSize,
                                 RdmaCtx *ctx, ibv_exp_dm **dm_ptr)
{
#ifdef ONCHIP_OK
    /* Device memory allocation request */
    struct ibv_exp_alloc_dm_attr dm_attr;
    memset(&dm_attr, 0, sizeof(dm_attr));
    dm_attr.length = mmSize;
    struct ibv_exp_dm *dm = ibv_exp_alloc_dm(ctx->ctx, &dm_attr);
    if (!dm)
    {
        LOG(ERROR) << "Allocate on-chip memory failed";
        return nullptr;
    }
    *dm_ptr = dm;
    /* Device memory registration as memory region */
    struct ibv_exp_reg_mr_in mr_in;
    memset(&mr_in, 0, sizeof(mr_in));
    mr_in.pd = ctx->pd, mr_in.addr = (void *)mm, mr_in.length = mmSize,
    mr_in.exp_access = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ |
                       IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC,
    mr_in.create_flags = 0;
    mr_in.dm = dm;
    mr_in.comp_mask = IBV_EXP_REG_MR_DM;
    struct ibv_mr *mr = ibv_exp_reg_mr(&mr_in);
    if (!mr)
    {
        LOG(ERROR) << "Memory registration failed";
        return nullptr;
    }

    // init zero
    char *buffer = (char *)malloc(mmSize);

    memset(buffer, 0, mmSize);
    *(uint64_t *)buffer = 1234567;

    struct ibv_exp_memcpy_dm_attr cpy_attr;
    memset(&cpy_attr, 0, sizeof(cpy_attr));
    cpy_attr.memcpy_dir = IBV_EXP_DM_CPY_TO_DEVICE;
    cpy_attr.host_addr = (void *)buffer;
    cpy_attr.length = mmSize;
    cpy_attr.dm_offset = 0;
    ibv_exp_memcpy_dm(dm, &cpy_attr);

    free(buffer);
    return mr;
#endif
    return NULL;
}


bool CreateCq(ibv_cq** cq, RdmaCtx* rdma_ctx, int cqe){
    int _ = cqe;
    if (_ == -1){
        _ = FLAGS_cq_len;
    }
    // printf("cq size = %d\n", _);
    *cq = ibv_create_cq(rdma_ctx->ctx, _, NULL, NULL, 0);
    if (*cq != NULL)
        return true;
    return false;
}

void init_srq(RdmaCtx* rdma_ctx, ibv_srq** srq, ibv_cq* recv_cq, SrqConfig* srq_config_) {

    struct ibv_exp_create_srq_attr  srq_init_attr;
    memset(&srq_init_attr, 0, sizeof(srq_init_attr));
    if (srq_config_ == nullptr || srq_config_->normal_srq_len == 0 || srq_config_->normal_srq_len <= FLAGS_qp_len)
        srq_init_attr.base.attr.max_wr  = FLAGS_qp_len;
    else 
        srq_init_attr.base.attr.max_wr  = srq_config_->normal_srq_len;
    srq_init_attr.base.attr.max_sge = 1;
    srq_init_attr.srq_type = IBV_EXP_SRQT_TAG_MATCHING;
    srq_init_attr.pd = rdma_ctx->pd;
    
    if (srq_config_ != nullptr && srq_config_->mp_flag) {
        srq_init_attr.comp_mask =
        IBV_EXP_CREATE_SRQ_CQ | IBV_EXP_CREATE_SRQ_TM | IBV_EXP_CREATE_SRQ_MP_WR;
        srq_init_attr.mp_wr.log_num_of_strides = srq_config_->log_num_of_strides;
        srq_init_attr.mp_wr.log_stride_size = srq_config_->log_stride_size; // 64B stride
        srq_init_attr.base.attr.max_wr = srq_config_->num_of_stride_groups;
        puts("mp");
    }
    else 
        srq_init_attr.comp_mask =
        IBV_EXP_CREATE_SRQ_CQ | IBV_EXP_CREATE_SRQ_TM;
        // 0;
    if (recv_cq == nullptr) {
        srq_init_attr.srq_type = IBV_EXP_SRQT_BASIC; // for DCT?
    }


    srq_init_attr.tm_cap.max_num_tags = 1;
    srq_init_attr.tm_cap.max_ops = 1;

    printf("create srq with length=%d\n", srq_init_attr.base.attr.max_wr);

    // srq_init_attr_exp.mp_wr.log_num_of_strides = 0;
    // srq_init_attr_exp.mp_wr.log_stride_size = 6;
    // printf("srq's cq %p\n", recv_cq);
    srq_init_attr.cq = recv_cq;

    *srq = ibv_exp_create_srq(rdma_ctx->ctx, &srq_init_attr);

    if (*srq == nullptr) {
        printf("error %s\n", strerror(errno));
    }
    // *srq = ibv_create_srq(rdma_ctx->ctx, &ex1);
}

bool create_qp(ibv_qp** qp,
    rdma::RdmaCtx* rdma_ctx, ibv_cq* send_cq, ibv_cq* recv_cq,
    ibv_qp_type mode, int32_t qpr_max_depth, ibv_srq* srq, int32_t qps_max_depth, int32_t qp_max_sge)
{
    if (qpr_max_depth == -1)
    {
        qpr_max_depth = FLAGS_qp_len;
    }

    if (qps_max_depth == -1)
    {
        qps_max_depth = FLAGS_qp_len;
    }

    uint32_t maxInlineData = 0;
    struct ibv_qp_init_attr attr_;
    struct ibv_qp_init_attr_ex attr_exp_;

    if (mode == IBV_EXP_QPT_DC_INI) {
        memset(&attr_exp_, 0, sizeof(attr_exp_));
        attr_exp_.qp_type = mode;
        attr_exp_.sq_sig_all = 0;
        attr_exp_.send_cq = send_cq;
        attr_exp_.recv_cq = recv_cq;
        attr_exp_.pd = rdma_ctx->pd;
        attr_exp_.comp_mask = IBV_QP_INIT_ATTR_PD;

        attr_exp_.cap.max_send_wr = qps_max_depth;
        attr_exp_.cap.max_recv_wr = qpr_max_depth;
        attr_exp_.cap.max_send_sge = qp_max_sge;
        attr_exp_.cap.max_recv_sge = qp_max_sge;
        attr_exp_.cap.max_inline_data = maxInlineData;

        *qp = ibv_create_qp_ex(rdma_ctx->ctx, &attr_exp_);
    }
    else if (srq != nullptr) {
        memset(&attr_, 0, sizeof(attr_));
        attr_.qp_type = mode;
        attr_.sq_sig_all = 0;
        attr_.send_cq = send_cq;
        attr_.recv_cq = recv_cq;
        attr_.qp_context = rdma_ctx->ctx;
        attr_.srq = srq;

        attr_.cap.max_send_wr = qps_max_depth;
        attr_.cap.max_recv_wr = qpr_max_depth;
        attr_.cap.max_send_sge = qp_max_sge;
        attr_.cap.max_recv_sge = qp_max_sge;
        attr_.cap.max_inline_data = maxInlineData;
        *qp = ibv_create_qp(rdma_ctx->pd, &attr_);
    }
    else {
        memset(&attr_, 0, sizeof(attr_));
        attr_.qp_type = mode;
        attr_.sq_sig_all = 0;
        attr_.send_cq = send_cq;
        attr_.recv_cq = recv_cq;
        attr_.qp_context = rdma_ctx->ctx;
        // attr_.srq = srq;

        attr_.cap.max_send_wr = qps_max_depth;
        attr_.cap.max_recv_wr = qpr_max_depth;
        attr_.cap.max_send_sge = qp_max_sge;
        attr_.cap.max_recv_sge = qp_max_sge;
        attr_.cap.max_inline_data = maxInlineData;
        *qp = ibv_create_qp(rdma_ctx->pd, &attr_);
    }


    if (!(*qp))
    {
        LOG(ERROR) << "Failed to create QP";
        return false;
    }

    return true;
}


bool createDCTarget(ibv_exp_dct** dct, ibv_cq* cq, rdma::RdmaCtx* rdma_ctx,
    uint32_t qpsMaxDepth, uint32_t maxInlineData)
{

    // construct SRQ fot DC Target :)

    ibv_exp_dct_init_attr dAttr;
    memset(&dAttr, 0, sizeof(dAttr));

    init_srq(rdma_ctx, &dAttr.srq);

    dAttr.gid_index = rdma_ctx->gid_index;
    dAttr.pd = rdma_ctx->pd;
    dAttr.cq = cq;
    // dAttr.srq = srq;
    dAttr.dc_key = DCT_ACCESS_KEY;
    dAttr.port = rdma_ctx->port;
    dAttr.access_flags = IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_ATOMIC;
    // dAttr.access_flags = IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_READ |
    //                      IBV_ACCESS_REMOTE_ATOMIC;
    dAttr.min_rnr_timer = 12;
    dAttr.tclass = 0;
    dAttr.flow_label = 0;
    dAttr.mtu = IBV_MTU_4096;
    dAttr.pkey_index = 0;
    dAttr.hop_limit = 1;
    dAttr.create_flags = 0;
    dAttr.inline_size = maxInlineData;

    *dct = ibv_exp_create_dct(rdma_ctx->ctx, &dAttr);
    if (dct == NULL) {
        LOG(ERROR) << "failed to create dc target";
        return false;
    }

    return true;
}

bool modifyQPtoInit(rdma::RdmaCtx* rdma_ctx, struct ibv_qp* qp)
{

    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));

    attr.qp_state = IBV_QPS_INIT;
    attr.port_num = rdma_ctx->port;
    attr.pkey_index = 0;

    switch (qp->qp_type)
    {
    case IBV_QPT_RC:
        attr.qp_access_flags = IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC | IBV_ACCESS_LOCAL_WRITE;
        break;

    case IBV_QPT_UC:
        attr.qp_access_flags = IBV_ACCESS_REMOTE_WRITE;
        break;

    default:
        // Debug::notifyError("implement me:)");
        break;
    }

    if (ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT | IBV_QP_ACCESS_FLAGS))
    {
        LOG(ERROR) << "Failed to modify QP state to INIT";
        return false;
    }
    return true;
}

void fillAhAttr(ibv_ah_attr* attr, uint32_t remoteLid, uint8_t* remote_gid,
    rdma::RdmaCtx* rdma_ctx)
{

    (void)remote_gid;

    memset(attr, 0, sizeof(ibv_ah_attr));
    attr->dlid = remoteLid;
    attr->sl = 0;
    attr->src_path_bits = 0;
    attr->port_num = rdma_ctx->port;

    // attr->is_global = 0;

    // fill ah_attr with GRH
    attr->is_global = 1;
    memcpy(&attr->grh.dgid.raw, remote_gid, 16);
    attr->grh.flow_label = 0;
    attr->grh.hop_limit = 1;
    attr->grh.sgid_index = rdma_ctx->gid_index;
    attr->grh.traffic_class = 0;

}



void fillAhAttr(ibv_ah_attr* attr, uint32_t remoteLid, uint8_t* remoteGid, 
    RdmaCtx* context, uint64_t interface_id)
{

    (void)remoteGid;

    memset(attr, 0, sizeof(ibv_ah_attr));
    attr->dlid = remoteLid;
    attr->sl = 0;
    attr->src_path_bits = 0;
    attr->port_num = context->port;

    // attr->is_global = 0;

    if (remoteGid[15] != 0){
    // fill ah_attr with GRH
        // printf("GID RoCE");
        // attr->grh.dgid.global.interface_id = interface_id;
        // printf("GID RoCE %lx %lx\n", attr->grh.dgid.global.interface_id, attr->grh.dgid.global.subnet_prefix);
        attr->is_global = 1;
        memcpy(&attr->grh.dgid.raw, remoteGid, 16);
        attr->grh.flow_label = 0;
        attr->grh.hop_limit = 1;
        attr->grh.sgid_index = context->gid_index;
        attr->grh.traffic_class = 0;
    }
}

bool create_ah(ibv_ah **ah_ptr, rdma::RdmaCtx* rdma_ctx, uint32_t remote_lid, uint8_t* remote_gid) 
{   
    struct ibv_ah_attr ah_attr;
    fillAhAttr(&ah_attr, remote_lid, remote_gid, rdma_ctx);
    // printf("lid = %d\n",remote_lid);
    *ah_ptr = ibv_create_ah(rdma_ctx->pd, &ah_attr);
    if (*ah_ptr == NULL) {
        return false;
        puts("ah fail");
    }
    return true;
}

bool modifyQPtoRTR(rdma::RdmaCtx* rdma_ctx, struct ibv_qp* qp,
    uint32_t remote_qpn, uint16_t remote_lid, uint8_t* remote_gid)
{

    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;

    attr.path_mtu = IBV_MTU_4096;
    attr.dest_qp_num = remote_qpn;
    attr.rq_psn = PSN;

    fillAhAttr(&attr.ah_attr, remote_lid, remote_gid, rdma_ctx, 0);

    int flags = IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU | IBV_QP_DEST_QPN | IBV_QP_RQ_PSN;

    if (qp->qp_type == IBV_QPT_RC)
    {
        attr.max_dest_rd_atomic = 16;
        attr.min_rnr_timer = 1; // FIXME:
        flags |= IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER;
    }

    if (ibv_modify_qp(qp, &attr, flags))
    {
        LOG(ERROR) << "failed to modify QP state to RTR";
        return false;
    }
    return true;
}

bool modifyQPtoRTS(struct ibv_qp* qp)
{
    struct ibv_qp_attr attr;
    int flags;
    memset(&attr, 0, sizeof(attr));

    attr.qp_state = IBV_QPS_RTS;
    attr.sq_psn = PSN;
    flags = IBV_QP_STATE | IBV_QP_SQ_PSN;

    if (qp->qp_type == IBV_QPT_RC)
    {
        attr.timeout = 0;
        attr.retry_cnt = 7;
        attr.rnr_retry = 7;
        attr.max_rd_atomic = 16;
        // attr.alt_timeout = 7;
        flags |= IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT | IBV_QP_RNR_RETRY | IBV_QP_MAX_QP_RD_ATOMIC;
    }

    if (ibv_modify_qp(qp, &attr, flags))
    {
        LOG(ERROR) << "failed to modify QP state to RTS";
        return false;
    }
    return true;
}

bool modifyUDtoRTS(RdmaCtx* rdma_ctx, struct ibv_qp *qp) {
    // assert(qp->qp_type == IBV_QPT_UD);

    struct ibv_qp_attr attr;
    memset(&attr, 0, sizeof(attr));

    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = 0;
    attr.port_num = rdma_ctx->port;
    attr.qkey = UD_PKEY;

    if (qp->qp_type == IBV_QPT_UD) {
        if (ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_PKEY_INDEX |
                                         IBV_QP_PORT | IBV_QP_QKEY)) {
            // Debug::notifyError("Failed to modify QP state to INIT");
            return false;
        }
    } else {
        if (ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_PORT)) {
            // Debug::notifyError("Failed to modify QP state to INIT");
            return false;
        }
    }

    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTR;
    if (ibv_modify_qp(qp, &attr, IBV_QP_STATE)) {
        // Debug::notifyError("failed to modify QP state to RTR");
        return false;
    }

    memset(&attr, 0, sizeof(attr));
    attr.qp_state = IBV_QPS_RTS;
    attr.sq_psn = PSN;

    if (qp->qp_type == IBV_QPT_UD) {
        if (ibv_modify_qp(qp, &attr, IBV_QP_STATE | IBV_QP_SQ_PSN)) {
            // Debug::notifyError("failed to modify QP state to RTS");
            return false;
        }
    } else {
        if (ibv_modify_qp(qp, &attr, IBV_QP_STATE)) {
            // Debug::notifyError("failed to modify QP state to RTS");
            return false;
        }
    }
    return true;
}


bool modifyDCtoRTS(struct ibv_qp *qp, uint16_t remoteLid, uint8_t *remoteGid, RdmaCtx* rdma_ctx, uint64_t interface_id) {
    // assert(qp->qp_type == IBV_EXP_QPT_DC_INI);

    struct ibv_exp_qp_attr attr;
    memset(&attr, 0, sizeof(attr));

    attr.qp_state = IBV_QPS_INIT;
    attr.pkey_index = 0;
    attr.port_num = rdma_ctx->port;
    attr.qp_access_flags = 0;
    attr.dct_key = DCT_ACCESS_KEY;

    if (ibv_exp_modify_qp(qp, &attr, IBV_EXP_QP_STATE | IBV_EXP_QP_PKEY_INDEX |
                                         IBV_EXP_QP_PORT | IBV_EXP_QP_DC_KEY)) {
        LOG(ERROR) << ("failed to modify QP state to INI");
        return false;
    }

    attr.qp_state = IBV_QPS_RTR;
    attr.path_mtu = IBV_MTU_4096;

    fillAhAttr(&attr.ah_attr, remoteLid, remoteGid, rdma_ctx, interface_id); 
    if (ibv_exp_modify_qp(qp, &attr, IBV_EXP_QP_STATE | IBV_EXP_QP_PATH_MTU |
                                         IBV_EXP_QP_AV)) {
        LOG(ERROR) << ("failed to modify QP state to RTR");
        return false;
    }

    attr.qp_state = IBV_QPS_RTS;
    attr.timeout = 16;
    attr.retry_cnt = 7;
    attr.rnr_retry = 7;
    attr.max_rd_atomic = 16;
    if (ibv_exp_modify_qp(qp, &attr, IBV_EXP_QP_STATE | IBV_EXP_QP_TIMEOUT |
                                         IBV_EXP_QP_RETRY_CNT |
                                         IBV_EXP_QP_RNR_RETRY |
                                         IBV_EXP_QP_MAX_QP_RD_ATOMIC)) {

        LOG(ERROR) << ("failed to modify QP state to RTS");
        return false;
    }

    return true;
}

// template<int kMaxWr>


int poll_a_wc(ibv_cq* cq, struct ibv_wc* wc)
{
    // struct ibv_cq_ex x;
    int count = ibv_poll_cq(cq, 1, wc);
    if (count == 0)
    {
        return 0;
    }
    if (count < 0)
    {
        *(int*)0 = 1;
        return 0;
    }
    if (wc->status != IBV_WC_SUCCESS)
    {
        LOG(ERROR) << "Failed id=" << wc->status << ibv_wc_status_str(wc->status);
        // ("Failed status %s (%d) for wr_id %d",
        //                    ibv_wc_status_str(wc->status), wc->status,
        //                    (int)wc->wr_id);
        return -1;
    }
    else
    {
        return count;
    }
}
} // namespace rdma