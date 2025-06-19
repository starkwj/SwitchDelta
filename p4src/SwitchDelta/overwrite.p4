#include "include/header.p4"
#include "include/parser.p4"

#include <tofino/intrinsic_metadata.p4>
#include <tofino/constants.p4>
#include <tofino/stateful_alu_blackbox.p4>
// #include <src/fence.p4>

#include "src/common.p4"
#include "src/overwrite_v2.p4"
#include "src/log_read.p4"

#define ReplySwitch 10

control ingress {
    if (valid(kvmsg)) {
        apply(counter_table);
        
        if (kvmsg.flag == 2) {
            apply(timecheck);

            if (umeta.timestamp == kvmsg.timestamp) {
                apply(visibility);
                if (kvmsg.key32 == umeta.key32) {
                    // if (valid(fsmsg)) {
                        // apply(key2value0);
                        // apply(key2value1);
                        // apply(key2value2);
                        // apply(key2value3);
                        // apply(key2value4);
                        // apply(key2value5);
                        // apply(key2value6);
                        // apply(key2value7);
                        // apply(key2value8);
                        // apply(key2value9);
                        // apply(key2value10);
                        // apply(key2value11);
                        // apply(key2value12);
                        // apply(key2value13);
                        // apply(key2value14);
                        // apply(key2value15);
                        // apply(key2value16);
                        // apply(key2value17);
                        // apply(key2value18);
                        // apply(key2value19);
                        // apply(key2value20);
                        // apply(key2value21);
                        // apply(key2value22);
                        // apply(key2value23);
                    // }
                    apply(key2logid0);
                    apply(key2logid1);
                }
            }

            if (kvmsg.op == ReplySwitch) {
                apply(drop_table0);
            }

        }
        if (kvmsg.key32 != umeta.key32) // cache
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