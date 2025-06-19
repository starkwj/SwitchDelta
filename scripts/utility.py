


import time 
import subprocess 
import signal 
import itertools

from time import monotonic as timer 
from time import gmtime, strftime 
from subprocess import Popen, PIPE, TimeoutExpired 
import os 
def run_a_test(cmd, wait_time=100, printx=16, printout=0):
  start = timer()
  # output = ""
  with Popen(cmd, shell=True, stdout=PIPE, preexec_fn=os.setsid) as process: 
    try: 
      output = process.communicate(timeout=wait_time)[0] 
      # print(output)
    except TimeoutExpired: 
      os.killpg(process.pid, signal.SIGINT) # send signal to the process group 
      output = process.communicate()[0] 
    print('Elapsed seconds: {:.2f}'.format(timer() - start)) 
    ret = []
    lines = output.split(b'\n')
    if printout:
      print(output)
    while (not ret):
      for line in lines:
        if f'({printx})' in str(line):
          print(str(line).split(','))  
          ret.append(str(line).split(','))
      if not ret:
        printx -= 1
    return ret, printx


def read_tp(file_name, tp_type, print=16):
  with open(file_name, "r") as file:
    lines  = file.readlines()
    tp = 0
    tpl = []
    for line in lines:
        if f'({print})' in str(line):
          tpl.append(str(line).split(','))
    return float(tpl[tp_type][3])