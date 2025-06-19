#!/bin/python
import sys
sys.path.append('./build_fw/gen-py')
PRE = "./"
sys.path.append("/root/bf-sde-8.8.1/install/lib/python2.7/site-packages/tofino")
sys.path.append("/root/bf-sde-8.8.1/install/lib/python2.7/site-packages/")

from ptf import config
from ptf.testutils import *
from ptf.thriftutils import *
from p4_pd_rpc import p4_fw
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
    protocol = TMultiplexedProtocol(protocol, "p4_fw")
    client = p4_fw.Client(protocol)

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
    #     p4_fw_cache_check_match_spec_t(int(kEnumWriteReplyWithData))
    # )
    # client.cache_check_table_add_with_cache_read_reply(
    #     0,
    #     devtt,
    #     p4_fw_cache_check_match_spec_t(int(kEnumReadReply))
    # )
    # client.cache_check_table_add_with_cache_write_req(
    #     0, 
    #     devtt,
    #     p4_fw_cache_check_match_spec_t(int(kEnumWriteAndRead))
    # )
    # client.cache_check_table_add_with_cache_read_req(
    #     0,
    #     devtt,
    #     p4_fw_cache_check_match_spec_t(int(kEnumReadReq))
    # )

    # client.timecheck_table_add_with_timecmp(
    #     0,
    #     devtt,
    #     p4_fw_timecheck_match_spec_t(int(kEnumAppendReply))
    # )

    # client.timecheck_table_add_with_timecmp(
    #     0,
    #     devtt,
    #     p4_fw_timecheck_match_spec_t(int(kEnumUpdateIndexReq))
    # )

    # client.timecheck_table_add_with_timeclr(
    #     0,
    #     devtt,
    #     p4_fw_timecheck_match_spec_t(int(ReplySwitch))
    # )


    # visibility table
    client.visibility_table_add_with_new_index(
        0, 
        devtt, 
        p4_fw_visibility_match_spec_t(int(kEnumAppendReply)))
    client.visibility_table_add_with_new_index(
        0, 
        devtt, 
        p4_fw_visibility_match_spec_t(int(kEnumUpdateIndexReq)))

    # client.visibility_table_add_with_delete_index(
    #     0, 
    #     devtt, 
    #     p4_fw_visibility_match_spec_t(kEnumUpdateIndexReply))
    client.visibility_table_add_with_delete_index(
        0, 
        devtt, 
        p4_fw_visibility_match_spec_t(int(ReplySwitch)))
        # p4_fw_udp_route_match_spec_t
    
    client.visibility_table_add_with_check_index(
        0, 
        devtt, 
        p4_fw_visibility_match_spec_t(int(kEnumReadIndexReq)))

    # ------------------------------------------------- check_hash# 


    # key2logid
    # client.key2value0_table_add_with_read_fs0(0, devtt, cable_store_key2value0_match_spec_t(int(kEnumReadIndexReq)))   
    # client.key2value0_table_add_with_write_fs0(0, devtt, cable_store_key2value0_match_spec_t(int(kEnumAppendReply)))
    # client.key2value0_table_add_with_write_fs0(0, devtt, cable_store_key2value0_match_spec_t(int(kEnumUpdateIndexReq)))

    # client.key2value1_table_add_with_read_fs1(0, devtt, cable_store_key2value1_match_spec_t(int(kEnumReadIndexReq)))   
    # client.key2value1_table_add_with_write_fs1(0, devtt, cable_store_key2value1_match_spec_t(int(kEnumAppendReply)))
    # client.key2value1_table_add_with_write_fs1(0, devtt, cable_store_key2value1_match_spec_t(int(kEnumUpdateIndexReq)))

    # client.key2value2_table_add_with_read_fs2(0, devtt, cable_store_key2value2_match_spec_t(int(kEnumReadIndexReq)))   
    # client.key2value2_table_add_with_write_fs2(0, devtt, cable_store_key2value2_match_spec_t(int(kEnumAppendReply)))
    # client.key2value2_table_add_with_write_fs2(0, devtt, cable_store_key2value2_match_spec_t(int(kEnumUpdateIndexReq)))

    # client.key2value3_table_add_with_read_fs3(0, devtt, cable_store_key2value3_match_spec_t(int(kEnumReadIndexReq)))   
    # client.key2value3_table_add_with_write_fs3(0, devtt, cable_store_key2value3_match_spec_t(int(kEnumAppendReply)))
    # client.key2value3_table_add_with_write_fs3(0, devtt, cable_store_key2value3_match_spec_t(int(kEnumUpdateIndexReq)))

    # client.key2value4_table_add_with_read_fs4(0, devtt, cable_store_key2value4_match_spec_t(int(kEnumReadIndexReq)))
    # client.key2value4_table_add_with_write_fs4(0, devtt, cable_store_key2value4_match_spec_t(int(kEnumAppendReply)))
    # client.key2value4_table_add_with_write_fs4(0, devtt, cable_store_key2value4_match_spec_t(int(kEnumUpdateIndexReq))) 

    # client.key2value5_table_add_with_read_fs5(0, devtt, cable_store_key2value5_match_spec_t(int(kEnumReadIndexReq)))
    # client.key2value5_table_add_with_write_fs5(0, devtt, cable_store_key2value5_match_spec_t(int(kEnumAppendReply)))
    # client.key2value5_table_add_with_write_fs5(0, devtt, cable_store_key2value5_match_spec_t(int(kEnumUpdateIndexReq)))

    # client.key2value6_table_add_with_read_fs6(0, devtt, cable_store_key2value6_match_spec_t(int(kEnumReadIndexReq)))
    # client.key2value6_table_add_with_write_fs6(0, devtt, cable_store_key2value6_match_spec_t(int(kEnumAppendReply)))
    # client.key2value6_table_add_with_write_fs6(0, devtt, cable_store_key2value6_match_spec_t(int(kEnumUpdateIndexReq)))

    # client.key2value7_table_add_with_read_fs7(0, devtt, cable_store_key2value7_match_spec_t(int(kEnumReadIndexReq)))
    # client.key2value7_table_add_with_write_fs7(0, devtt, cable_store_key2value7_match_spec_t(int(kEnumAppendReply)))
    # client.key2value7_table_add_with_write_fs7(0, devtt, cable_store_key2value7_match_spec_t(int(kEnumUpdateIndexReq)))

    # client.key2value8_table_add_with_read_fs8(0, devtt, cable_store_key2value8_match_spec_t(int(kEnumReadIndexReq)))
    # client.key2value8_table_add_with_write_fs8(0, devtt, cable_store_key2value8_match_spec_t(int(kEnumAppendReply)))
    # client.key2value8_table_add_with_write_fs8(0, devtt, cable_store_key2value8_match_spec_t(int(kEnumUpdateIndexReq)))

    # client.key2value9_table_add_with_read_fs9(0, devtt, cable_store_key2value9_match_spec_t(int(kEnumReadIndexReq)))
    # client.key2value9_table_add_with_write_fs9(0, devtt, cable_store_key2value9_match_spec_t(int(kEnumAppendReply)))
    # client.key2value9_table_add_with_write_fs9(0, devtt, cable_store_key2value9_match_spec_t(int(kEnumUpdateIndexReq)))

    # client.key2value10_table_add_with_read_fs10(0, devtt, cable_store_key2value10_match_spec_t(int(kEnumReadIndexReq)))
    # client.key2value10_table_add_with_write_fs10(0, devtt, cable_store_key2value10_match_spec_t(int(kEnumAppendReply)))
    # client.key2value10_table_add_with_write_fs10(0, devtt, cable_store_key2value10_match_spec_t(int(kEnumUpdateIndexReq)))

    # client.key2value11_table_add_with_read_fs11(0, devtt, cable_store_key2value11_match_spec_t(int(kEnumReadIndexReq)))
    # client.key2value11_table_add_with_write_fs11(0, devtt, cable_store_key2value11_match_spec_t(int(kEnumAppendReply)))
    # client.key2value11_table_add_with_write_fs11(0, devtt, cable_store_key2value11_match_spec_t(int(kEnumUpdateIndexReq)))

    # client.key2value12_table_add_with_read_fs12(0, devtt, cable_store_key2value12_match_spec_t(int(kEnumReadIndexReq)))
    # client.key2value12_table_add_with_write_fs12(0, devtt, cable_store_key2value12_match_spec_t(int(kEnumAppendReply)))
    # client.key2value12_table_add_with_write_fs12(0, devtt, cable_store_key2value12_match_spec_t(int(kEnumUpdateIndexReq)))

    # client.key2value13_table_add_with_read_fs13(0, devtt, cable_store_key2value13_match_spec_t(int(kEnumReadIndexReq)))
    # client.key2value13_table_add_with_write_fs13(0, devtt, cable_store_key2value13_match_spec_t(int(kEnumAppendReply)))
    # client.key2value13_table_add_with_write_fs13(0, devtt, cable_store_key2value13_match_spec_t(int(kEnumUpdateIndexReq)))

    # client.key2value14_table_add_with_read_fs14(0, devtt, cable_store_key2value14_match_spec_t(int(kEnumReadIndexReq)))
    # client.key2value14_table_add_with_write_fs14(0, devtt, cable_store_key2value14_match_spec_t(int(kEnumAppendReply)))
    # client.key2value14_table_add_with_write_fs14(0, devtt, cable_store_key2value14_match_spec_t(int(kEnumUpdateIndexReq)))

    # client.key2value15_table_add_with_read_fs15(0, devtt, cable_store_key2value15_match_spec_t(int(kEnumReadIndexReq)))
    # client.key2value15_table_add_with_write_fs15(0, devtt, cable_store_key2value15_match_spec_t(int(kEnumAppendReply)))
    # client.key2value15_table_add_with_write_fs15(0, devtt, cable_store_key2value15_match_spec_t(int(kEnumUpdateIndexReq)))

    # key2logid
    client.key2logid0_table_add_with_read_log_id0(
        0, 
        devtt, 
        p4_fw_key2logid0_match_spec_t(int(kEnumReadIndexReq)))
    
    client.key2logid0_table_add_with_write_log_id0(
        0, 
        devtt, 
        p4_fw_key2logid0_match_spec_t(int(kEnumAppendReply)))
    client.key2logid0_table_add_with_write_log_id0(
        0, 
        devtt, 
        p4_fw_key2logid0_match_spec_t(int(kEnumUpdateIndexReq)))
    
    client.key2logid1_table_add_with_read_log_id1(
        0, 
        devtt, 
        p4_fw_key2logid1_match_spec_t(int(kEnumReadIndexReq)))
    
    client.key2logid1_table_add_with_write_log_id1(
        0, 
        devtt, 
        p4_fw_key2logid1_match_spec_t(int(kEnumAppendReply)))
    client.key2logid1_table_add_with_write_log_id1(
        0, 
        devtt, 
        p4_fw_key2logid1_match_spec_t(int(kEnumUpdateIndexReq)))


    # fix port
    client.fix_port_table_add_with_send_to_index(
        0, 
        devtt, 
        p4_fw_fix_port_match_spec_t(188, int(kEnumMultiCastPkt)))
    
    client.fix_port_table_add_with_send_to_client(
        0, 
        devtt, 
        p4_fw_fix_port_match_spec_t(136, int(kEnumMultiCastPkt)))
    client.fix_port_table_add_with_send_to_client(
        0, 
        devtt, 
        p4_fw_fix_port_match_spec_t(136, int(kEnumReadIndexReq)))
    client.fix_port_table_add_with_send_to_client(
        0, 
        devtt, 
        p4_fw_fix_port_match_spec_t(180, int(kEnumMultiCastPkt)))
    client.fix_port_table_add_with_send_to_client(
        0, 
        devtt, 
        p4_fw_fix_port_match_spec_t(180, int(kEnumReadIndexReq)))
    client.fix_port_table_add_with_send_to_client(
        0, 
        devtt, 
        p4_fw_fix_port_match_spec_t(172, int(kEnumMultiCastPkt)))
    client.fix_port_table_add_with_send_to_client(
        0, 
        devtt, 
        p4_fw_fix_port_match_spec_t(172, int(kEnumReadIndexReq)))

    # counter table
    port_list = [0, 1, 2, 3, 4, 5, 6, 7];
    for i in port_list:
        port = hex_to_i16(i);
        client.counter_table_table_add_with_counter_add(
            0,
            devtt,
            p4_fw_counter_table_match_spec_t(int(kEnumAppendReq), port),
            p4_fw_counter_add_action_spec_t(i)
        )  
        # client.counter_table_table_add_with_counter_add(
        #     0,
        #     devtt,
        #     p4_fw_counter_table_match_spec_t(int(kEnumUpdateIndexReq), port),
        #     p4_fw_counter_add_action_spec_t(i)
        # )     
        client.counter_table_table_add_with_counter_sub(
            0,
            devtt,
            p4_fw_counter_table_match_spec_t(int(ReplySwitch), port),
            p4_fw_counter_sub_action_spec_t(i)
        )
        client.counter_table_table_add_with_counter_sub(
            0,
            devtt,
            p4_fw_counter_table_match_spec_t(int(kEnumUpdateIndexReply), port),
            p4_fw_counter_sub_action_spec_t(i)
        )

        client.counter_table_table_add_with_counter_read(
            0,
            devtt,
            p4_fw_counter_table_match_spec_t(int(kEnumAppendReply), port),
            p4_fw_counter_sub_action_spec_t(i)
        )
        client.counter_table_table_add_with_counter_read(
            0,
            devtt,
            p4_fw_counter_table_match_spec_t(int(kEnumReadReply), port),
            p4_fw_counter_sub_action_spec_t(i)
        )
        client.counter_table_table_add_with_counter_read(
            0,
            devtt,
            p4_fw_counter_table_match_spec_t(int(kEnumReadIndexReply), port),
            p4_fw_counter_sub_action_spec_t(i)
        )
        client.counter_table_table_add_with_counter_read(
            0,
            devtt,
            p4_fw_counter_table_match_spec_t(int(kEnumReadSend), port),
            p4_fw_counter_sub_action_spec_t(i)
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
                                                 p4_fw_ipv4_route_match_spec_t(
                                                     ip2int(ip)),
                                                 p4_fw_set_egr_action_spec_t(int(port)))
        # egress
        
        client.ethernet_set_mac_table_add_with_ethernet_set_mac_act(0, devtt,
                                                                    p4_fw_ethernet_set_mac_match_spec_t(
                                                                        int(port)),
                                                                    p4_fw_ethernet_set_mac_act_action_spec_t(mac2v(mac)))
        # do ucast
        print client.udp_route_table_add_with_set_port_action(
            0, devtt, p4_fw_udp_route_match_spec_t(int(node)), p4_fw_set_port_action_action_spec_t(int(port)))
        line = f.readline()
        

except Thrift.TException, ex:
    print "%s" % (ex.message)
