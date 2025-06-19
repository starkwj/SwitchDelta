#include "include/header.p4"
#include "include/parser.p4"

#include <tofino/intrinsic_metadata.p4>
#include <tofino/constants.p4>
#include <tofino/stateful_alu_blackbox.p4>
// #include <src/fence.p4>

#include "src/common.p4"
// #include "src/cache.p4"
#include "src/visible_fw.p4"
// #include "src/log_readL.p4"

#define ReplySwitch 10

control ingress {
    if (valid(kvmsg)) {
        apply(counter_table);
        if (kvmsg.flag == 2) {
            apply(timecheck);
            if (umeta.timestamp == kvmsg.timestamp) {
                apply(visibility);
                if (kvmsg.key32 == umeta.key32) {

                    apply(key2logid0);
                    apply(key2logid1);
                }
                if (kvmsg.op == ReplySwitch) {
                    apply(drop_table0);
                }
            }
        }
        if (kvmsg.key32 != umeta.key32) // drop the code
            apply(udp_route);
    }
    else {
        apply(ipv4_route);
    }
}

control egress {
    apply(ethernet_set_mac);
    if (valid(kvmsg) and kvmsg.flag == 2) {
        apply(fix_port);
    }
}