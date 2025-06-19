from header import *

currentDateAndTime = datetime.now()
print("The current date and time is", currentDateAndTime)

localtime = time.localtime(time.time())
finger = time.strftime("%Y%m%d%H%M", time.localtime()) 

nodes = [116, 117, 116, 115, 119]
path = "/home/ljr/NetStore/build"
store_name = os.path.basename(__file__)[0:-3]

exe = "kv_co --visibility=true --batch=true --batch_size=16"
#pic type
# KEY_SPACE = [1024, 32 * 1024, 512 * 1024, 2^20, 10 * 2^20, 100 * 2^20]
# zipf 

#line type
# build_scripts = ["./build.sh", "./build_overwrite.sh"]
# run_scripts = ["./run_batch.sh", "./overwriterun.sh"]
# zipf = [0, 0.95, 0.96, 0.97, 0.98, 0.99, 1.0]
# zipf = [0, 0.95, 0.96, 0.97, 0.98, 0.99, 1.0]
# zipf = [0.0, 0.8, 0.9, 0.99]
tmp_coros = [0,1,2,3,4,5]
build_scripts = ["./build_fw.sh"]
run_scripts = ["./run_fw.sh"]
zipf = [0.0, 0.8, 0.9, 0.99, 1.0, 1.2]
# zipf = [0.99]
# tmp_coros = [4]
COROS = [[8,1], [8,2], [8,3], [8,4], [8,6], [8,8]] #3
# CORO_ARRAY = [[0,0,1,0,1,1], [0,2,1,1,1,1], [0,2,1,2,2,2], [0,3,1,3,2,2], [0,3,1,3,3,3], [0,4,1,4,3,3]]

# client

# INDEX = ["tree", "hash"]
# QP = [0,1]
# BATCHES = [1, 16, 32, 64, 128, 256, 512, 1024]

def run():
    res = {}
    for i,x in enumerate(build_scripts):
        # load_switch(x, 3000)
        owiw_list = {run_scripts[i]}
        for test in itertools.product(owiw_list, zipf, tmp_coros):
            if (test[1] != 0.99 and test[2] != 4):
                continue
            kill_all(nodes, "kv_co")
            kill_all(nodes, "kv")
            load_switch(test[0])
            comm = f'--qp_type=1  --c_thread={COROS[test[2]][0]}  --dn_thread=4 --mn_thread=4 --read=50 --zipf={test[1]}'
            args = comm + f' --coro_num={COROS[test[2]][1]}'
            # def run_test_common(NODES, EXE_NAME, ARGS, last_id, PATH):
            run_test_common(nodes, exe, args, 2, path)
            cmd = f'ssh root@10.0.2.{nodes[2]} "cd {path}; ./{exe} --node_id=2 {comm} --coro_num=1"'
            with open("tmp_cmd.sh", "w") as file2:
                file2.writelines("set -x\n");
                file2.writelines(cmd);
            os.system("chmod +x ./tmp_cmd.sh")
            lat, _x = run_a_test("./tmp_cmd.sh", 120, 6)

            kill_all(nodes, "kv_co")
            kill_all(nodes, "kv")
            # restp = 0
            restp = float(lat[1][3])
            fasttp = float(lat[5][3])
            slowtp = float(lat[6][3])

            for i, n in enumerate(nodes):
                if i <= 2:
                    continue;
                cmd = f'ssh root@10.0.2.{n} "cat /tmp/log{i};"'
                with open("tmp_cmd.sh", "w") as file2:
                    file2.writelines("set -x\n");
                    file2.writelines(cmd);
                tp, _x = run_a_test("./tmp_cmd.sh", 60, 6)
                restp += float(tp[1][3])
                slowtp += float(tp[6][3])
                fasttp += float(tp[5][3])
            # print(restp)
            res[test] = [restp, lat, slowtp, fasttp]
            print(restp, lat[0][9][0:-1])
    
    with open("./data/" + store_name  + finger, "wb") as file:
        pickle.dump(res, file, True)
    # print(res)
    
def draw(name = "shit"):
    if (name == "shit"):
        name = "./data/" + store_name + finger
    else:
        name = "./data/" + name
    with open(name, "rb") as file:
        res = pickle.load(file)
        # print(res)
        # print(res)
        
        # pic1
        plt.cla()
        for j in run_scripts:
            j_list = [j]
            i_list = zipf

            x_line = []
            y_line = []

            y2_line = []
            y3_line = []
            latall_line = []
            latf_line = []
            coros = [4]
            for test in itertools.product(j_list, i_list, coros):
                if test in res:
                    if res[test] is None:
                        print("error")
                        print(test)
                        continue
                    yy2 = float(res[test][0])
                    xx = test[1];
                    yy = 1.0 - (float(res[test][2]) / (float(res[test][0]) + 0.000000000000001))
                    yy3 =float(res[test][3]) / (float(res[test][0]) + 0.000000000000001)

                    # 6:p50 8:p99
                    
                    y_line.append(yy) # %
                    y2_line.append(yy2) # thp
                    y3_line.append(yy3) # % fast
                    latall_line.append(res[test][1][1][6][0:-1])
                    latf_line.append(res[test][1][5][6][0:-1])

                    x_line.append(xx) # throughput

            type = ""

            plt.plot(x_line, y3_line, label=f'{j}', ls="-.", marker="o")
            print("name [zipf] [%] [tph2] [fast%] [all lat] [index lat]", j, *x_line, *y_line, *y2_line, *y3_line, *latall_line, *latf_line) 
        plt.xlabel("coro_num " + name + " " + os.path.basename(__file__))
        plt.ylabel("%") 
        plt.legend()
        plt.savefig("./pic/" + store_name + f'pic1' +".jpg")
        
        # pic2
        plt.cla()
        for j in run_scripts:
            j_list = [j]
            x_line = []
            y_line = []

            x2_line = []
            y2_line = []
            y3_line = []
            latall_line = []
            latf_line = []
            
            for i in [0.99]:
                i_list = [i]
                for test in itertools.product(j_list, i_list, tmp_coros):
                    if test in res:
                        if res[test] is None:
                            print("error")
                            print(test)
                            continue
                        yy2 = float(res[test][0])
                        
                        xx = COROS[test[2]][0] * COROS[test[2]][1] * 2;

                        yy = float(res[test][2]) / (float(res[test][0]) + 0.000000000000001)
                        yy3 =float(res[test][3]) / (float(res[test][0]) + 0.000000000000001)

                        # 6:p50 8:p99
                        
                        y_line.append(yy) # %
                        y2_line.append(yy2) # thp
                        y3_line.append(yy3) # % fast
                        latall_line.append(res[test][1][1][6][0:-1])
                        latf_line.append(res[test][1][5][6][0:-1])
                        x_line.append(xx) #

                type = ""

            plt.plot(x_line, y3_line, label=f'{i} all + {j}', ls="-.", marker="o")
            print("name [coro] [%] [tph2] [fast%] [all lat] [index lat]", j, *x_line, *y_line, *y2_line, *y3_line, *latall_line, *latf_line) 
        plt.xlabel("zipf " + name + " " + os.path.basename(__file__))
        plt.ylabel("%") 
        plt.legend()
        plt.savefig("./pic/" + store_name + f'pic2' +".jpg")
    # print(res)


def main():
    # load_switch()
    # run()

    name = store_name + "202309170123"
    draw(name)

if __name__ == '__main__':
  main()
