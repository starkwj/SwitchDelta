#!/bin/python

import struct, socket
import sys
import memcache
PRE = "./"

sys.path.append(PRE + 'build_pw/gen-py')
sys.path.append(
    "/root/bf-sde-8.8.1/install/lib/python2.7/site-packages/tofino")
sys.path.append(
    "/root/bf-sde-8.8.1/install/lib/python2.7/site-packages/")


from ptf import config
from ptf.testutils import *
from ptf.thriftutils import *

from p4_pd_rpc import p4_pw
from mc_pd_rpc import mc
from res_pd_rpc.ttypes import *
from p4_pd_rpc.ttypes import *
from mc_pd_rpc.ttypes import *


from thrift import Thrift
from thrift.transport import TSocket
from thrift.transport import TTransport
from thrift.protocol import TBinaryProtocol, TCompactProtocol
from thrift.protocol.TMultiplexedProtocol import TMultiplexedProtocol


def portToPipe(port):
    return port >> 7


def portToPipeLocalId(port):
    return port & 0x7F


def portToBitIdx(port):
    pipe = portToPipe(port)
    index = portToPipeLocalId(port)
    return 72 * pipe + index


def BitIdxToPort(index):
    pipe = index / 72
    local_port = index % 72
    return (pipe << 7) | local_port


def set_port_map(indicies):
    bit_map = [0] * ((288+7)/8)
    for i in indicies:
        index = portToBitIdx(i)
        bit_map[index/8] = (bit_map[index/8] | (1 << (index % 8))) & 0xFF
    return bytes_to_string(bit_map)


def set_lag_map(indicies):
    bit_map = [0] * ((256+7)/8)
    for i in indicies:
        bit_map[i/8] = (bit_map[i/8] | (1 << (i % 8))) & 0xFF
    return bytes_to_string(bit_map)


dev_id = 0
pipeID = hex_to_i16(0xFFFF)
dev_tgt = DevTarget_t(dev_id, pipeID)


def Hosts():

    host = {}
    f = open(PRE + "host")
    line = f.readline()
    i = 0
    while line:
        print(line)
        ip, mac, port, node = line.split(" ")
        host[ip] = int(port)
        i+=1
        if (i>=8):
            break
        line = f.readline()

    return host


def Memcached():

    memcDict = {}
    f = open(PRE + "host")
    line = f.readline()
    i = 0 
    while line:
        ip, mac, port, k = line.split(" ")
        memcDict[int(k)] = ip
        i+=1
        if (i>=8):
            break
        line = f.readline()

    return memcDict


def GetMap():
    host = Hosts()
    memcDict = Memcached()
    return {k: host[v] for k, v in memcDict.items()}


def read_map():
    multicast_map = {}
    f = open(PRE + "host")
    line = f.readline()
    i = 0
    res_map = {}
    while line: 
        ip, mac, port, k = line.split(" ")
        res_map[int(k)] = int(port)
        i+=1
        if (i>=8):
            break
        line = f.readline()
    return res_map


def toBig(v):
    return struct.unpack('i', struct.pack('>I', v))[0]


def addMC():

    # Dict = GetMap()
    Dict = read_map()
    print Dict

    nr = 2 ** len(Dict)
    print(nr)
    mgrp_hdl_list = []
    l1_hdl_list = []

    try:
        transport = TSocket.TSocket('127.0.0.1', 9090)
        transport = TTransport.TBufferedTransport(transport)

        protocol = TBinaryProtocol.TBinaryProtocol(transport)
        protocol = TMultiplexedProtocol(protocol, "mc")
        client = mc.Client(protocol)

        transport.open()

        mc_sess_hdl = client.mc_create_session()
        for i in range(0, nr):
            l = []

            # print Dict
            for k in range(0, len(Dict)):
                if ((i >> k) & 1) == 1:
                    
                    l.append(Dict[k])

            #  mgrp_hdl = client.mc_mgrp_create(mc_sess_hdl, dev_id,
                    #  (i >> 8) | ((i & 0xff) << 8));
            mgrp_hdl = client.mc_mgrp_create(mc_sess_hdl, dev_id,
                                             struct.unpack('h', struct.pack('>h', i))[0])

            mgrp_hdl_list.append(mgrp_hdl)

            l1_hdl = client.mc_node_create(mc_sess_hdl, dev_id, 233,
                                           set_port_map(l),
                                           set_lag_map([]))

            l1_hdl_list.append(l1_hdl)

            client.mc_associate_node(
                mc_sess_hdl, dev_id, mgrp_hdl, l1_hdl, 0, 0)

        client.mc_complete_operations(mc_sess_hdl)

    except Thrift.TException, ex:
        print "%s" % (ex.message)

    with open('../record.log', 'w') as f :
        f.writelines(str(Dict) + "\n")
        f.writelines(str(mc_sess_hdl)  + "\n")
        f.writelines(str(mgrp_hdl_list) + "\n")
        f.writelines(str(l1_hdl_list) + "\n")
if __name__ == '__main__':
    addMC()
