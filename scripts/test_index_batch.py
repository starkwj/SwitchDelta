
from header import *
from datetime import datetime
currentDateAndTime = datetime.now()
print("The current date and time is", currentDateAndTime)

import time
localtime = time.localtime(time.time())
finger = time.strftime("%Y%m%d%H", time.localtime()) 
finger = "2023041913"

OP_NAME = {"true":"insert", "false":"write"}
OP = ["true", "false"]
BATCHES = [1, 2, 4, 8, 16]
# BATCHES = [1]
nodes=[116]
path = "/home/ljr/NetStore/build"
exe = "index_only_test"

def run_test(op, batch):
    for i in nodes:
        cmd = f'ssh 10.0.2.{i} "pkill index_only"'
        print(cmd)
        os.system(cmd)
    
    time.sleep(1)
    cmd = f'cd {path}; ./{exe} --index=tree --batch={batch} --insert={op}'

    return cmd
    # client

def run(test_name):
    res = {}
    for test in itertools.product(OP, BATCHES):
        cmd = run_test(test[0], test[1])
        with open("tmp_cmd.sh", "w") as file2:
            file2.writelines("set -x\n");
            file2.writelines(cmd);
        os.system("chmod +x ./tmp_cmd.sh")
        res[test], _x = run_a_test("./tmp_cmd.sh", 100, 11)
    
    with open("./data/"+ exe + finger, "wb") as file:
        pickle.dump(res, file, True)
    print(res)
    # kill_all()
    
def draw(test_name = "shit"):
    # finger = "2023041016"
    if (test_name=="shit"):
        name = "./data/"+ exe + finger
    else:
        name = "./data/" + test_name
    with open("./data/"+ exe + finger , "rb") as file:
        res = pickle.load(file)
        plt.cla()


       
        for i in OP:
            i_list = [i]
            
            x_line=[]
            y_line=[]
            for test in itertools.product(i_list, BATCHES):
                if test in res:
                    if res[test] is None:
                        print("error")
                        print(test)
                        continue
                    # print(res[test][1])
                    if (float(res[test][1][3])<1):
                        continue;
                    xx = float(res[test][1][3])
                    yy = float(res[test][0][8])
                    y_line.append(yy) # p50 latency
                    x_line.append(xx) # throughput
                    plt.text(xx,yy,(test, xx, yy))
            plt.plot(x_line, y_line, label=f'{OP_NAME[i]}', ls="-.", marker="o")
            print(*x_line, *y_line)
        
        plt.xlabel("throughput(Mops/s) " + name + " " + os.path.basename(__file__))
        plt.ylabel("latency(us)")
        plt.legend()
        plt.savefig("./pic/"+ exe + ".jpg")
        
    # print(res)


def main():
    # load_switch()
    # run(exe)
    name = exe + "2023041913"
    draw(name)

if __name__ == '__main__':
  main()
