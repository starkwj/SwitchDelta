#include <gperftools/profiler.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "timer.h"

extern "C" {
void init_embedded_memcached(size_t max_bytes, int num_threads,
                             uint8_t hash_power);
void embedded_put(char *key, int nkey, char *value, int vlen, int thread_id);
bool embedded_get(char *key, int nkey, char *value, int *vlen, int thread_id);
}

int main() {
  init_embedded_memcached(16ull * 1024 * 1024 * 1024, 4, 24);

  uint64_t kMaxKey = 100 * 1024ull * 1024;

  ProfilerStart("/tmp/wq");
  perf::Timer timer;

  char value[10];
  int vlen;

  for (int i = 0; i < 2; ++i) {
    timer.begin();
    for (uint64_t k = 0; k < kMaxKey; ++k) {
      embedded_put((char *)(&k), 8, (char *)(&k), 8, 0);
    }
    timer.end_print(kMaxKey);

    timer.begin();
    for (uint64_t k = 0; k < kMaxKey; ++k) {
      embedded_get((char *)(&k), 8, value, &vlen, 0);
    }
    timer.end_print(kMaxKey);
    printf("-----------------\n");
  }
  ProfilerStop();

  while (true)
    ;

  // timer.begin();
  // for (uint64_t k = 0; k < kMaxKey; ++k) {
  //     embedded_get((char *)(&k), 8, value, &vlen, 0);
  //     // printf("%ld\n", *(uint64_t *)value);
  // }
  // timer.end_print(kMaxKey);

  return 0;
}