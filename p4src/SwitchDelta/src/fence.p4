
// op 操作类型
// order_id  
// fence_id

#define order_register(name) \
register done_flag_##name { \ 
    width : 16; \
    instance_count : 65536; \ 
} 

#define order_action_group(name, bitmap) \
blackbox stateful_alu last_reply_box {\
    reg: done_flag_##name; \
    condition_lo: true; \
    update_lo_1_predicate: condition_lo; \ // if 
    update_lo_1_value: 1; \
    update_lo_2_predicate: not condition_lo; \ // else 
    update_lo_2_value: 1; \
    output_value: alu_lo; \
    initial_register_lo_value: 1; \ 
} \
blackbox stateful_alu req_box {\
    reg: done_flag_##name; \
    condition_lo: umeta.blocked == 0; \
    update_lo_1_predicate: condition_lo; \ // if 
    update_lo_1_value: 2; \
    update_lo_2_predicate: not condition_lo; \ // else 
    update_lo_2_value: 0; \
    output_value: alu_lo; \
} \
blackbox stateful_alu req_reply_after_box {\
    reg: done_flag_##name; \
    condition_hi: register_lo == 3; \
    update_hi_1_predicate: condition_lo; \ // if 
    update_hi_1_value: umeta.blocked ; \
    update_hi_2_predicate: not condition_lo; \ // else 
    update_hi_2_value: umeta.blocked | bitmap; \
    output_value: alu_hi; \
    output_dst: umeta.blocked; \
} \
blackbox stateful_alu reply_before_box() {\
    reg: done_flag_##name; \
    condition_hi: register_lo | umeta.blocked == 0; \
    update_lo_1_predicate: condition_lo; \ // if 
    update_lo_1_value: 2; \
    update_lo_2_predicate: not condition_lo; \ // else 
    update_lo_2_value: register_lo; \
    update_hi_1_predicate: condition_lo; \ // if 
    update_hi_1_value: umeta.blocked; \
    update_hi_2_predicate: not condition_lo; \ // else 
    update_hi_2_value: bitmap; \
    output_value: alu_hi; \
    output_dst: umeta.blocked; \
} \
blackbox stateful_alu reply_my_box() {\
    reg: done_flag_##name; \
    condition_hi: true; \
    update_lo_1_predicate: condition_lo; \ // if 
    update_lo_1_value: 3; \
    update_lo_2_predicate: not condition_lo; \ // else 
    update_lo_2_value: 3; \
} \
action req() { \
    req_box.execute_stateful_alu(kvmsg.small_rpc_id); \
} \
action req_reply_after() { \
    req_reply_after_box.execute_stateful_alu(kvmsg.small_rpc_id); \
} \
action reply_before() { \
    reply_before_box.execute_stateful_alu(kvmsg.small_rpc_id); \
} \
action reply_my() { \
    reply_my_box.execute_stateful_alu(kvmsg.small_rpc_id); \
} \
action last_reply() { \
    last_reply_box.execute_stateful_alu(kvmsg.small_rpc_id); \
} \
action order_nop() { \
    no_op(); \
} 

 
#define order_table(name) \
table visibility_##name { \
    reads { \ 
        kvmsg.op: exact; \
        kvmsg.order_id: exact; \
        kvmsg.fence_id: exact; \
    } \
    actions { \
        req; \
        req_reply_after; \
        reply_before; \
        reply_my; \ 
        last_reply; \
        order_nop; \
    }   \
}


order_register(0)
order_register(1)
order_action_group(0, 1)
order_action_group(1, 2)
order_table(0)
order_table(1)


