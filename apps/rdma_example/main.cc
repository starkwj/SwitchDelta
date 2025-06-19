#include "zipf.h" 
#include "sample.h"
#include "size_def.h"

constexpr uint64_t kKeySpace = 100ull * MB;
constexpr double zipf = 0.99;

sampling::StoRandomDistribution<>* sample; /*Fast*/

struct zipf_gen_state state;
struct zipf_gen_state op_state;

// 

int main() {
    uint64_t* cnt = new uint64_t[kKeySpace];
    

    puts("nic");

    for (int r = 1; r <= 15; r++) {
        memset(cnt, 0, kKeySpace * sizeof(uint64_t));
        mehcached_zipf_init(&state, kKeySpace, zipf, r * rand() & 0x0000FFFFF);
        for (uint64_t i = 0; i < 10 * MB; i++) {
            auto res = mehcached_zipf_next(&state);
            // if (i<100){
            //     printf("%d ",res);
            // }
            cnt[res]++;
        }
        puts("\n---");
        for (int i = 0; i < 10; i++) {
            if (cnt[i] == 0) {
                printf("!");
            }
            printf("%lf ", cnt[i] * 1.0 / (100*MB));
        }
        puts("\n");
    }

    puts("\nsto");

    sampling::StoRandomDistribution<>* sample; 
    for (int r = 0; r < 10; r++) {
        memset(cnt, 0, kKeySpace * sizeof(uint64_t));
        
        sampling::StoRandomDistribution<>::rng_type rng(r);
		sample = new sampling::StoZipfDistribution<>(rng, 0, kKeySpace-1, zipf);
        for (uint64_t i = 0; i < 10 * MB; i++) {
            cnt[sample->sample()]++;
        }
        delete sample;
        for (int i = 0; i < 20; i++) {
            printf("%lf ", cnt[i] * 1.0 / (100*MB));
        }
        puts("");
    }


}
