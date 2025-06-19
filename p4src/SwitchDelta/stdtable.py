#!/bin/python
import sys
sys.path.append('./build/gen-py')
PRE = "./"
sys.path.append("/root/bf-sde-8.8.1/install/lib/python2.7/site-packages/tofino")
sys.path.append("/root/bf-sde-8.8.1/install/lib/python2.7/site-packages/")

from ptf import config
from ptf.testutils import *
from ptf.thriftutils import *
from p4_pd_rpc import cable_store
from mc_pd_rpc import mc
from tm_api_rpc import tm
from res_pd_rpc.ttypes import *
from p4_pd_rpc.ttypes import *
from mc_pd_rpc.ttypes import *
from tm_api_rpc.ttypes import *

from thrift import Thrift
from thrift.transport import TSocket
from thrift.transport import TTransport
from thrift.protocol import TBinaryProtocol, TCompactProtocol
from thrift.protocol.TMultiplexedProtocol import TMultiplexedProtocol

kEnumReadReq = 0
kEnumWriteReq = 1
kEnumReadReply = 2
kEnumWriteReply = 3

kEnumAppendReq = 4
kEnumAppendReply = 5
kEnumReadLog = 6

ReplySwitch = 10
kEnumUpdateIndexReq = 11
kEnumUpdateIndexReply = 12

kEnumReadIndexReq = 13
kEnumReadIndexReply = 14

kEnumMultiCastPkt = 21
kEnumReadSend = 22

kEnumWriteAndRead = 23
kEnumWriteReplyWithData = 24




def hex_to_i16(h):
    x = int(h)
    if (x > 0x7FFF):
        x -= 0x10000
    return x


def ip2int(ip):
    return hex_to_i32(reduce(lambda a, b: a << 8 | b, map(int, ip.split("."))))


def mac2v(mac):
    return ''.join(map(lambda x: chr(int(x, 16)), mac.split(":")))


try:
    transport = TSocket.TSocket('127.0.0.1', 9090)
    transport = TTransport.TBufferedTransport(transport)

    protocol = TBinaryProtocol.TBinaryProtocol(transport)
    protocol = TMultiplexedProtocol(protocol, "cable_store")
    client = cable_store.Client(protocol)

    transport.open()

    pipeID = hex_to_i16(0xFFFF)
    i = hex_to_i32(1)
    # print client.ethernet_set_mac_get_entry_count(0, DevTarget_t(0, pipeID))
    devtt = DevTarget_t(0, pipeID)

    #------------Cache table------------#
    #cache_check

    # client.cache_check_table_add_with_cache_write_reply(
    #     0,
    #     devtt,
    #     cable_store_cache_check_match_spec_t(int(kEnumWriteReplyWithData))
    # )
    # client.cache_check_table_add_with_cache_read_reply(
    #     0,
    #     devtt,
    #     cable_store_cache_check_match_spec_t(int(kEnumReadReply))
    # )
    # client.cache_check_table_add_with_cache_write_req(
    #     0, 
    #     devtt,
    #     cable_store_cache_check_match_spec_t(int(kEnumWriteAndRead))
    # )
    # client.cache_check_table_add_with_cache_read_req(
    #     0,
    #     devtt,
    #     cable_store_cache_check_match_spec_t(int(kEnumReadReq))
    # )


    # client.cache_op_table_add_with__drop(
    #     0,
    #     devtt,
    #     cable_store_cache_op_match_spec_t(0,0);
    # )

    # client.cache_op_table_add_with__drop(
    #     0,
    #     devtt,
    #     cable_store_cache_op_match_spec_t(0,1);
    # )

    # client.cache_op_table_add_with_copy_port_a(
    #     0,
    #     devtt,
    #     cable_store_cache_op_match_spec_t(1,0);
    # )

    # client.cache_op_table_add_with__drop(
    #     0,
    #     devtt,
    #     cable_store_cache_op_match_spec_t(1,1);
    # )


    # visibility table
    client.visibility_table_add_with_new_index(
        0, 
        devtt, 
        cable_store_visibility_match_spec_t(int(kEnumAppendReply)))
    client.visibility_table_add_with_new_index(
        0, 
        devtt, 
        cable_store_visibility_match_spec_t(int(kEnumUpdateIndexReq)))

    # client.visibility_table_add_with_delete_index(
    #     0, 
    #     devtt, 
    #     cable_store_visibility_match_spec_t(kEnumUpdateIndexReply))
    client.visibility_table_add_with_delete_index(
        0, 
        devtt, 
        cable_store_visibility_match_spec_t(int(ReplySwitch)))
        # cable_store_udp_route_match_spec_t
    
    client.visibility_table_add_with_check_index(
        0, 
        devtt, 
        cable_store_visibility_match_spec_t(int(kEnumReadIndexReq)))

    # ------------------------------------------------- check_hash# 
    client.check_hash_table_add_with_new_index_hash(
        0, 
        devtt, 
        cable_store_check_hash_match_spec_t(int(kEnumAppendReply)))
    client.check_hash_table_add_with_new_index_hash(
        0, 
        devtt, 
        cable_store_check_hash_match_spec_t(int(kEnumUpdateIndexReq)))

    # client.visibility_table_add_with_delete_index(
    #     0, 
    #     devtt, 
    #     cable_store_visibility_match_spec_t(kEnumUpdateIndexReply))
    client.check_hash_table_add_with_delete_index_hash(
        0, 
        devtt, 
        cable_store_check_hash_match_spec_t(int(ReplySwitch)))
        # cable_store_udp_route_match_spec_t
    
    client.check_hash_table_add_with_check_index_hash(
        0, 
        devtt, 
        cable_store_check_hash_match_spec_t(int(kEnumReadIndexReq)))
    


    # key2logid
    client.key2logid0_table_add_with_read_log_id0(
        0, 
        devtt, 
        cable_store_key2logid0_match_spec_t(int(kEnumReadIndexReq)))
    
    client.key2logid0_table_add_with_write_log_id0(
        0, 
        devtt, 
        cable_store_key2logid0_match_spec_t(int(kEnumAppendReply)))
    client.key2logid0_table_add_with_write_log_id0(
        0, 
        devtt, 
        cable_store_key2logid0_match_spec_t(int(kEnumUpdateIndexReq)))
    
    client.key2logid1_table_add_with_read_log_id1(
        0, 
        devtt, 
        cable_store_key2logid1_match_spec_t(int(kEnumReadIndexReq)))
    
    client.key2logid1_table_add_with_write_log_id1(
        0, 
        devtt, 
        cable_store_key2logid1_match_spec_t(int(kEnumAppendReply)))
    client.key2logid1_table_add_with_write_log_id1(
        0, 
        devtt, 
        cable_store_key2logid1_match_spec_t(int(kEnumUpdateIndexReq)))

    # ------------------------------------------------- key2value#

    



    
    
    
    


    # fix port
    client.fix_port_table_add_with_send_to_index(
        0, 
        devtt, 
        cable_store_fix_port_match_spec_t(188, int(kEnumMultiCastPkt)))
    client.fix_port_table_add_with_send_to_client(
        0, 
        devtt, 
        cable_store_fix_port_match_spec_t(136, int(kEnumMultiCastPkt)))
    client.fix_port_table_add_with_send_to_client(
        0, 
        devtt, 
        cable_store_fix_port_match_spec_t(136, int(kEnumReadIndexReq)))
    client.fix_port_table_add_with_send_to_client(
        0, 
        devtt, 
        cable_store_fix_port_match_spec_t(180, int(kEnumMultiCastPkt)))
    client.fix_port_table_add_with_send_to_client(
        0, 
        devtt, 
        cable_store_fix_port_match_spec_t(180, int(kEnumReadIndexReq)))
    client.fix_port_table_add_with_send_to_client(
        0, 
        devtt, 
        cable_store_fix_port_match_spec_t(172, int(kEnumMultiCastPkt)))
    client.fix_port_table_add_with_send_to_client(
        0, 
        devtt, 
        cable_store_fix_port_match_spec_t(172, int(kEnumReadIndexReq)))

    # counter table
    port_list = [0, 1, 2, 3, 4, 5, 6, 7];
    for i in port_list:
        port = hex_to_i16(i); # FIXME:3 only FS 
        client.counter_table_table_add_with_counter_add(
            0,
            devtt,
            cable_store_counter_table_match_spec_t(int(kEnumAppendReq), port),
            cable_store_counter_add_action_spec_t(i)
        )  
        # client.counter_table_table_add_with_counter_add(
        #     0,
        #     devtt,
        #     cable_store_counter_table_match_spec_t(int(kEnumAppendReply), port),
        #     cable_store_counter_add_action_spec_t(i)
        # )     
        client.counter_table_table_add_with_counter_read(
            0,
            devtt,
            cable_store_counter_table_match_spec_t(int(kEnumAppendReply), port),
            cable_store_counter_sub_action_spec_t(i)
        )
        client.counter_table_table_add_with_counter_sub(
            0,
            devtt,
            cable_store_counter_table_match_spec_t(int(ReplySwitch), port),
            cable_store_counter_sub_action_spec_t(i)
        )
        client.counter_table_table_add_with_counter_sub(
            0,
            devtt,
            cable_store_counter_table_match_spec_t(int(kEnumUpdateIndexReply), port),
            cable_store_counter_sub_action_spec_t(i)
        )

        client.counter_table_table_add_with_counter_read(
            0,
            devtt,
            cable_store_counter_table_match_spec_t(int(kEnumReadReply), port),
            cable_store_counter_sub_action_spec_t(i)
        )
        client.counter_table_table_add_with_counter_read(
            0,
            devtt,
            cable_store_counter_table_match_spec_t(int(kEnumReadIndexReply), port),
            cable_store_counter_sub_action_spec_t(i)
        )
        client.counter_table_table_add_with_counter_read(
            0,
            devtt,
            cable_store_counter_table_match_spec_t(int(kEnumReadSend), port),
            cable_store_counter_sub_action_spec_t(i)
        )
        

    f = open(PRE + "host")
    line = f.readline()

    while line:
        if ('.' not in line):
            break
        print (line)
        ip, mac, port,node = line.split(" ")
        # normal rc rdma 
        client.ipv4_route_table_add_with_set_egr(0, devtt,
                                                 cable_store_ipv4_route_match_spec_t(
                                                     ip2int(ip)),
                                                 cable_store_set_egr_action_spec_t(int(port)))
        # egress
        
        client.ethernet_set_mac_table_add_with_ethernet_set_mac_act(0, devtt,
                                                                    cable_store_ethernet_set_mac_match_spec_t(
                                                                        int(port)),
                                                                    cable_store_ethernet_set_mac_act_action_spec_t(mac2v(mac)))
        # do ucast
        print client.udp_route_table_add_with_set_port_action(
            0, devtt, cable_store_udp_route_match_spec_t(int(node)), cable_store_set_port_action_action_spec_t(int(port)))
        line = f.readline()
        

except Thrift.TException, ex:
    print "%s" % (ex.message)
