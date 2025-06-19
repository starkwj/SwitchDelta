
set -x

# ssh root@10.0.2.125 "pkill bash;"

if [ $# != 4 ] ; then
echo "USAGE: $0 [coro_num=] [visibility] [cable_cache] [read_write] [cache test]"
exit 1;
fi


if ()

ssh 10.0.2.116 "cd /home/ljr/NetStore/build; pkill kv; ./kv --node_id=0 >/dev/null 2>&1 &"
sleep 1s
# ssh 10.0.2.115 "cd /home/ljr/NetStore/build; pkill kv; ./kv --node_id=1 >/dev/null 2>&1 &"

ssh 10.0.2.116 "cd /home/ljr/NetStore/build; ./kv --node_id=1 --coro_num=$1 --visibility=$2 --cable_cache=$3 --cache_test=$3 --read=$4"
