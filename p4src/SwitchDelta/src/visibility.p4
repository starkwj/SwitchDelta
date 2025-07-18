register key_check { // hash -> key
    width : 64; 
    instance_count : 65536; 
}

register timestamp1 {
    width : 32;
    instance_count : 65536;
}

#define kEnumMultiCastPkt 21
#define ReplySwitch 10
#define kEnumUpdateIndexReq 11
#define kEnumUpdateIndexReply 12
#define kEnumReadIndexReq 13
#define kEnumReadIndexReply 14

blackbox stateful_alu write_data_reply {
    reg: key_check;
    condition_lo: register_lo == 0;
    condition_hi : register_lo == kvmsg.key32; // 

    update_lo_1_predicate: condition_lo; // if 
    update_lo_1_value: kvmsg.key32;
    update_lo_2_predicate: not condition_lo; // else 
    update_lo_2_value: register_lo;
    
    update_hi_1_predicate: condition_lo;
    update_hi_1_value: kvmsg.rpc_id;
    update_hi_2_predicate: condition_hi;
    update_hi_2_value: kvmsg.rpc_id;

    output_value: alu_lo;
    output_dst: umeta.key32;
    initial_register_lo_value: 0;
    initial_register_hi_value: 0;
}

blackbox stateful_alu write_index_reply {
    reg: key_check;
    condition_lo: kvmsg.rpc_id == register_hi;
    // condition_hi : register_lo == 0; // 

    update_lo_1_predicate: condition_lo; // if 
    update_lo_1_value: 0;
    update_lo_2_predicate: not condition_lo; // else 
    update_lo_2_value: register_lo; 

    update_hi_1_predicate: condition_lo; // if 
    update_hi_1_value: 0;
    update_hi_2_predicate: not condition_lo; // else 
    update_hi_2_value: register_hi;

    initial_register_lo_value: 0;
    initial_register_hi_value: 0;
}

blackbox stateful_alu check_index_msg {
    reg: key_check;
    condition_lo: register_lo == kvmsg.key32;

    update_lo_1_predicate: condition_lo; // if 
    update_lo_1_value: register_lo;
    update_lo_2_predicate: not condition_lo; // else 
    update_lo_2_value: register_lo; 

    output_value: alu_lo;
    output_dst: umeta.key32;
}


action new_index() { // data reply + index req
    write_data_reply.execute_stateful_alu(kvmsg.key);
}

action delete_index() {
    write_index_reply.execute_stateful_alu(kvmsg.key);
}

action check_index() {
    // register_read(); // how to read only low-32-bit ? 
    check_index_msg.execute_stateful_alu(kvmsg.key);
}

action no_op_my() {
    no_op();
}

table visibility {
    reads {
        kvmsg.op: exact;
    }
    actions {
        new_index; 
        delete_index;
        check_index;
        no_op_my;
    }
    default_action: no_op_my;
}


/*--------------------------------------------------*/
register log { // hash -> log_id
    width : 64; 
    instance_count : 65536; 
}

blackbox stateful_alu read_log_id_alu {
    reg: log;
    condition_lo: true;

    update_lo_1_predicate: condition_lo; // if 
    update_lo_1_value: register_lo;
    update_lo_2_predicate: not condition_lo; // else 
    update_lo_2_value: register_lo; 

    output_value: alu_lo;
    output_dst: kvmsg.log_id;
}

blackbox stateful_alu write_log_id_alu {
    reg: log;
    condition_lo: true;

    update_lo_1_predicate: condition_lo; // if 
    update_lo_1_value: kvmsg.log_id;
    update_lo_2_predicate: not condition_lo; // else 
    update_lo_2_value: kvmsg.log_id; 
}

action read_log_id() {
    read_log_id_alu.execute_stateful_alu(kvmsg.key);
    // reply to client 
    // modify_field(ig_intr_md_for_tm.mcast_grp_a, TXmessage.broadcastServer); 
    modify_field(ig_intr_md_for_tm.ucast_egress_port, kvmsg.route_port);
}

action write_log_id() {
    write_log_id_alu.execute_stateful_alu(kvmsg.key);
    // send_back 
    modify_field(ig_intr_md_for_tm.mcast_grp_a, kvmsg.route_map);
    modify_field(kvmsg.op, kEnumMultiCastPkt);
}


table key2logid {
    reads {
        kvmsg.op: exact;
    }

    actions {
        read_log_id;
        write_log_id;
        no_op_my;
    }
    default_action: no_op_my;
}

action send_to_index() {
    modify_field(udp.dstPort, kvmsg.index_port); // load balance for index server
}

action send_to_client() {
    modify_field(udp.dstPort, kvmsg.client_port);
}

table fix_port {
    reads {
        eg_intr_md.egress_port: exact;
        kvmsg.op: exact;
    }

    actions {
        send_to_index;
        send_to_client;
        no_op_my;
    }
    default_action: no_op_my;
}

/*--------------------------------------------------*/
register index_counter { // hash -> log_id
    width : 16; 
    instance_count : 48; 
}

blackbox stateful_alu counter_r {
    reg: index_counter;
    condition_lo: true;

    update_lo_1_predicate: condition_lo; // if 
    update_lo_1_value: register_lo;
    update_lo_2_predicate: not condition_lo; // else 
    update_lo_2_value: register_lo; 

    initial_register_lo_value: 0;
    output_value: alu_lo;
    output_dst: kvmsg.count;
}

blackbox stateful_alu counter_sub_alu {  
    reg: index_counter;
    condition_lo: true;

    update_lo_1_predicate: condition_lo; // if 
    update_lo_1_value: register_lo - 1;
    update_lo_2_predicate: not condition_lo; // else 
    update_lo_2_value: register_lo - 1; 

    initial_register_lo_value: 0;
    output_value: alu_lo;
    output_dst: kvmsg.count;
}

blackbox stateful_alu counter_add_alu {  
    reg: index_counter;
    condition_lo: true;

    update_lo_1_predicate: condition_lo; // if 
    update_lo_1_value: register_lo + 1;
    update_lo_2_predicate: not condition_lo; // else 
    update_lo_2_value: register_lo + 1; 

    initial_register_lo_value: 0;
    output_value: alu_lo;
    output_dst: kvmsg.count;
}

action counter_read(id) {
    counter_r.execute_stateful_alu(id);
}

action counter_add(id) {
    counter_add_alu.execute_stateful_alu(id);
}

action counter_sub(id) {
    counter_sub_alu.execute_stateful_alu(id);
}


table counter_table {
    reads {
        kvmsg.op: exact;
        kvmsg.index_port: exact;
    }
    actions {
        counter_read;
        counter_add;
        counter_sub;
        no_op_my;
    }  
    default_action: no_op_my;
}
