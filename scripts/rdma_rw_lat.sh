#!/bin/bash

client_ip="10.0.2.115"
server_ip="10.0.2.116"
src_path=$(cat ./src_path)

cd ../build; make -j
cd -

for ip in client_ip server_ip
    do
        ssh root@ip "pkill rdma_rw_lat" 
    done

ssh root@$server_ip "cd $src_path/build; ./rdma_rw_lat -is_server=true >/dev/null 2>&1 &"
ssh root@$client_ip "cd $src_path/build; gdb -ex=r ./rdma_rw_lat"