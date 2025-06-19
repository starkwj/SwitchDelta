
set -x

# ssh root@10.0.2.125 "pkill bash;"

ssh 10.0.2.116 "pkill kv"
sleep 2s
ssh 10.0.2.117 "pkill kv"


ssh root@10.0.2.125 "cd /home/workspace/ljr/NetStore/p4src/SwitchDelta/; ./run.sh  > /dev/null 2>&1;"

