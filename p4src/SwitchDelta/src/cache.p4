#define FIRSTREQ 1
#define DROP 2

register key2port {
    width : 16; 
    instance_count : 65536; 
}

#define kEnumReadReq 0
#define kEnumReadReply 2
#define kEnumWriteAndRead 23
#define kEnumWriteReplyWithData 24

#define kReq 0
#define kReply 1



blackbox stateful_alu key2port_box{
    reg : key2port;
    condition_lo : umeta.req_reply == kReq;
    // condition_hi : register_lo == 0; // first packet

    update_lo_1_predicate: condition_lo; // if 
    update_lo_1_value : register_lo | kvmsg.q_map;
    update_lo_2_predicate: not condition_lo; // else 
    update_lo_2_value : 0;

    update_hi_1_predicate: condition_lo; // if 
    update_hi_1_value : register_lo | kvmsg.q_map;
    update_hi_2_predicate: not condition_lo; // else 
    update_hi_2_value : register_lo | kvmsg.q_map;

    output_value: alu_hi;
    output_dst : umeta.q_map;
    initial_register_lo_value : 0;
}



action dedup() {
    // is used for read and read-reply, 
    key2port_box.execute_stateful_alu(kvmsg.key);
}


table cache {
    actions {
        dedup;
    }
    default_action: dedup;
}

register coro_id_r1 {
    width : 32; 
    instance_count : 65536;
}

register coro_id_r2 {
    width : 32; 
    instance_count : 65536;
}

blackbox stateful_alu coro_id_box1 {
    reg : coro_id_r1;
    condition_lo : umeta.req_reply == kReq;

    update_lo_1_predicate: condition_lo; // if 
    update_lo_1_value : register_lo | kvmsg.coro_id_1;
    update_lo_2_predicate: not condition_lo; // else 
    update_lo_2_value : 0;

    update_hi_1_predicate: condition_hi; // if 
    update_hi_1_value : register_lo;
    update_hi_2_predicate: not condition_hi; // else 
    update_hi_2_value : register_lo;

    output_value: alu_hi;
    output_dst : kvmsg.coro_id_1;
    initial_register_lo_value : 0;
}

blackbox stateful_alu coro_id_box2 {
    reg : coro_id_r2;
    condition_lo : umeta.req_reply == kReq;

    update_lo_1_predicate: condition_lo; // if 
    update_lo_1_value : register_lo | kvmsg.coro_id_2;
    update_lo_2_predicate: not condition_lo; // else 
    update_lo_2_value : 0;

    update_hi_1_predicate: condition_hi; // if 
    update_hi_1_value : register_lo;
    update_hi_2_predicate: not condition_hi; // else 
    update_hi_2_value : register_lo;

    output_value: alu_hi;
    output_dst : kvmsg.coro_id_2;
    initial_register_lo_value : 0;
}


action coro_id_a1() {
    coro_id_box1.execute_stateful_alu(kvmsg.key);
}
action coro_id_a2() {
    coro_id_box2.execute_stateful_alu(kvmsg.key);
}

table coro_id_t1 {
    actions {
        coro_id_a1;
    }
    default_action: coro_id_a1;
}

table coro_id_t2 {
    actions {
        coro_id_a2;
    }
    default_action: coro_id_a2;
}


action copy_port_a() {
    modify_field(udp.dstPort, umeta.q_map);
    modify_field(kvmsg.s_map, 666);
}

table copy_port_t {
    actions {
        copy_port_a;
    }
    default_action: copy_port_a;
}

action test_a() {
    modify_field(kvmsg.s_map, umeta.q_map);
}

table test {
    actions {
        test_a;
    }
    default_action: test_a;
}

 
register dedup_flag {
    width: 16;
    instance_count : 65536;
}


blackbox stateful_alu cache_req_box {
    reg: dedup_flag;
    condition_lo: true;
 
    update_lo_1_predicate: condition_lo; // if 
    update_lo_1_value : register_lo;
    update_lo_2_predicate: not condition_lo; // else 
    update_lo_2_value : register_lo;
 
    output_value: register_lo;
    output_dst : kvmsg.magic_in_switch; // drop or route
    initial_register_lo_value : 0; 
}
action cache_read_req() {
    cache_req_box.execute_stateful_alu(kvmsg.key); 
    modify_field(umeta.req_reply, 0);
}

blackbox stateful_alu cache_write_req_box {
    reg: dedup_flag;
    condition_lo: true;
 
    update_lo_1_predicate: condition_lo; // if 
    update_lo_1_value : register_lo | 0x1;
    update_lo_2_predicate: not condition_lo; // else 
    update_lo_2_value : register_lo | 0x1;

    update_hi_1_predicate: condition_lo; // if 
    update_hi_1_value : register_lo; 
    update_hi_2_predicate: not condition_lo; // else 
    update_hi_2_value : register_lo; // drop

    output_value: alu_hi;
    output_dst : kvmsg.magic_in_switch;
    initial_register_lo_value : 0; 

}
action cache_write_req() {
    cache_write_req_box.execute_stateful_alu(kvmsg.key); 
    modify_field(umeta.req_reply,0);
}


blackbox stateful_alu cache_write_reply_box {
    reg : dedup_flag;
    condition_lo : true;

    update_lo_1_predicate: condition_lo; // if 
    update_lo_1_value : register_lo + 1;
    update_lo_2_predicate: not condition_lo; // else 
    update_lo_2_value : register_lo + 1;

    initial_register_lo_value : 0;
}
action cache_write_reply() {
    cache_write_reply_box.execute_stateful_alu(kvmsg.key); 
    modify_field(umeta.req_reply,1);
}

blackbox stateful_alu cache_read_reply_box {
    reg: dedup_flag;
    condition_lo: register_lo == kvmsg.magic_in_switch;
 
    update_lo_1_predicate: condition_lo; // if 
    update_lo_1_value : register_lo + 2;
    update_lo_2_predicate: not condition_lo; // else 
    update_lo_2_value : register_lo;

    update_hi_1_predicate: condition_lo; // if 
    update_hi_1_value : 1; 
    update_hi_2_predicate: not condition_lo; // else 
    update_hi_2_value : 0; // drop

    output_value: alu_hi;
    output_dst : umeta.op;
    initial_register_lo_value : 0; 
}

action cache_read_reply() {
    cache_read_reply_box.execute_stateful_alu(kvmsg.key);
    modify_field(umeta.req_reply,1);
}

// To support for write op
table cache_check {
    reads {
        kvmsg.op: exact;
    }
    actions {
        cache_write_reply;
        cache_read_reply;
        cache_read_req;
        cache_write_req;
        _nop;
    }
    default_action: _nop;
}

action bit_or_0x1() {
    bit_and(umeta.already_write, kvmsg.magic_in_switch, 0x1);
}
table get_the_lowest_bit {
    actions {
        bit_or_0x1;
    }
    default_action: bit_or_0x1;
}


table cache_op {
    reads {
        umeta.already_write: exact;
    }
    actions {
        _drop;
        copy_port_a;
        _nop;    
    }

    default_action: _nop;
}