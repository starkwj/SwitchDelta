#pragma once
#include <atomic>
#define kMaxShardDN (1<<10)
struct Timestamp32 {
    // int global_cnt;
    
    std::atomic<uint64_t> tss[kMaxShardDN][8];

    void init() {
        for (int i = 0; i < kMaxShardDN; i++) {
            tss[i][0] = 1;
        }
    }

    uint32_t get_next_ts(uint16_t key) {
        uint32_t ret = ++tss[key % kMaxShardDN][0];
        return __builtin_bswap32(ret);
    }

    uint32_t get_shard_id(uint64_t key) {
        return key % kMaxShardDN;
    }

};