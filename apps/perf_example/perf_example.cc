#include "perfv2.h"
#include <bits/stdc++.h>
const int kThreadCnt = 4;
const int kCoroCnt = 2;
const int kTypeCnt = 2;
static perf::PerfConfig perf_config = {
    .thread_cnt = kThreadCnt,
    .coro_cnt = kCoroCnt,
    .type_cnt = kTypeCnt,
    .slowest_latency = 400,
    .fastest_latency = 1,
};

int fake_sleep_1000(int cnt) {
    int t = 0;
    for (int i = 0; i < cnt * 1e3; i++) {
        t += t * i + i;
    }
    return t;
}
int ans[100];
void worker(int thread_id) {
    perf::PerfTool* reporter = perf::PerfTool::get_instance_ptr(&perf_config);
    reporter->worker_init(thread_id);

    int i = 0;
    printf("thread_id=%d", thread_id);
    while (true) {
        i++;
        if (i % 2) {
            reporter->begin(thread_id, 0, 0);
            usleep(1);
            reporter->end(thread_id, 0, 0);
        }
        else {
            reporter->begin(thread_id, 0, 1);
            ans[i%100] = fake_sleep_1000(1);
            // usleep(0);
            reporter->end(thread_id, 0, 1);
        }
    }

    return;
}

int main() {
    
    std::thread* th[kThreadCnt];
    perf::PerfTool* reporter = perf::PerfTool::get_instance_ptr(&perf_config);
    reporter->new_type("sleep");
    reporter->new_type("for1000");
    reporter->master_thread_init();
    
    for (int i = 0; i < kThreadCnt; i++) {
        th[i] = new std::thread(std::bind(&worker, i));
    }

    while (true) {
        reporter->try_wait();
        reporter->try_print(kTypeCnt);
    }

    for (int i = 0; i < kThreadCnt; i++) {
        th[i]->detach();
    }
    return 0;

}