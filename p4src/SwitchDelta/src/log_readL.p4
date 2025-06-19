
#define value_store(id) register data##id { \
    width : 32; \
    instance_count : 65536; \
} \
blackbox stateful_alu read_fs_alu##id { \
    reg: data##id; \
    condition_lo: true; \
    update_lo_1_predicate: condition_lo; \
    update_lo_1_value: register_lo; \
    update_lo_2_predicate: not condition_lo; \
    update_lo_2_value: register_lo; \
    output_value: alu_lo; \
    output_dst: fsmsg.value##id; \
} \
blackbox stateful_alu write_fs_alu##id { \
    reg: data##id; \
    condition_lo: true; \
    update_lo_1_predicate: condition_lo;  \
    update_lo_1_value: fsmsg.value##id; \
    update_lo_2_predicate: not condition_lo; \
    update_lo_2_value: fsmsg.value##id; \
} \
action read_fs##id() { \
    read_fs_alu##id.execute_stateful_alu(kvmsg.key); \
} \
action write_fs##id() { \
    write_fs_alu##id.execute_stateful_alu(kvmsg.key); \
} \
table key2value##id { \
    reads { \
        kvmsg.op: exact; \
    }\
    actions { \
        read_fs##id; \
        write_fs##id; \
        no_op_my; \
    } \
    default_action: no_op_my; \
}


value_store(0)
value_store(1)
value_store(2)
value_store(3)
value_store(4)
value_store(5)
value_store(6)
value_store(7)
value_store(8)
value_store(9)
value_store(10)
value_store(11)
value_store(12)
value_store(13)
value_store(14)
value_store(15)
// value_store(16)
// value_store(17)
// value_store(18)
// value_store(19)
// value_store(20)
// value_store(21)
// value_store(22)
// value_store(23)