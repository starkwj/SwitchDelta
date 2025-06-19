parser start {
    return parse_ethernet;
}

#define DELTA_TYPE_FS 0xFFFFFFFF

#define ETHER_TYPE_IPV4 0x0800
parser parse_ethernet {
    
    extract (ethernet);
    //return ingress;
    
    return select (latest.etherType) {
        ETHER_TYPE_IPV4: parse_ipv4;
        default: ingress;
    }

}

#define IPV4_PROTOCOL_TCP 6
#define IPV4_PROTOCOL_UDP 17
#define IPV4_PROTOCOL_LL 0

parser parse_ipv4 {
    extract(ipv4);
    return select (latest.protocol) {
        IPV4_PROTOCOL_TCP: parse_tcp;
        IPV4_PROTOCOL_UDP: parse_udp;
        default: ingress;
    }
}
parser parse_tcp {
    extract (tcp);
    return ingress;
}

#define KVAPP_PORT 666

parser parse_udp {
    extract (udp);
    return select (latest.srcPort) {
        KVAPP_PORT: parse_kvmsg;
        default: ingress;
    }
}

parser parse_kvmsg {
    extract (kvmsg);
    set_metadata(umeta.key32, 0);
    set_metadata(umeta.rpc_id, 0);
    set_metadata(umeta.already_write, 0);
    set_metadata(umeta.op, 1);
    set_metadata(umeta.req_reply, 0);
    set_metadata(umeta.timestamp, 0);

    return ingress;
    // return select (latest.log_id1) {
    //     DELTA_TYPE_FS: parse_fs;
    //     default: ingress;
    // }
}

parser parse_fs {
    extract (fsmsg);
    return ingress;
}