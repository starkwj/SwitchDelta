
set -x

# ssh root@10.0.2.125 "pkill bash;"
ssh root@10.0.2.125 "pkill bf_switchd; pkill bf_switchd; sleep 1; pkill bash"

ssh root@10.0.2.125 "cd /home/workspace/ljr/NetStore/p4src/SwitchDelta/; $1  > /dev/null 2>&1"

