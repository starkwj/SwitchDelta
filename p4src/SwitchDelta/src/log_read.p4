register log0 { // hash -> log_id
    width : 32; 
    instance_count : 65536; 
}

blackbox stateful_alu read_log_id_alu0 {
    reg: log0;
    condition_lo: true;

    update_lo_1_predicate: condition_lo; // if 
    update_lo_1_value: register_lo;
    update_lo_2_predicate: not condition_lo; // else 
    update_lo_2_value: register_lo; 

    output_value: alu_lo;
    output_dst: kvmsg.log_id0;
}

blackbox stateful_alu write_log_id_alu0 {
    reg: log0;
    condition_lo: true;

    update_lo_1_predicate: condition_lo; // if 
    update_lo_1_value: kvmsg.log_id0;
    update_lo_2_predicate: not condition_lo; // else 
    update_lo_2_value: kvmsg.log_id0; 
}

action read_log_id0() {
    read_log_id_alu0.execute_stateful_alu(kvmsg.key);
}

action write_log_id0() {
    write_log_id_alu0.execute_stateful_alu(kvmsg.key);
}



table key2logid0 {
    reads {
        kvmsg.op: exact;
    }
    actions {
        read_log_id0;
        write_log_id0;
        no_op_my;
    }
    default_action: no_op_my;
}

register log1 { // hash -> log_id
    width : 32; 
    instance_count : 65536; 
}

blackbox stateful_alu read_log_id_alu1 {
    reg: log1;
    condition_lo: true;

    update_lo_1_predicate: condition_lo; // if 
    update_lo_1_value: register_lo;
    update_lo_2_predicate: not condition_lo; // else 
    update_lo_2_value: register_lo; 

    output_value: alu_lo;
    output_dst: kvmsg.log_id1;
}

blackbox stateful_alu write_log_id_alu1 {
    reg: log1;
    condition_lo: true;

    update_lo_1_predicate: condition_lo; // if 
    update_lo_1_value: kvmsg.log_id1;
    update_lo_2_predicate: not condition_lo; // else 
    update_lo_2_value: kvmsg.log_id1; 
}

action read_log_id1() {
    read_log_id_alu1.execute_stateful_alu(kvmsg.key);
    modify_field(ig_intr_md_for_tm.ucast_egress_port, kvmsg.route_port); // KV MUST CHANGE FIXME:3 P4 this is not a good way to do this
    // modify_field(ig_intr_md_for_tm.mcast_grp_a, kvmsg.route_map); // secondary index MUST CHANGE
}

action write_log_id1() {
    write_log_id_alu1.execute_stateful_alu(kvmsg.key);
    // send_back 
    modify_field(ig_intr_md_for_tm.mcast_grp_a, kvmsg.route_map);
    modify_field(kvmsg.op, kEnumMultiCastPkt);
}



table key2logid1 {
    reads {
        kvmsg.op: exact;
    }
    actions {
        read_log_id1;
        write_log_id1;
        no_op_my;
    }
    default_action: no_op_my;
}

/*-----------------------------------------------------------------------------*/