
set -x

# ssh root@10.0.2.125 "pkill bash;"


ssh 10.0.2.116 "pkill secondary_index"
# sleep 2s

ssh 10.0.2.116 "pkill kv"
# sleep 2s

ssh 10.0.2.117 "pkill secondary_index"
# sleep 2s

ssh 10.0.2.117 "pkill kv"
# sleep 2s

ssh root@10.0.2.125 "pkill bf_switchd; pkill bash;"
sleep 2s
