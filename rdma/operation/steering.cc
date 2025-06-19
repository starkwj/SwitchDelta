#include "operation.h"

namespace rdma
{
void steeringWithMacUdp(ibv_qp *qp, rdma::RdmaCtx *ctx, const uint8_t mac[6], 
        uint16_t dstPort, uint16_t srcPort, uint16_t dst_port_mask, uint16_t priority) {

    struct raw_eth_flow_attr {
        struct ibv_flow_attr        attr;
        struct ibv_flow_spec_eth    spec_eth;
        struct ibv_flow_spec_tcp_udp spec_udp;
    } flow_attr = {
        .attr = {
            .comp_mask  = 0,
            .type       = IBV_FLOW_ATTR_NORMAL,
            .size       = sizeof(flow_attr),
            .priority   = priority,
            .num_of_specs  = 2,
            .port       = ctx->port,
            .flags      = (uint32_t) ((dst_port_mask == 0xFFFFu) ? 0 : IBV_FLOW_ATTR_FLAGS_DONT_TRAP),
            // .flags = IBV_FLOW_ATTR_FLAGS_DONT_TRAP,
        },
        .spec_eth = {
            .type   = IBV_FLOW_SPEC_ETH,
            .size   = sizeof(struct ibv_flow_spec_eth),
            .val = {
                .dst_mac = {mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]},
                .src_mac = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                .ether_type = 0,
                .vlan_tag = 0,
            },
            .mask = {
                .dst_mac = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
                .src_mac = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                .ether_type = 0,
                .vlan_tag = 0,
            }
        },
        .spec_udp = {
            .type = IBV_FLOW_SPEC_UDP,
            .size = sizeof(struct ibv_flow_spec_tcp_udp),
            .val = {
                .dst_port = dstPort,
                .src_port = srcPort,
            },
            .mask = {
                .dst_port = dst_port_mask,
                .src_port = 0xFFFF,
            }
        },
    };
    struct ibv_flow *eth_flow;
    printf("steering %x %x\n", dstPort, dst_port_mask);
    /* create steering rule */
    eth_flow = ibv_create_flow(qp, &flow_attr.attr);
    if (!eth_flow) {
        LOG(ERROR) << ("Couldn't attach steering flow");
    }
}
/*
void steeringWithMacUdp_exp(ibv_qp *qp, rdma::RdmaCtx *ctx, const uint8_t mac[6], 
        uint16_t dstPort, uint16_t srcPort, uint16_t dst_port_mask) {

    struct raw_eth_flow_attr {
        struct ibv_exp_flow_attr        attr;
        struct ibv_exp_flow_spec_eth    spec_eth;
        struct ibv_exp_flow_spec_tcp_udp spec_udp;
    } __attribute__((packed)) flow_attr = {
        .attr = {
            // .comp_mask  = 0,
            .type       = IBV_EXP_FLOW_ATTR_NORMAL,
            .size       = sizeof(flow_attr),
            .priority   = 0,
            .num_of_specs  = 2,
            .port       = ctx->port,
            .flags      = 0,
        },
        .spec_eth = {
            .type   = IBV_EXP_FLOW_SPEC_ETH,
            .size   = sizeof(struct ibv_exp_flow_spec_eth),
            .val = {
                .dst_mac = {mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]},
                .src_mac = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                .ether_type = 0,
                .vlan_tag = 0,
            },
            .mask = {
                .dst_mac = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF},
                .src_mac = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00},
                .ether_type = 0,
                .vlan_tag = 0,
            }
        },
        .spec_udp = {
            .type = IBV_EXP_FLOW_SPEC_UDP,
            .size = sizeof(struct ibv_exp_flow_spec_tcp_udp),
            .val = {
                .dst_port = dstPort,
                .src_port = srcPort,
            },
            .mask = {
                .dst_port = dst_port_mask,
                .src_port = 0xFFFF,
            }
        },
    };
    struct ibv_exp_flow *eth_flow;
    
    puts("exp");
    eth_flow = ibv_exp_create_flow(qp, &flow_attr.attr);
    if (!eth_flow) {
        LOG(ERROR) << ("Couldn't attach steering flow");
    }
}
*/

}