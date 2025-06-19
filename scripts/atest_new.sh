
set -x

# ssh root@10.0.2.125 "pkill bash;"

if [ $# != 6 ] ; then
echo "USAGE: $0 [1:testname] [2:read=num] [3:coro_num=num] [4:visibility=y/n] [5:cable_cache=y/n] [6:read_first=y/n]"
exit 1;
fi


ssh 10.0.2.116 "cd /home/ljr/NetStore/build; ./$1 --node_id=0 >/dev/null 2>&1 &"
sleep 1s
ssh 10.0.2.117 "cd /home/ljr/NetStore/build; ./$1 --node_id=1 --read_first=$6 >/dev/null 2>&1 &"

ssh 10.0.2.116 "cd /home/ljr/NetStore/build; ./$1 --node_id=2 --read=$2 --coro_num=$3 --visibility=$4 --cable_cache=$5 --cache_test=$5 --qp_type=1"
