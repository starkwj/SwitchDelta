header_type ethernet_t {
    fields {
        dstAddr : 48;
        srcAddr : 48;
        etherType : 16;
    }
}
header ethernet_t ethernet;

header_type ipv4_t {
    fields {
        version : 4;
        ihl : 4;
        diffserv : 8;
        totalLen : 16;
        identification : 16;
        flags : 3;
        fragOffset : 13;
        ttl : 8;
        protocol : 8;
        hdrChecksum : 16;
        srcAddr : 32;
        dstAddr: 32;
    }
}
header ipv4_t ipv4;

field_list ipv4_checksum_list{
    ipv4.version;
    ipv4.ihl;
    ipv4.diffserv;
    ipv4.totalLen;
    ipv4.identification;
    ipv4.flags;
    ipv4.fragOffset;
    ipv4.ttl;
    ipv4.protocol;
    ipv4.srcAddr;
    ipv4.dstAddr;
}
field_list_calculation ipv4_checksum{
    input {ipv4_checksum_list;}
    algorithm : csum16;
    output_width :16;
}
calculated_field ipv4.hdrChecksum{
    update ipv4_checksum;
}
header_type tcp_t {
    fields {
        srcPort : 16;
        dstPort : 16;
        seqNo : 32;
        ackNo : 32;
        dataOffset : 4;
        res : 3;
        ecn : 3;
        ctrl : 6;
        window : 16;
        checksum : 16;
        urgentPtr : 16;
    }   
}
header tcp_t tcp;

header_type udp_t {
    fields {
        srcPort : 16;
        dstPort : 16;
        len : 16;
        checksum : 16;
    }
}
header udp_t udp;

header_type message_t {
    fields {
        node_id: 8;
        rpc_addr: 32;
        op: 8;
        len: 32;
        flag: 8;
        key: 16;
        rpc_id: 32; // g_id c_id magic
        s_map: 16;
        q_map: 16;
        coro_id_1: 32;
        coro_id_2: 32;
        key32: 32;
        log_id0: 32;
        log_id1: 32;
        route_port: 16;
        route_map: 16;
        client_port: 16;
        count: 16;
        index_port: 16;
        magic_in_switch: 16;
        timestamp: 32;
    }
}
header message_t kvmsg;

header_type fs_t {
    fields {
        value0: 32;
        value1: 32;
        value2: 32;
        value3: 32;
        value4: 32;
        value5: 32;
        value6: 32;
        value7: 32;
        value8: 32;
        value9: 32;
        value10: 32;
        value11: 32;
        value12: 32;
        value13: 32;
        value14: 32;
        value15: 32;
        // value16: 32;
        // value17: 32;
        // value18: 32;
        // value19: 32;
        // value20: 32;
        // value21: 32;
        // value22: 32;
        // value23: 32;
    }
}
header fs_t fsmsg;

header_type usermeta_t{
    fields{
        q_map: 16;
        key32: 32;
        op:8;
        already_write:8;
        req_reply:8;
        rpc_id: 32;
        timestamp: 32;
        // 
        // blocked: 8;
        // block_count: 8;
        // blocked_id: 8;
    }
}
metadata usermeta_t umeta;

