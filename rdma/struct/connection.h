#pragma once

#include "node_info.h"
#include "qp_info.h"
#include "mac.h"
#include <glog/logging.h>
#include <infiniband/verbs.h>

namespace rdma
{

template<int kMaxMR>
struct RwConnect
{
    ibv_qp* qp;
    RCQPEndPoint<kMaxMR> local;
    RCQPEndPoint<kMaxMR> remote;
};

template<int kMaxQPNum, int kMaxMRNum>
class RemoteNode;

template<int kMaxNodeNum, int kMaxQPNum, int kMaxMRNum>
class ConnectGroup
{
private:
    /* data */
    RdmaCtx* rdma_ctx_;
    uint global_id_;
    
public:
    RemoteNode<kMaxQPNum, kMaxMRNum>* remote_node_; // RC 

    QPInfo* raw_qp;
    uint32_t* local_raw_qp_num;
    uint32_t* local_ud_qp_num;
    uint32_t* local_dct_qp_num;
   
    ibv_srq *srq; // RC thread local srq
    QPInfo* ud_qp_;
    QPInfo* dct_qp_; // error for ofed 5.4

    uint16_t port_num;

    ConnectGroup(/* args */);
    ~ConnectGroup();


    void setDefault(RdmaCtx* rdma_ctx) {
        rdma_ctx_ = rdma_ctx;
    }

    void set_global_id(uint global_id) {
        global_id_ = global_id;
        for (int i = 0; i < kMaxNodeNum; i++) {
            remote_node_[i].set_node_id(global_id);
        }
        return;
    }


    void initRCQP(uint16_t remote_node_id, uint16_t qp_id, CQInfo* send_cq, CQInfo* recv_cq, RdmaCtx* rdma_ctx, ibv_srq* srq = nullptr, SrqConfig* srq_config_ = nullptr) {
        auto& node_info = remote_node_[remote_node_id];
        auto& qpinfo = node_info.qp[qp_id];
        auto& qp = node_info.qp[qp_id].qpunion.qp;
        // if (srq_config_ == nullptr) {
        //     create_qp(&qp, rdma_ctx, send_cq->cq, recv_cq->cq, IBV_QPT_RC, FLAGS_qp_len, srq, qpinfo.kStaticSendBuf);
        // }
        // else {
        create_qp(&qp, rdma_ctx, send_cq->cq, recv_cq->cq, IBV_QPT_RC, srq==nullptr ? FLAGS_qp_len : 0, srq, qpinfo.kStaticSendBuf);
        // }
        node_info.qp[qp_id].send_cq = send_cq;
        node_info.qp[qp_id].recv_cq = recv_cq;
        node_info.local_qp_num[qp_id] = qp->qp_num;
        node_info.qp[qp_id].set_qp_id(qp_id);
    }

    void defaultInitRCQP(uint16_t remote_node_id, uint16_t qp_id, CQInfo* send_cq, CQInfo* recv_cq) {
        initRCQP(remote_node_id, qp_id, send_cq, recv_cq, rdma_ctx_);
    }

    void default_init_ud_qp(uint16_t qp_id, CQInfo* send_cq, CQInfo* recv_cq) {
        auto& qp = ud_qp_[qp_id].qpunion.qp;
        create_qp(&qp, rdma_ctx_, send_cq->cq, recv_cq->cq, IBV_QPT_UD);
        ud_qp_[qp_id].set_qp_id(qp_id);
        ud_qp_[qp_id].send_cq = send_cq;
        ud_qp_[qp_id].recv_cq = recv_cq;
        local_ud_qp_num[qp_id] = qp->qp_num;
        modifyUDtoRTS(rdma_ctx_, qp);
    }

    void default_init_dct(uint16_t qp_id, CQInfo* send_cq, CQInfo* recv_cq) {
        auto& qp = dct_qp_[qp_id].dct;
        createDCTarget(&qp, recv_cq->cq, rdma_ctx_);
        dct_qp_[qp_id].set_qp_id(qp_id);
        // dct_qp_[qp_id].send_cq = send_cq;
        dct_qp_[qp_id].recv_cq = recv_cq;
        local_dct_qp_num[qp_id] = qp->dct_num;

        // printf("dct qp num %d\n", qp->dct_num);

        struct ibv_exp_arm_attr aattr;
        aattr.comp_mask = 0;
        ibv_exp_arm_dct(qp, &aattr);

        // modifyUDtoRTS(rdma_ctx_, qp);
    }


    void default_init_dci(uint16_t remote_node_id, uint16_t qp_id, CQInfo* send_cq, CQInfo* recv_cq) {
        auto& node_info = remote_node_[remote_node_id];
        auto& qp = node_info.dci_qp_[qp_id].qpunion.qp;
        create_qp(&qp, rdma_ctx_, send_cq->cq, recv_cq->cq, IBV_EXP_QPT_DC_INI);
        node_info.dci_qp_[qp_id].set_qp_id(qp_id);
        node_info.dci_qp_[qp_id].send_cq = send_cq;
        // ud_qp_[qp_id].recv_cq = recv_cq;
        node_info.local_dci_qp_num[qp_id] = qp->qp_num;

        // modifyUDtoRTS(rdma_ctx_, qp);
        // modifyDCtoRTS(peer->qp, remoteMeta->lid, remoteMeta->gid, context_ptr, remoteMeta->interface_id); // DCI
    }


    // 

    QPInfo& get_rc_qp(uint16_t remote_node_id, uint16_t qp_id) {
        return remote_node_[remote_node_id].qp[qp_id];
    }

    QPInfo& get_raw_qp(uint16_t qp_id) {
        return raw_qp[qp_id];
    }

    QPInfo& get_ud_qp(uint16_t qp_id) {
        return ud_qp_[qp_id];
    }


    QPInfo& get_dct_qp(uint16_t qp_id) {
        return dct_qp_[qp_id];
    }

    QPInfo& get_dci_qp(uint16_t remote_node_id, uint16_t qp_id) {
        return remote_node_[remote_node_id].dci_qp_[qp_id];
    }
    


    void default_init_raw_qp(uint16_t qp_id, CQInfo* send_cq, CQInfo* recv_cq, std::string nic_name, uint16_t my_port, uint16_t remote_port) {
        auto& qp = raw_qp[qp_id].qpunion.qp;
        create_qp(&qp, rdma_ctx_, send_cq->cq, recv_cq->cq, IBV_QPT_RAW_PACKET);
        raw_qp[qp_id].set_qp_id(qp_id);
        raw_qp[qp_id].send_cq = send_cq;
        raw_qp[qp_id].recv_cq = recv_cq;
        local_raw_qp_num[qp_id] = qp->qp_num;
        modifyUDtoRTS(rdma_ctx_, qp);

        // steering
        auto mac = (uint8_t*)getMac(nic_name.c_str());
        memcpy((void*)raw_qp[qp_id].mac, (void*)mac, 6);
        raw_qp[qp_id].src_port = remote_port;
        raw_qp[qp_id].my_port = my_port;
        // printf("steering %u\n", toBigEndian16(my_port));
        if (FLAGS_port_func == false) {
            steeringWithMacUdp(qp, rdma_ctx_, (uint8_t*)raw_qp[qp_id].mac, toBigEndian16(my_port), toBigEndian16(remote_port));
            // steeringWithMacUdp(qp, rdma_ctx_, (uint8_t*)raw_qp[qp_id].mac, toBigEndian16(my_port + 16), toBigEndian16(remote_port), 0xFFFF);
        }
        else {
            assert(my_port <= 15);
            for (uint16_t i = 0; i < (1 << port_num); i++) {
                if ((i & (1u << my_port)) == (1u << my_port))
                    steeringWithMacUdp(qp, rdma_ctx_, (uint8_t*)raw_qp[qp_id].mac,
                        toBigEndian16(i), toBigEndian16(remote_port));
            }
            raw_qp[qp_id].steering_port = toBigEndian16(1u << my_port);

            // steeringWithMacUdp(qp, rdma_ctx_, (uint8_t*)raw_qp[qp_id].mac,
            //     toBigEndian16(1u << my_port),
            //     toBigEndian16(remote_port),
            //     toBigEndian16(1u << my_port), my_port);
            printf("myport=%d, port = %x\n", my_port, toBigEndian16(1u << my_port));
            //  QUERY_HCA_CAP()
        }

    }

    void publish_node_info() {
        std::string set_key = setKey(global_id_, global_id_, "id");
        auto& id = rdma_ctx_->nic_id;
        Memcached::get_instance().memcachedSet(set_key, (char*)&id, sizeof(id), global_id_);
    }

    void publish_ud_connect_info() {
        std::string set_key = setKey(global_id_, global_id_, "udqpn");
        // printf("qpn = %d\n", local_ud_qp_num[0]);
        Memcached::get_instance().memcachedSet(set_key, (char*)local_ud_qp_num, sizeof(local_ud_qp_num), global_id_);
    }

    void publish_dct_connect_info() {
        std::string set_key = setKey(global_id_, global_id_, "dctqpn");
        // printf("qpn = %d\n", local_ud_qp_num[0]);
        Memcached::get_instance().memcachedSet(set_key, (char*)local_dct_qp_num, sizeof(local_dct_qp_num), global_id_);
    }

    // void publish_dci_connect_info() {
    //     std::string set_key = setKey(global_id_, global_id_, "dctqpn");
    //     // printf("qpn = %d\n", local_ud_qp_num[0]);
    //     Memcached::get_instance().memcachedSet(set_key, (char*)local_dci_qp_num, sizeof(local_dci_qp_num), global_id_);
    // }

    void subscribe_ud_connect_info(uint remote_node_id) { // + change state
        if (remote_node_id == global_id_)
            return;
        remote_node_[remote_node_id].subscribe_ud_connect_info(remote_node_id);
        remote_node_[remote_node_id].set_remote_ud_qp_state(rdma_ctx_);
    }

    void subscribe_dct_connect_info(uint remote_node_id) { // + dci 
        if (remote_node_id == global_id_)
            return;
        remote_node_[remote_node_id].subscribe_dct_connect_info(remote_node_id);
        remote_node_[remote_node_id].set_remote_ud_qp_state(rdma_ctx_); // same as ud
        remote_node_[remote_node_id].modify_dci_state(rdma_ctx_);

        // printf("get remote=%ld\n", remote_node_[remote_node_id].remote_dct_qp_num[0]);
    }


    void subscribe_node_info(uint remote_node_id) {
        if (remote_node_id == global_id_)
            return;
        remote_node_[remote_node_id].subscribe_node_info(remote_node_id);
    }

    void subscribe_connect_info(uint remote_node_id, int extra_flag = 0) {
        if (remote_node_id == global_id_)
            return;
        remote_node_[remote_node_id].subscribe_connect_info(remote_node_id, extra_flag);
    }

    void publish_connect_info(uint remote_node_id, int extra_flag = 0) {
        if (remote_node_id == global_id_)
            return;
        remote_node_[remote_node_id].publish_connect_info(remote_node_id, extra_flag);
    }

    void subscribe_mrs(uint remote_node_id) {
        if (remote_node_id == global_id_)
            return;
        remote_node_[remote_node_id].remote_mr.subscribe_mrs(remote_node_id);
        #ifdef LOGDEBUG
        printf("remote = %lx\n", remote_node_[remote_node_id].remote_mr.get_addr(0));
        #endif
    }

    void modify_rcqp_state(uint remote_node_id) {
        if (remote_node_id == global_id_)
            return;
        remote_node_[remote_node_id].modify_qp_state(rdma_ctx_);
    }

    void subscribe_nic_rcqp_mr(uint remote_node_id) { // + change state
        // subscribe_node_info(remote_node_id);
        if (remote_node_id == global_id_)
            return;
        subscribe_mrs(remote_node_id);
        subscribe_connect_info(remote_node_id);
        modify_rcqp_state(remote_node_id);
    }

    void publish_barrier_info() {
        std::string set_key = setKey(global_id_, global_id_, "barrier");
        Memcached::get_instance().memcachedSet(set_key, (char*)&global_id_, sizeof(global_id_), global_id_);
    }

    void p2p_barrier(uint remote_node_id) {
        if (remote_node_id == global_id_)
            return;
        std::string get_key = getKey(remote_node_id, remote_node_id, "barrier");
        uint ret;
        Memcached::get_instance().memcachedGet(get_key, (char*)&ret, sizeof(ret), global_id_);
    }
};

template<int kMaxNodeNum, int kMaxQPNum, int kMaxMRNum>
ConnectGroup<kMaxNodeNum, kMaxQPNum, kMaxMRNum>::ConnectGroup(/* args */)
{
    
    local_raw_qp_num = new uint32_t[kMaxQPNum];
    local_ud_qp_num = new uint32_t[kMaxQPNum];
    local_dct_qp_num = new uint32_t[kMaxQPNum];
    remote_node_ = new RemoteNode<kMaxQPNum, kMaxMRNum>[kMaxNodeNum]; 
    ud_qp_ = new QPInfo[kMaxQPNum];
    dct_qp_ = new QPInfo[kMaxQPNum];
    raw_qp = new QPInfo[kMaxQPNum];
    memset(local_raw_qp_num, 0, kMaxQPNum * sizeof(uint32_t));
    memset(local_ud_qp_num, 0, kMaxQPNum * sizeof(uint32_t));
    memset(local_dct_qp_num, 0, kMaxQPNum * sizeof(uint32_t)); 
}

template<int kMaxNodeNum, int kMaxQPNum, int kMaxMRNum>
ConnectGroup<kMaxNodeNum, kMaxQPNum, kMaxMRNum>::~ConnectGroup()
{
}

} // rdma