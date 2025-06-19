
#pragma once

#include "timer.h"
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string>
#include <unistd.h>
#include <algorithm>
#include <map>
#include <cstring>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <emmintrin.h>

namespace perf {



class DrawTable {
    static void DrawLine(std::stringstream& ss, const std::vector<int>& max,
        int columns) {
        for (int i = 0; i < columns; i++) {
            ss << "+-";
            for (int j = 0; j <= max[i]; j++) {
                ss << '-';
            }
        }
        ss << '+' << std::endl;
    }

public:
    static void DrawTB(std::stringstream& ss, const std::vector<int>& max,
        const std::vector<std::string>& header,
        const std::vector<std::vector<std::string>>& str) {
        int row = str.size();
        int columns = str[0].size();
        DrawLine(ss, max, columns);
        for (size_t i = 0; i < header.size(); i++) {
            ss << "| " << std::setw(max[i]) << std::setiosflags(std::ios::left)
                << std::setfill(' ') << header[i] << ' ';
        }
        ss << '|' << std::endl;
        DrawLine(ss, max, columns);
        for (int i = 0; i < row; i++) {
            for (int j = 0; j < columns; j++) {
                ss << "| " << std::setw(max[j]) << std::setiosflags(std::ios::left)
                    << std::setfill(' ');
                ss << str[i][j] << ' ';
            }
            ss << '|' << std::endl;
        }
        DrawLine(ss, max, columns);
    }
};

// inline int32_t min(int32_t a, int32_t b) { return a < b ? a : b; }

struct PerfConfig {
    int thread_cnt;
    int coro_cnt;
    int type_cnt{ 1 };

    bool enable_latency{ true };
    bool enable_throughput{ true };

    int phase_cnt{ 1 };


    uint64_t slowest_latency{ 200 }; // us
    uint64_t fastest_latency{ 0 };
    int print_interval{ 5 }; // s
};

class PerfTool {

private:
    std::vector<std::string> header = { "name", "ops/s", "top-1", "avg", "p50", "p99", "p999", "last-1"};
    std::vector<int> size = { 10,10,10,10,10,10, 10, 10};
    PerfTool() = default;
    PerfTool(PerfConfig config_) {
        config = config_;
        name.clear();

        type_cnt = config.type_cnt;
        phase_cnt = config.phase_cnt;

        kTPEpoch = config.print_interval;

        int total_num = config.thread_cnt * config.coro_cnt;
        thread_num_ = config.thread_cnt;
        report_ok = (uint64_t *)malloc(thread_num_ * sizeof(uint64_t));
        fast = (uint64_t *)malloc(type_cnt * sizeof(uint64_t));
        last = (uint64_t *)malloc(type_cnt * sizeof(uint64_t));
        memset(report_ok, 0, sizeof(uint64_t) * thread_num_);
        instance_num_ = config.coro_cnt;
        kLatencyRange = config.slowest_latency - config.fastest_latency;

        config_latency();

        auto size = (kLatencyArray + 1) * sizeof(uint64_t);
        ans = (uint64_t*)malloc(size);

        if (phase_cnt == 1) { // latency only, no breakdown
            int latency_num_ = type_cnt * phase_cnt; // 1
            printf("latency_num_ %d\n", latency_num_);
            latency_bucket = new uint64_t * [latency_num_];
            latency_bucket_back = new uint64_t * [latency_num_];

            for (int i = 0; i < type_cnt; i++) {
                // std::cout<<  i * phase_cnt << std::endl;
                auto id = i * phase_cnt;

                latency_bucket[id] = new uint64_t[kLatencyArray + 1];
                memset(latency_bucket[id], 0, size);
                latency_bucket_back[id] = new uint64_t[kLatencyArray + 1];
                memset(latency_bucket_back[id], 0, size);
            }
        }

        if (type_cnt != -1) {
            req_count = (uint64_t**)malloc(sizeof(uint64_t*) * total_num);
            memset(req_count, 0, sizeof(uint64_t*) * total_num);
        }

        tp_offset = (type_cnt + 7) / 8 * 8; // cache line

        return;
    }

public:

    /**
     * @brief configuration of concurrent reqs
     *
     * @param thread_num: threads in a process
     * @param instance_num: async reqs in a thread
     */
    static PerfTool* get_instance_ptr(PerfConfig* config_ = nullptr) {
        static PerfTool perf_tool(*config_);
        return &perf_tool;
    }

    void master_thread_init() {
        for (int i = name.size(); i < type_cnt; i++)
            name.push_back("type_" + std::to_string(i));
        running = true;
    }

    void worker_init(int thread_id) {
        if (running == false) return;
        // printf("%d\n", instance_num_);
        for (int i = 0; i < instance_num_; i++)
            init_tp(thread_id, i);
        if (thread_id == 0) {
            timer = new Timer * [type_cnt];
            for (int i = 0; i < type_cnt; i++) {
                timer[i] = new Timer[instance_num_];
            }
        }
    }

    void master_init_all_worker() {
        if (running == false) return;
        for (int i = 0; i < thread_num_; i++)
            worker_init(i);
    }

    void begin(int thread_id, int coro_id, int type_id) {
        if (thread_id == 0)
            timer[type_id][coro_id].begin();
    }

    void new_type(std::string s, int thread_id_ = 0) {
        if (thread_id_ == 0)
            name.push_back(s);
    }

    void phase_flag(int thread_id, int coro_id, int type_id) {
        // TODO: breakdown
    }

    uint64_t get_pos(int coro_id, int type_id, uint64_t res) {
        // uint64_t latency_value = timer[type_id][coro_id].end();
        if (res < fast[type_id]) {
            fast[type_id] = res;
        }
        if (res > last[type_id]) {
            last[type_id] = res;
        }
        auto pos = std::max(
            config.fastest_latency * kUS,
            std::min(res, config.slowest_latency * kUS - 1));
        
        return (pos - config.fastest_latency * kUS) * ns2unit / kLatencyUnit;
    }

    uint64_t get_res(int coro_id, int type_id) {
        uint64_t latency_value = timer[type_id][coro_id].end();
        return latency_value;
    }

    uint64_t end(int thread_id, int coro_id, int type_id, int phase_id = 0) {
        ++(req_count[thread_id][type_id]);
        if (thread_id == 0) {
            uint64_t res = get_res(coro_id, type_id);
            ++latency_bucket[type_id * phase_cnt + phase_id][get_pos(coro_id, type_id, res)];
            return res;
        }
        return 0;
    }

    uint64_t many_end(int cnt, int thread_id, int coro_id, int type_id, int phase_id = 0) {
        (req_count[thread_id][type_id]) += cnt;
        if (thread_id == 0) {
            uint64_t res = get_res(coro_id, type_id);
            ++latency_bucket[type_id * phase_cnt + phase_id][get_pos(coro_id, type_id, res)];
            return res;
        }
        return 0;
    }

    void end_copy(int thread_id, int coro_id, int type_id, uint64_t res = 0, int phase_id = 0) {
        ++(req_count[thread_id][type_id]);
        if (thread_id == 0) {
            ++latency_bucket[type_id  * phase_cnt + phase_id][get_pos(coro_id, type_id, res)];
        }
    }

    void many_end_copy(int cnt, int thread_id, int coro_id, int type_id, uint64_t res = 0, int phase_id = 0) {
        (req_count[thread_id][type_id]) += cnt;
        if (thread_id == 0) {
            latency_bucket[type_id  * phase_cnt + phase_id][get_pos(coro_id, type_id, res)] += cnt;
        }
    }

    void end_and_end_copy(int thread_id, int coro_id, int type_id_A, int type_id_B, int phase_id = 0) {
        uint64_t ans = end(thread_id, coro_id, type_id_A);
        end_copy(thread_id, coro_id, type_id_B, ans);
    }

    void thread_begin(int thread_id) {
        assert(thread_id<=thread_num_);
        report_ok[thread_id] = 1;
        _mm_mfence();
    }


    void try_wait() {
        while (true) {
            int i = 0;
            for (i = 0; i < thread_num_; i++) {
                if (report_ok[i] == 0) {
                    // printf("wait for thread(%d) %d\n", i, thread_num_);
                    sleep(1);
                    break;
                }
            }
            if (i >= thread_num_) 
                break;
        }
        sleep(kTPEpoch);
    }

    static std::string point2(double x) {
        uint64_t tmp = x;
        uint64_t tmp2 = x * 100;
        tmp2 %= 100;

        return std::to_string(tmp) + "." + std::to_string(tmp2 / 10) + std::to_string(tmp2 % 10);
    }

    static std::string tp_to_string(double tp) {
        if (tp > 1e9)
            return point2(tp / 1e9) + "G";
        if (tp > 1e6)
            return point2(tp / 1e6) + "M";
        if (tp > 1e3)
            return point2(tp / 1e3) + "K";
        return point2(tp);
    }

    std::string lat_to_string(double lat) {
        if (lat > (1e9 * 60 * 60 * 24 * 365)) 
            return "~" + point2(lat / 1e9 / (60 * 60 * 24 * 365)) + "y";
        if (lat > (1e9 * 60 * 60 * 24)) 
            return point2(lat / 1e9 / (60 * 60 * 24)) + "d";
        if (lat > (1e9 * 60 * 60)) 
            return point2(lat / 1e9 / (60 * 60)) + "h";
        if (lat > (1e9 * 60)) 
            return point2(lat / 1e9 / ((60))) + "mins";
        if (lat > 1e9)
            return point2(lat / 1e9) + "s";
        if (lat > 1e6)
            return point2(lat / 1e6) + "ms";
        if (lat > 1e3)
            return point2(lat / 1e3) + "us";
        return point2(lat) + "ns";
    }



    bool try_print(int print_num = 1, bool cdf = false, bool detail = false)
    {
        interval = tp_epoch_time.end();
        if (interval > 1000000000ull * kTPEpoch) {
            uint64_t tp_of_this_epoch;
            std::stringstream ss;
            std::vector<std::vector<std::string>> vec;
            vec.clear();
            for (int i = 0; i < std::min(print_num, type_cnt); i++) {
                interval = tp_epoch_time.end();
                tp_of_this_epoch = merge_count_by_id(i, true);

                if (i == 0) {
                    mpl_tps = tp_of_this_epoch;
                }
                auto tp = tp_of_this_epoch / (interval / 1e9);

                print_latency_file(i, cdf);

                std::vector<std::string> line;
                line.clear();
                line.push_back(name[i]);
                line.push_back(tp_to_string(tp));
                line.push_back(lat_to_string(fast[i]));
                line.push_back(lat_to_string(average));
                line.push_back(lat_to_string(l_p50));
                line.push_back(lat_to_string(l_p99));
                line.push_back(lat_to_string(l_p999));
                line.push_back(lat_to_string(last[i]));

                printf("%s(%d),%f,%f,%f, ,%f,%f,%f,%f,%f\n",
                    name[i].c_str(),
                    tp_epoch_id,
                    tp, tp / (1 << 10), tp / (1 << 20),
                    average / 1000.0, l_p50 / 1000.0, l_p90 / 1000.0, l_p99 / 1000.0, l_p999 / 1000.0);
                if (name[i] != ("type_" + std::to_string(i)))
                    vec.push_back(line);
            }
            if (vec.size() != 0) {
                DrawTable::DrawTB(ss, size, header, vec);
                std::cout << ss.str();
            }

            fflush(stdout);
            tp_epoch_time.begin();
            tp_epoch_id++;

            memset(fast, 0x3f3f3f3f, sizeof(uint64_t) * type_cnt);
            memset(last, 0, sizeof(uint64_t) * type_cnt);
            return true;
        }
        return false;
    }

    // TODO:
    void throughput_listen_ms(uint64_t kTPListenLength, int tp_id = 0)
    {
        int kTPListenEpoch = 5; // ms
        int old = dup(1);
        FILE* fp = freopen("tp_listen.txt", "w", stdout);

        uint64_t print_repeat = kTPListenLength * 1000 / kTPListenEpoch;
        uint64_t* print_array = (uint64_t*)malloc((print_repeat + 1) * sizeof(uint64_t));
        uint64_t print_i = 0;
        Timer print_time;
        print_time.begin();
        while (print_i < print_repeat) {
            if (print_time.end() > 1000000ull * kTPListenEpoch) {
                print_time.begin();
                print_array[print_i++] = merge_count_by_id(tp_id);
            }
        }

        for (uint64_t i = 0; i < print_repeat; i++)
            printf("%lu %lu\n", i * kTPListenEpoch, print_array[i] / kTPListenEpoch);
        fflush(fp);
        dup2(old, 1);
        free(print_array);
        // printf("[Throughput listen]: End. Total: %lu\n", throughput);
    }

    /*latency*/




    void print_latency_file(int type_id, bool cdf = false, int append = 1)
    {
        ans[0] = 0;
        average = 0;

        if (latency_bucket[type_id] != nullptr) {
            for (int i = 0; i < kLatencyArray; i++) {
                // printf("%lu %lu, ", latency_bucket[type_id][i], latency_bucket_back[type_id][i]);
                uint64_t tmp = latency_bucket[type_id][i];
                average += (tmp - latency_bucket_back[type_id][i]) * i;
                ans[i] += (tmp - latency_bucket_back[type_id][i]);
                latency_bucket_back[type_id][i] = tmp;
                ans[i + 1] = ans[i];
            }
        }
        else {
            puts("print_latency_file error");
            exit(0);
        }

        printf("%lf/%ld \n", average, ans[kLatencyArray]);

        if (ans[kLatencyArray] != 0) {
            average = average / ans[kLatencyArray] * kLatencyUnit / ns2unit + config.fastest_latency * kUS;
        }
        else
            average = 0;

        int old = 0;
        FILE* fp = nullptr;
        if (cdf) {
            old = dup(1);
            printf("old = %d %s\n", old, ("latency" + name[type_id] + ".txt").c_str());
            fp = freopen(("latency" + name[type_id] + ".txt").c_str(), append == 1 ? "a" : "w", stdout);
        }

        // freopen("/dev/console", "w", stdout);
        // uint64_t l_min = 0;
        l_p50 = 0, l_p90 = 0, l_p99 = 0, l_p999 = 0;
        // fast = 0;

        for (int i = 0; i < kLatencyArray; i++) {
            // if (ans[i] != 0 && l_min == 0) {
            //     l_min = i * kLatencyUnit / ns2unit + config.fastest_latency * kUS;
            //     fast = l_min;
            // }
            if (ans[i] == 0) continue;
            if (ans[i] * 1.0 / ans[kLatencyArray] < 0.5 || l_p50 == 0) {
                l_p50 = i * kLatencyUnit / ns2unit + config.fastest_latency * kUS;
            }
            if (ans[i] * 1.0 / ans[kLatencyArray] < 0.9 || l_p90 == 0) {
                l_p90 = i * kLatencyUnit / ns2unit + config.fastest_latency * kUS;
            }
            if (ans[i] * 1.0 / ans[kLatencyArray] < 0.99 || l_p99 == 0) {
                l_p99 = i * kLatencyUnit / ns2unit + config.fastest_latency * kUS;
            }
            if (ans[i] * 1.0 / ans[kLatencyArray] < 0.999 || l_p99 == 0) {
                l_p999 = i * kLatencyUnit / ns2unit + config.fastest_latency * kUS;
            }
            if (cdf)
                printf("%lu,%f,%lf\n", ans[i], i * (kLatencyUnit / 1000.0 / ns2unit) + config.fastest_latency, ans[i] * 1.0 / ans[kLatencyArray]);
        }

        // puts("[latency_cdf]: end");
        if (cdf) {
            printf("[avg,50,90,99,999,min]: %lf %lf, %lf, %lf, %lf, %lf\n", average / 1000.0, l_p50 / 1000.0, l_p90 / 1000.0, l_p99 / 1000.0,
                l_p999 / 1000.0, fast[type_id] / 1000.0);
            fflush(fp);
            dup2(old, 1);
        }
        // fclose(fp);
        // printf("%s [avg,50,90,99,999,min]: %lf %lf, %lf, %lf, %lf, %lf\n", filename.c_str(),
        //     average, l_p50 / 1000.0, l_p90 / 1000.0, l_p99 / 1000.0,
        //     l_p999 / 1000.0, l_min / 1000.0);

    }

    /* TODO: breakdown
    inline void segment_sum_begin(int thread_id)
    {
        latency_segment_sum = 0;
        return;
    }

    inline void segment_begin(int thread_id)
    {
        timer[thread_id]->begin();
        return;
    }

    inline void segment_end(int thread_id)
    {
        latency_segment_sum += timer[thread_id]->end();
        return;
    }

    inline void segment_sum_end(int thread_id)
    {
        latency_segment_sum += timer[thread_id]->end();
        latency_bucket[thread_id][min(latency_segment_sum / kLatencyUnit, kLatencyRange - 1)]++;
        return;
    }
    */



private:
    bool running = false;
    PerfConfig config;
    int thread_num_;
    int instance_num_;
    const int kMaxTP = 8;
    uint64_t kLatencyUnit = 100; // ns
    uint64_t kLatencyRange = 200; // input us
    uint64_t ns2unit=10;
    int kUS = 1000;

    const int kLatencyArray = 10000; // static
    void config_latency() { 
        kLatencyUnit = kLatencyRange * 10000 / kLatencyArray; 
        ns2unit = 10; 
        }

    std::vector<std::string> name;
    uint64_t* ans;
    /*perf tool type*/
    int tp_offset;
    int phase_cnt{ 1 };
    int type_cnt = -1;

    uint64_t** latency_bucket; // [type_id * phase_id][time];
    uint64_t** latency_bucket_back;
    Timer** timer;  // [type_id][coro_id];
    uint64_t* latency_segment_sum; // [type_id]

    uint64_t** req_count; // [thread_id][type_id '% 8 == 0']
    uint64_t* volatile report_ok;

    uint64_t mpl_tps;


    Timer tp_epoch_time;
    int kTPEpoch = 2;
    int tp_epoch_id = 0;
    double average = 0;
    uint64_t* fast = 0;
    uint64_t* last = 0;
    uint64_t interval;
    uint64_t l_p50, l_p90, l_p99, l_p999;

    volatile uint64_t epoch[24];
    volatile uint64_t g_epoch;
    volatile uint16_t g_mpl;

    void init_tp(int thread_id, int coro_id) {
        if (running == false)
            return;

        if (thread_id >= thread_num_ || coro_id >= instance_num_) {
            printf("%s: Your code is shit!", __func__);
            exit(-1);
        }

        if (type_cnt != -1) { // a req_count per thread
            req_count[thread_id] = new uint64_t[tp_offset * 2];
            memset(req_count[thread_id], 0, tp_offset * 2 * sizeof(uint64_t));
        }
        return;
    }

    inline uint64_t merge_count_by_id(int id = 0, bool printf_detail = false)
    {
        // puts("");
        printf("%s: ", name[id].c_str());
        uint64_t tp_of_this_epoch = 0;
        for (int i = 0; i < thread_num_; i++) {
            if (req_count[i] == nullptr)
                continue;

            if (printf_detail)
                printf("%lu ", req_count[i][id] - req_count[i][id + tp_offset]);

            uint64_t tmp = req_count[i][id];
            tp_of_this_epoch += tmp - req_count[i][id + tp_offset];
            req_count[i][id + tp_offset] = tmp;
        }
        if (printf_detail) {
            printf(" [total:%lu]\n", tp_of_this_epoch);
        }
        return tp_of_this_epoch;
    }
};
}
