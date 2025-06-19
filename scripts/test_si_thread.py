from header import *

currentDateAndTime = datetime.now()
print("The current date and time is", currentDateAndTime)

localtime = time.localtime(time.time())
finger = time.strftime("%Y%m%d%H", time.localtime()) 

nodes = [116, 117, 116, 118, 119]
path = "/home/ljr/NetStore/build"
exe = "secondary_index"

#pic type
DN_THREADS = [4] #1
MN_THREADS = [2, 4, 8] #2
# MN_THREADS = [4] #2


#line type
GFLAGS = ["--visibility=true", "--visibility=false"]
# GFLAGS = ["--visibility=true"]
tmp_coros = [0,1,2,3,4,5,6,7,8]
# tmp_coros = [0]
COROS = [[1,1], [4,1], [4,2], [4,3], [4,4], [6,4], [8,4], [8,6], [8,8]] #3
# CORO_ARRAY = [[0,0,1,0,1,1], [0,2,1,1,1,1], [0,2,1,2,2,2], [0,3,1,3,2,2], [0,3,1,3,3,3], [0,4,1,4,3,3]]

# client

# INDEX = ["tree", "hash"]
# QP = [0,1]
# BATCHES = [1, 16, 32, 64, 128, 256, 512, 1024]

def run():
    res = {}
    for test in itertools.product(DN_THREADS, MN_THREADS, GFLAGS, tmp_coros):
        kill_all(nodes, exe)
        comm = f'--qp_type=1 {test[2]} --c_thread={COROS[test[3]][0]}  --dn_thread={test[0]} --mn_thread={test[1]} --read=0'
        args = comm + f' --coro_num={COROS[test[3]][1]}'
        # def run_test_common(NODES, EXE_NAME, ARGS, last_id, PATH):
        # if (test[2] == GFLAGS[0]):
        #     load_switch("./run_batch.sh")
        run_test_common(nodes, exe, args, 2, path)
        cmd = f'ssh root@10.0.2.{nodes[2]} "cd {path}; ./{exe} --node_id=2 {comm} --coro_num=1"'
        with open("tmp_cmd.sh", "w") as file2:
            file2.writelines("set -x\n");
            file2.writelines(cmd);
        os.system("chmod +x ./tmp_cmd.sh")
        lat, _x = run_a_test("./tmp_cmd.sh", 60, 6)

        kill_all(nodes, exe)
        # restp = 0
        restp = float(lat[0][3])

        for i, n in enumerate(nodes):
            if i <= 2:
                continue;
            cmd = f'ssh root@10.0.2.{n} "cat /tmp/log{i};"'
            with open("tmp_cmd.sh", "w") as file2:
                file2.writelines("set -x\n");
                file2.writelines(cmd);
            tp, _x = run_a_test("./tmp_cmd.sh", 60, 6)
            restp += float(tp[0][3])
        # print(restp)
        res[test] = [restp, lat]
        print(restp, lat[0][9][0:-1])
    
    with open("./data/" + exe + finger, "wb") as file:
        pickle.dump(res, file, True)
    # print(res)
    
def draw(name = "shit"):
    if (name == "shit"):
        name = "./data/" + exe + finger
    else:
        name = "./data/" + name
    with open(name, "rb") as file:
        res = pickle.load(file)
    # print(res)
        # print(res)
       

        for i in DN_THREADS:
            i_list = [i]
            for j in MN_THREADS:
                j_list = [j]
                plt.cla()
                for k in GFLAGS:
                    k_list = [k]

                    x_line = []
                    y_line = []

                    for test in itertools.product(i_list, j_list, k_list, tmp_coros):
                        if test in res:
                            if res[test] is None:
                                print("error")
                                print(test)
                                continue
                            # print(res[test][1])
                            # if (float(res[test][0])<1):
                            #     continue;
                            

                            xx = float(res[test][0])
                            yy = float(res[test][1][0][6][0:-1]) 
                            # 6:p50 8:p99
                            
                            y_line.append(yy) # p50 latency
                            x_line.append(xx) # throughput

                            plt.text(xx,yy,(COROS[test[3]]))

                    type = ""

                    plt.plot(x_line, y_line, label=f'{k}', ls="-.", marker="o")
                    print(*x_line, *y_line)
        
                plt.xlabel("throughput(Mops/s) " + name + " " + os.path.basename(__file__))
                plt.ylabel("latency(us)") 
                plt.legend()
                plt.savefig("./pic/" + exe + f'DN{i}+MN{j}' +".jpg")
        
    # print(res)


def main():
    # load_switch()
    run()

    name = exe + "2023052214"
    draw()

if __name__ == '__main__':
  main()
