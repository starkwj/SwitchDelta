


import sys 
import time 
import subprocess 
import signal 
import itertools

import pickle
# import matplotlib.pyplot as plt
import matplotlib.pyplot as plt
from utility import run_a_test
# repeat experiments for the following times
import os
REPEAT = 5

from datetime import datetime
currentDateAndTime = datetime.now()
print("The current date and time is", currentDateAndTime)

import time
localtime = time.localtime(time.time())
finger = time.strftime("%Y%m%d%H", time.localtime()) 

print(finger)


# coro_num = [1, 2, 4, 6, 8, 10, 12, 14, 15]
coro_num = [1, 3, 4, 5, 6, 8]
read = [0, 25]
v = ["true", "false"]

# vv = [["true", "true"], ["true", "false"], ["false", "false"]]
vv = ["no", "+v", "v+b"]

def run_visibility(test_name):
  res = {}
  for test in itertools.product(read, vv, coro_num):
    print(test)
    cmd = "./clean.sh"
    os.system(cmd)
    cmd = "./run_switch.sh"
    os.system(cmd)
    cmd = "./atest_new.sh " +  \
          test_name + " " + \
          str(test[0]) + " " + \
          str(test[2]) + " " + \
          (["true", "false"][test[1] == "no"]) + " " + \
          "false" + " " + \
          (["false", "true"][test[1] == "v+b"])
    res[test], _x = run_a_test(cmd)
    
    cmd = "./clean.sh"
    os.system(cmd)
  
  with open("v_res" + finger + test_name, "wb") as file:
    pickle.dump(res, file, True)
  print(res)
    
def draw_visibility(test_name):
  # finger = "2023041016"
  with open("v_res" + finger + test_name, "rb") as file:
    res = pickle.load(file)
    # print(res)
    for ri in read:
      plt.cla()
      read_list = []
      read_list.append(ri)
      for v_i in vv:
        v_list = []
        v_list.append(v_i)
        axi_all_t = []
        axi_all_l = []
        axi_read_t = []
        axi_read_l = []
        axi_write_t = []
        axi_write_l = []

        for test in itertools.product(read_list, v_list, coro_num):
          if test in res:
            # print(test)
            if res[test] is None:
              print("error")
              print(test)
              continue
            axi_all_l.append(float(res[test][0][8])) # p50 latency
            axi_all_t.append(float(res[test][0][3])) # throughput

            axi_write_l.append(float(res[test][2][8]))
            axi_write_t.append(float(res[test][2][3]))
  
            axi_read_l.append(float(res[test][1][8]))
            axi_read_t.append(float(res[test][1][3]))
        type = ""
        # if (v_i == "true"): 
        #   type = "visibility"
        # else:
        #   type = "baseline"
        # print(axi_write_t, axi_write_l)
        # print(axi_read_t, axi_read_l)
        plt.plot(axi_write_t, axi_write_l, label="write:" + v_i, ls="-.")
        # plt.plot(axi_all_t, axi_all_l, label="all:" + v_i)
        plt.plot(axi_read_t, axi_read_l, label="read:" + v_i)
      
      plt.xlabel("throughput(Mops/s), " + "read=" + str(ri))
      plt.ylabel("latency(us)")
      plt.legend()
      plt.savefig("./test_v" + str(ri) + ".jpg")
    
    print(res)


  # ariaFB
  # n_lock_managers = [1, 2, 3, 4, 6]
  # for n_lock_manager in n_lock_managers:
  #   for partition_num in partition_nums:
  #     for i in range(REPEAT):
  #       cmd = get_cmd_string(machine_id, ips, port + i)
  #       print(f'./bench_tpcc --logtostderr=1 --id={machine_id} --servers="{cmd}" --protocol=AriaFB --partition_num={partition_num} --threads={threads} --batch_size={batch_size} --query={query} --neworder_dist={neworder_dist} --payment_dist={payment_dist} --same_batch=False --ariaFB_lock_manager={n_lock_manager}')


def run_cable_cache():
  res = {}
  for test in itertools.product(read, v, coro_num):
    
    cmd = "./run_switch.sh"
    os.system(cmd)
    cmd = "./atest.sh " + str(test[2]) + " false " + str(test[1]) + " " + str(test[0])
    print(cmd)
    res[test], _x = run_a_test(cmd)
    cmd = "./clean.sh"
    run_a_test(cmd)
  
  with open("c_res", "wb") as file:
    pickle.dump(res, file, True)
  print(res)

def draw_cable_cache():
  with open("c_res", "rb") as file:
    res = pickle.load(file)
    
    for ri in read:
      plt.cla()
      read_list = []
      read_list.append(ri)
      for v_i in v:
        v_list = []
        v_list.append(v_i)
        axi_read_t = []
        axi_read_l = []

        for test in itertools.product(read_list, v_list, coro_num):
          if test in res:
            if res[test] is None:
              print("error")
              print(test)
              continue
            if res[test] == []:
              print("con't read data[0]")
              print(test)
              continue
            print(res[test])
            axi_read_l.append(float(res[test][0][8]))
            axi_read_t.append(float(res[test][0][3]))
        type1 = ""
        if (v_i == "true"): 
          type1 = "cable_cache"
        else:
          type1 = "baseline"
        plt.plot(axi_read_t, axi_read_l, label="read:" + type1)
        for a,b in zip(axi_read_t,axi_read_l):
          plt.annotate('(%s,%s)'%(a,b),xy=(a,b),xytext=(-20,10),textcoords='offset points')
      
      plt.xlabel("throughput(Mops/s), " + "read=" + str(ri))
      plt.ylabel("latency(us)")
      plt.legend()
      plt.savefig("./test_c_r_" + str(ri) + ".jpg")
    
    # print(res)

def main():
  # run_cable_cache()
  # draw_cable_cache()
  run_visibility("secondary_index")
  draw_visibility("secondary_index")

if __name__ == '__main__':
  main()
