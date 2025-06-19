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
import time


def load_switch(batch_name, timex=120):
    cmd = f"./run_switch_bash.sh {batch_name}"
    while (1):
        start = timer()
        with Popen(cmd, shell=True, stdout=PIPE, preexec_fn=os.setsid) as process: 
            try: 
                output = process.communicate(timeout=timex)
                break
            except TimeoutExpired: 
                os.killpg(process.pid, signal.SIGINT) # send signal to the process group 
                # output = process.communicate()[0] 
            print('Elapsed seconds: {:.2f}'.format(timer() - start))
    # os.system(cmd)

def kill_all(NODES, exe_name):
    for i in NODES:
        cmd = f'ssh 10.0.2.{i} "pkill batch; pkill {exe_name}; pkill tmp_cmd.sh"'
        print(cmd)
        os.system(cmd)

def run_test_common(NODES, EXE_NAME, ARGS, last_id, PATH):
    # kill_all(NODES, EXE_NAME)
    # time.sleep(1)
    last_cmd = f'last';

    for i, node in enumerate(NODES):
        if (i == last_id):
            cmd = f'ssh 10.0.2.{node} "cd {PATH}; ./{EXE_NAME} --node_id={i} {ARGS}"'
            last_cmd = cmd
        else:
            cmd = f'ssh 10.0.2.{node} "cd {PATH}; ./{EXE_NAME} --node_id={i} {ARGS} > /tmp/log{i} 2>&1 &"'
            os.system(cmd)
        print(cmd);
    
    return last_cmd