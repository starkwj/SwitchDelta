#!/bin/bash

root_path="10.0.2.112:/home/ljr"
nodes=(117 116)

for node in ${nodes[@]}
    do
        echo $node
        ssh 10.0.2.$node "mkdir -p /home/ljr; mount $root_path /home/ljr"
    done
