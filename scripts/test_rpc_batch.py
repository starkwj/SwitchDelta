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
finger = time.strftime("%Y%m%d%H", time.localtime()) 

nodes = [116, 117, 116, 115, 118, 119]
path = "/home/ljr/NetStore/build"
exe = "batch_Index_moti"

CORO_ARRAY= [[0,0,1,0,1,1], [0,2,1,1,1,1], [0,2,1,2,2,2], [0,3,1,3,2,2], [0,3,1,3,3,3], [0,4,1,4,3,3]]


def load_switch():
    cmd = "./run_switch_bash.sh run_batch.sh"
    os.system(cmd)

def kill_all():
    for i in nodes:
        cmd = f'ssh 10.0.2.{i} "pkill batch; pkill tmp_cmd"'
        print(cmd)
        os.system(cmd)

def run_test(index_type, qp_type, batch, coro):
    for i in nodes:
        cmd = f'ssh 10.0.2.{i} "pkill batch"'
        print(cmd)
        os.system(cmd)
    time.sleep(1)
    ret_cmd = f'last';
    tp_file = f'{path}/test{qp_type}{index_type}{batch}-{coro}'

    for i, node in enumerate(nodes):
        if (i == 0):
            cmd = f'ssh 10.0.2.{node} "cd {path}; ./{exe} --node_id={i} --qp_type={qp_type} --index={index_type} --batch={batch} > {tp_file} 2>&1 &"'
            os.system(cmd)
        elif (i == 2):
            cmd = f'ssh 10.0.2.{node} "cd {path}; ./{exe} --node_id={i} --qp_type={qp_type} --index={index_type} --batch={batch} --coro_num={CORO_ARRAY[coro][i]}"'
            ret_cmd = str(cmd)
        else:
            cmd = f'ssh 10.0.2.{node} "cd {path}; ./{exe} --node_id={i} --qp_type={qp_type} --index={index_type} --batch={batch} --coro_num={CORO_ARRAY[coro][i]} > /dev/null 2>&1 &"'
            os.system(cmd)
        print(cmd);
    
    return ret_cmd, tp_file
    # client

# INDEX = ["tree", "hash"]
# QP = [0,1]
QP_NAME = {0:"rc", 2:"ud"}

INDEX = ["hash"]
QP = [2]
# BATCHES = [1, 2, 4, 8, 16, 32, 64]
BATCHES = [1, 2, 4, 8, 16]
COROS = [0, 1, 2, 3, 4]


# BATCHES = [1, 16, 32, 64, 128, 256, 512, 1024]

def run():
    res = {}
    for test in itertools.product(INDEX, QP, BATCHES, COROS):
        cmd = run_test(test[0], test[1], test[2], test[3])
        with open("tmp_cmd.sh", "w") as file2:
            file2.writelines("set -x\n");
            file2.writelines(cmd[0]);
        os.system("chmod +x ./tmp_cmd.sh")
        lat, _x = run_a_test("./tmp_cmd.sh", 60, 6)
        tp = read_tp(cmd[1], 0, 6)
        res[test] = [tp, lat]
        # cmd = "./clean.sh"
        # os.system(cmd)
    
    with open("./data/" + exe + finger, "wb") as file:
        pickle.dump(res, file, True)
    print(res)
    kill_all()
    
def draw(name = "shit"):
    if (name == "shit"):
        name = "./data/" + exe + finger
    else:
        name = "./data/" + name
    with open(name, "rb") as file:
        res = pickle.load(file)
    # print(res)
        # print(res)
        plt.cla()

        for i in INDEX:
            
            index_list = [i]
            for j in QP:
                qp_list = [j]

                for k in BATCHES:

                    batch_list = [k]

                    x_line = []
                    y_line = []

                    for test in itertools.product(index_list, qp_list, batch_list, COROS):
                        if test in res:
                            if res[test] is None:
                                print("error")
                                print(test)
                                continue
                            # print(res[test][1])
                            if (float(res[test][0])<1):
                                continue;
                            

                            xx = float(res[test][0])
                            yy = float(res[test][1][0][6][0:-1])
                            
                            y_line.append(yy) # p50 latency
                            x_line.append(xx) # throughput

                            plt.text(xx,yy,(test))

                    type = ""

                    plt.plot(x_line, y_line, label=f'{QP_NAME[j]}+{i}+batch={k}', ls="-.", marker="o")
                    print(*x_line, *y_line)
        
        plt.xlabel("throughput(Mops/s) " + name + " " + os.path.basename(__file__))
        plt.ylabel("latency(us)") 
        plt.legend()
        plt.savefig("./pic/" + exe + ".jpg")
        
    # print(res)


def main():
    # load_switch()
    # run("batch")

    finger_old = "2023041820"
    name = "batch_res" + finger_old + "batch"
    draw(name)

if __name__ == '__main__':
  main()
