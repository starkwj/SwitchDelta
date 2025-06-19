#pragma once
#include "common.h"
#include "memory_region.h"
#include "operation.h"
#include "size_def.h"
#include "qp_info.h"
#include <string>
// #include "work_request.h"

namespace rdma {

template<int kMaxQPNum, int kMaxMRNum>
class RemoteNode {
private:
    /* node info data */
    /* RC QP */
    /* DCT info */

public:
    // local
    QPInfo* qp;
    QPInfo* dci_qp_;

    // FIXME: SRQ hear!!!!!!!!!!!!!!!!!!

    uint32_t* local_dci_qp_num;
    uint32_t* local_qp_num;
    
    uint16_t global_id_;
    bool valid{ false };
    bool ud_valid { false };
    bool dct_valid { false };

    // remote info
    NodeInfo remote_nic_id;
    RegionArr<kMaxMRNum> remote_mr;
    uint32_t* remote_qp_num;

    // ud

    AHInfo remote_ah;
    uint32_t* remote_ud_qp_num;
    uint32_t* remote_dct_qp_num;


    RemoteNode(/* args */) {
        qp = new QPInfo[kMaxQPNum];
        dci_qp_ = new QPInfo[kMaxQPNum];
        
        local_dci_qp_num = new uint32_t[kMaxQPNum];
        memset(local_dci_qp_num, 0, sizeof(uint32_t) * kMaxQPNum);
        
        local_qp_num = new uint32_t[kMaxQPNum];
        memset(local_qp_num, 0, sizeof(uint32_t) * kMaxQPNum);
        
        remote_qp_num = new uint32_t[kMaxQPNum];
        memset(remote_qp_num, 0, sizeof(uint32_t) * kMaxQPNum);
        
        remote_ud_qp_num = new uint32_t[kMaxQPNum];
        memset(remote_ud_qp_num, 0, sizeof(uint32_t) * kMaxQPNum);

        remote_dct_qp_num = new uint32_t[kMaxQPNum];
        memset(remote_dct_qp_num, 0, sizeof(uint32_t) * kMaxQPNum);
    }
    
    ~RemoteNode() {
    }

    void set_node_id(uint global_id) {
        global_id_ = global_id;
    }

    void publish_connect_info(uint remote_node_id, int extra_flag) {
        std::string s = "";
        if (extra_flag != 0)
            s = std::to_string(extra_flag)+"qp_num";
        std::string set_key = setKey(global_id_, remote_node_id, "qp_num" + s);
        Memcached::get_instance().memcachedSet(set_key, (char*)local_qp_num, sizeof(local_qp_num), global_id_);
    }

    void subscribe_connect_info(uint remote_node_id, int extra_flag) {
        std::string s = "";
        if (extra_flag != 0)
            s = std::to_string(extra_flag)+"qp_num";
        std::string get_key = getKey(global_id_, remote_node_id, "qp_num" + s);
        Memcached::get_instance().memcachedGet(get_key, (char*)remote_qp_num, sizeof(remote_qp_num), global_id_);
    }

    void subscribe_node_info(uint remote_node_id) {
        std::string get_key = getKey(remote_node_id, remote_node_id, "id");
        Memcached::get_instance().memcachedGet(get_key, (char*)&remote_nic_id, sizeof(remote_nic_id), global_id_);
    }

    void subscribe_ud_connect_info(uint remote_node_id) {
        std::string get_key = getKey(remote_node_id, remote_node_id, "udqpn");
        Memcached::get_instance().memcachedGet(get_key, (char*)remote_ud_qp_num, sizeof(remote_ud_qp_num), global_id_);
    }

    void subscribe_dct_connect_info(uint remote_node_id) {
        std::string get_key = getKey(remote_node_id, remote_node_id, "dctqpn");
        Memcached::get_instance().memcachedGet(get_key, (char*)remote_dct_qp_num, sizeof(remote_dct_qp_num), global_id_);
    }

    void modify_qp_state(RdmaCtx* rdma_ctx) {
        for (int j = 0; j < kMaxQPNum; j++) {
            if (qp[j].qpunion.qp == nullptr)
                continue;
            rdma::modifyQPtoInit(rdma_ctx, qp[j].qpunion.qp);
            rdma::modifyQPtoRTR(rdma_ctx, qp[j].qpunion.qp, remote_qp_num[j], remote_nic_id.lid, remote_nic_id.gid.raw);
            rdma::modifyQPtoRTS(qp[j].qpunion.qp);
        }
    }

    void modify_dci_state(RdmaCtx* rdma_ctx) {
        for (int j = 0; j < kMaxQPNum; j++) {
            rdma::modifyDCtoRTS(dci_qp_[j].qpunion.qp, remote_nic_id.lid, remote_nic_id.gid.raw, rdma_ctx, remote_nic_id.gid.global.interface_id); // DCI
        }
    }

    void set_remote_ud_qp_state(RdmaCtx* rdma_ctx) {
        if (remote_ah.ah)
            return;
        create_ah(&remote_ah.ah, rdma_ctx, remote_nic_id.lid, remote_nic_id.gid.raw);
        return;
    }
};
}