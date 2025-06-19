
action set_port_action(port) {
    modify_field(ig_intr_md_for_tm.ucast_egress_port, port);
}


table udp_route{
    reads {
        kvmsg.node_id : exact;
    }
    actions {
        set_port_action;
    }
}


action set_egr(port) {
    modify_field(ig_intr_md_for_tm.ucast_egress_port, port);
    add_to_field(ipv4.ttl, -1);
}
table ipv4_route {
    reads {
        ipv4.dstAddr : exact;
    }
    actions {
        set_egr;
    }
}

action ethernet_set_mac_act(dmac) {
    modify_field(ethernet.dstAddr, dmac);
    // modify_field(TXmessage.send_id,eg_intr_md.egress_qid);
}
table ethernet_set_mac {
    reads {
        eg_intr_md.egress_port: exact;
    }
    actions {
        ethernet_set_mac_act;
    }
}

action _drop() {
    drop();
}

action _nop() {
    no_op();
}

table drop_table1 {
    actions {
        _drop;
    }
    default_action: _drop;
}

table drop_table0 {
    actions {
        _drop;
    }
    default_action: _drop;
}

table drop_table2 {
    actions {
        _drop;
    }
    default_action: _drop;
}

table drop_table3 {
    actions {
        _drop;
    }
    default_action: _drop;
}

table drop_table4 {
    actions {
        _drop;
    }
    default_action: _drop;
}