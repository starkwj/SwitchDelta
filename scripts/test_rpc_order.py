import sys 
import time 
import subprocess 
import signal 
import itertools

import pickle
# import matplotlib.pyplot as plt
import matplotlib.pyplot as plt
from utility import *
# repeat experiments for the following times
import os

from datetime import datetime
currentDateAndTime = datetime.now()
print("The current date and time is", currentDateAndTime)

import time
localtime = time.localtime(time.time())
finger = time.strftime("%Y%m%d%H%M", time.localtime()) 
print(finger)

nodes = [116, 117, 116, 118, 119]
path = "/home/ljr/NetStore/build"
exe = "no_order"

CORO_ARRAY= [[0,0,8,8,8]]
THREAD_ARRAY = [4]
PB_name = ["2:1", "2:2", "2:3", "2:4", "2:5", "2:6", "2:7", "2:8"]
pb_array = [[2,1], [2,2], [2,3], [2,4], [2,5], [2,6], [2,7], [2,8]]



sum = []

for i, t in enumerate(THREAD_ARRAY):
    tmp = 0
    for j, c in enumerate(CORO_ARRAY[i]):
        tmp += c * t;
    sum.append(tmp)


def load_switch():
    cmd = "./run_switch_bash.sh run_batch.sh"
    os.system(cmd)

def kill_all():
    for i in nodes:
        cmd = f'ssh 10.0.2.{i} "pkill batch; pkill tmp_cmd; pkill index_onl; pkill no_order"'
        print(cmd)
        os.system(cmd)

def run_test(pb, order, coro):
    for i in nodes:
        cmd = f'ssh 10.0.2.{i} "pkill no_order"'
        print(cmd)
        os.system(cmd)
    time.sleep(1)
    ret_cmd = f'last';
    tp_file = f'{path}/test{order}-{coro}-{PB_name[pb]}'

    qp_type = 0
    thread_cnt = THREAD_ARRAY[coro]


    for i, node in enumerate(nodes):
        if (i == 0):
            cmd = f'ssh 10.0.2.{node} "cd {path}; ./{exe} --node_id={i} --qp_type={qp_type}  --order={order} --first={pb_array[pb][0]} --second={pb_array[pb][1]} --thread_cnt={thread_cnt} > {tp_file} 2>&1 &"'
            os.system(cmd)
        elif (i == 1):
            cmd = f'ssh 10.0.2.{node} "cd {path}; ./{exe} --node_id={i} --qp_type={qp_type}  --order={order} --first={pb_array[pb][0]} --second={pb_array[pb][1]} --thread_cnt={thread_cnt} --cq_len={24576} > /dev/null 2>&1 &"'
            os.system(cmd)
        elif (i == 2):
            cmd = f'ssh 10.0.2.{node} "cd {path}; ./{exe} --node_id={i} --qp_type={qp_type}  --order={order} --first={pb_array[pb][0]} --second={pb_array[pb][1]} --thread_cnt={thread_cnt} --coro_num={CORO_ARRAY[coro][i]}"'
            ret_cmd = str(cmd)
        else:
            cmd = f'ssh 10.0.2.{node} "cd {path}; ./{exe} --node_id={i} --qp_type={qp_type} --order={order} --first={pb_array[pb][0]} --second={pb_array[pb][1]} --thread_cnt={thread_cnt} --coro_num={CORO_ARRAY[coro][i]} > /dev/null 2>&1 &"'
            os.system(cmd)
        print(cmd);
    
    return ret_cmd, tp_file
    # client

# INDEX = ["tree", "hash"]
# QP = [0,1]
QP_NAME = {0:"rc", 2:"ud"}
ORDER = ["one", "order", "norder"]
PORDER = ["order", "norder", "one"]
out_array = {"order":96, "norder": 48, "one":48}

INDEX = ["hash"]
QP = [0]
BATCHES = [1]
COROS = [0]
# COROS = [8]
OUT = 48




PB = [0, 1, 2, 3, 4, 5]
# COROS = [2]


# BATCHES = [1, 16, 32, 64, 128, 256, 512, 1024]

def run():
    name = "./data/" + exe + finger
    res = {}
    for test in itertools.product(PB, ORDER, COROS):
        if test[1] == "one" and (test[0] != 0):
            res[test] = res[(0,"one",0)]
            continue
        cmd = run_test(test[0], test[1], test[2])
        with open("tmp_cmd.sh", "w") as file2:
            file2.writelines("set -x\n");
            file2.writelines(cmd[0]);
        os.system("chmod +x ./tmp_cmd.sh")
        lat, _x = run_a_test("./tmp_cmd.sh", 50, 7)
        tp = read_tp(cmd[1], 0, 7)
        res[test] = [tp, lat]
        # cmd = "./clean.sh"
        # os.system(cmd)
    
    with open(name, "wb") as file:
        pickle.dump(res, file, True)
    print(res)
    kill_all()
    
def draw(name = "shit"):
    if (name == "shit"):
        name = "./data/" + exe + finger
    
    bar_res = {}

    for order in ORDER:
        bar_res[order] = {}

    with open(name, "rb") as file:
        res = pickle.load(file)
    # print(res)
        print(res)
        for p in PB:
            peibi_list = [p]
            plt.cla()

            for i in INDEX: # TODO:
                
                index_list = [i]
                

                for j in ORDER:
                    order_list = [j]
                    

                    x_line = []
                    y_line = []

                    for test in itertools.product(peibi_list, order_list, COROS):
                        if test in res:
                            if res[test] is None:
                                # print("error")
                                # print(test)
                                continue
                            # print(res[test][1])
                            if (float(res[test][0])<1):
                                continue;
                            
                            

                            xx = float(res[test][0])
                            yy = float(res[test][1][0][9][0:-1])
                            # print(j, int(out_array[j]))
                            # if int(sum[test[2]]) == int(out_array[j]):
                            bar_res[j][p] = xx
                            
                            y_line.append(yy) # p50 latency
                            x_line.append(xx) # throughput

                            plt.text(xx,yy,(sum[test[2]]))

                    type = ""
                    # plt.plot(x_line, y_line, label=f'{j}', ls="-.", marker="o")
                    # print(x_line, y_line)
            
            # plt.xlabel("throughput(Mops/s) " + name + " " + os.path.basename(__file__))
            # plt.ylabel("latency(us)") 
            # plt.legend()
            # plt.savefig("./pic/" + exe + PB_name[p] +  ".jpg")
    
    print(bar_res)
    for i in bar_res:
        print(*bar_res[i].values())
    plt.cla()
    x = PB.copy()
    total_width, n = 0.8, len(PB)
    width = total_width / n
    # x = x - (total_width - width) / 2

    
    for i,order in enumerate(PORDER):
        pos = PB.copy()
        for j,p in enumerate(pos):
            pos[j] = x[j] + i * width
        if (i!=1):
            plt.bar(pos, list(bar_res[order].values()), width=width, label=order)
        else:
            plt.bar(pos, list(bar_res[order].values()), width=width, label=order, tick_label=PB_name[0:len(PB)])
    # plt.bar(x, tick_label=PB_name)
    plt.legend()
    plt.xlabel(name)
    # plt.xlabel(out_array)
    plt.savefig("./pic/" + exe + "bar" +  ".jpg")
    # plt.bar(x + width, b, width=width, label='b')
    # plt.bar(x + 2 * width, c, width=width, label='c')
        

    # print(res)


def main():
    # load_switch()
    # run()
    finger_old = "202305252330"
    namex = "./data/" + exe + finger_old
    draw(namex)

if __name__ == '__main__':
  main()
