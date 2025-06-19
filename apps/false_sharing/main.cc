#include "bindcore.h"
#include "perfv2.h"
#include "size_def.h"
#include <bits/stdc++.h>

const int kThreadCnt = 8;
const int kCoroCnt = 2;
const int kTypeCnt = 12;
static perf::PerfConfig perf_config = {
    .thread_cnt = kThreadCnt,
    .coro_cnt = kCoroCnt,
    .type_cnt = kTypeCnt,
    .slowest_latency = 300,
    .fastest_latency = 0,
};

struct LZQ {
    uint64_t a[8];
}__attribute__((packed));


LZQ lzq_f_small;
LZQ lzq_f_big[1 * KB];

LZQ lzq_s_small[8];
LZQ lzq_s_big[1 * KB][8];

thread_local uint64_t lzq_l_big[1 * KB];
thread_local uint64_t lzq_l_small;


void worker(int thread_id) {
    perf::PerfTool* reporter = perf::PerfTool::get_instance_ptr(&perf_config);
    BindCore(thread_id);
    reporter->worker_init(thread_id);

    uint64_t test[10000];
    for (int i = 0; i < 1e4; i++) {
        test[i] = rand();
    }

    // uint64_t* res = (uint64_t *)malloc(8*1024);

    // int cnt = 0;
    printf("thread_id=%d\n", thread_id);
    while (true) {

        int x = rand() % 12;
        reporter->begin(thread_id, 0, x);
        
        for (int i = 0; i < 1e4; i++) {
            switch (x)
            {
            case 0:
                lzq_l_small = test[i];
                break;
            case 1:
                lzq_l_big[i % (1 * KB)] = test[i];
                break;
            case 2:
                lzq_l_small += test[i];
                break;
            case 3:
                lzq_l_big[i % (1 * KB)] += test[i];
                break;
            case 4:
                lzq_s_small[thread_id].a[thread_id] = test[i];
                break;
            case 5:
                lzq_s_big[i % (1 * KB)][thread_id].a[thread_id] = test[i];
                break;
            case 6:
                lzq_s_small[thread_id].a[thread_id] += test[i];
                break;
            case 7:
                lzq_s_big[i % (1 * KB)][thread_id].a[thread_id] += test[i];
                break;
            case 8:
                lzq_f_small.a[thread_id] = test[i];
                break;
            case 9:
                lzq_f_big[i % (1 * KB)].a[thread_id] = test[i];
                break;
            case 10:
                lzq_f_small.a[thread_id] += test[i];
                break;
            case 11:
                lzq_f_big[i % (1 * KB)].a[thread_id] += test[i];
                break;
            
            default:
                
                break;
            }
        }

        reporter->end(thread_id, 0, x);
        
    }
    // res[cnt++ % ]
    return;
}

int main() {
    
    std::thread* th[kThreadCnt];
    perf::PerfTool* reporter = perf::PerfTool::get_instance_ptr(&perf_config);
    reporter->new_type("loc w");
    reporter->new_type("l w x1k");
    reporter->new_type("local rw");
    reporter->new_type("l rw x1k");
    reporter->new_type("s w");
    reporter->new_type("s w x1k");
    reporter->new_type("s rw");
    reporter->new_type("s rw x1k");
    reporter->new_type("false w");
    reporter->new_type("f w x1k");
    reporter->new_type("false rw");
    reporter->new_type("f rw x1k");
    reporter->master_thread_init();
    
    for (int i = 0; i < kThreadCnt; i++) {
        th[i] = new std::thread(std::bind(&worker, i));
    }

    BindCore(8);
    while (true) {
        reporter->try_wait();
        reporter->try_print(kTypeCnt);
    }

    for (int i = 0; i < kThreadCnt; i++) {
        th[i]->detach();
    }
    return 0;

}