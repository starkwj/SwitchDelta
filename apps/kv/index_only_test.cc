// #include "../../rdma/operation.h"
// #include "masstree_key.hh"


#include "zipf.h"
#include "sample.h"
#include <cassert>
#include <cmath>
#include <iterator>
#include "qp_info.h"
#include "rpc.h"
#ifdef OBATCH
#include "include/index_wrapper.h"
#endif

#include "include/test.h"

#ifdef COMT
#include "perfv2.h"
#include "bindcore.h"
#include "sm-coroutine.h"
#include "masstree_wrapper.h"
#endif


#define CTX_NUM 32

DEFINE_uint32(batch, 16, "batch size");
DEFINE_string(index, "tree", "index type");
DEFINE_uint64(keyspace, 200 * MB, "key space");
DEFINE_bool(insert, false, "insert");
DEFINE_bool(search, false, "insert");
DEFINE_double(zipf, 0.99, "zipf");


#ifdef OBATCH
ThreadLocalIndex_masstree* tree_index;
ThreadLocalIndex* hash_index;
#endif

#ifdef COMT
MasstreeWrapper* tree_;
#endif


struct Ctx {
    bool free;
};

Ctx ctx[CTX_NUM];

struct KvPair {
    uint64_t key;
    uint64_t value;
};

struct Batch_req {
	KvPair** req_ptr;
	KvPair* req;
	Batch_req() {
		req = (KvPair*)malloc(sizeof(KvPair) * FLAGS_batch);
		req_ptr = (KvPair**)malloc(sizeof(uint64_t) * FLAGS_batch);
	}

	static bool cmp(const KvPair* a, const KvPair* b) {
		return (*a).key < (*b).key;
	}

	static bool cmp_hash(const KvPair* a, const KvPair* b) {
		return std::hash<uint>()((*a).key) < std::hash<uint>()((*b).key);
	}

	void sort_batch() {
		if (FLAGS_index == "tree")
			std::sort(req_ptr, req_ptr + FLAGS_batch, cmp);
		else 
			std::sort(req_ptr, req_ptr + FLAGS_batch, cmp_hash);
	}
	void print() {
		for (uint32_t i = 0; i < FLAGS_batch; i++) {
			printf("key=%lx id=%lx\n", req_ptr[i]->key, (uint64_t)req_ptr[i]);
		}
		puts("-----------");
	}
};

inline void generate_key(uint i, uint64_t& key) {
	key = ((uint64_t)(std::hash<uint>()((rand() + 1) * 19210817)) << 32) + std::hash<uint>()(i * 10000079);
}

uint64_t* key_space;
struct zipf_gen_state state;
struct zipf_gen_state op_state;
struct Batch_req reqs;

#ifdef COMT
inline PROMISE(void) worker(int coro_id) {
    int cnt = 0;

    SUSPEND;

    // puts("??");

    while (true)
    {
        ctx[coro_id].free = false;

        
        if (FLAGS_search) 
            AWAIT tree_->search(reqs.req_ptr[coro_id]->key, reqs.req_ptr[coro_id]->value);
        else        
            AWAIT tree_->insert(reqs.req_ptr[coro_id]->key, reqs.req_ptr[coro_id]->value);

        ctx[coro_id].free = true;

        SUSPEND;
    }
    RETURN;
}
#endif

#ifdef OBATCH 
void read(uint64_t key, uint64_t* value) {
    if (FLAGS_index == "hash") {
        hash_index->get_long(key, value);
    }
    else if (FLAGS_index == "tree") {
        tree_index->get_long(key, value);
    }
}

void write(uint64_t key, uint64_t value) {
    if (FLAGS_index == "hash") {
        hash_index->put_long(key, value);
    }
    else if (FLAGS_index == "tree") {
        tree_index->put_long(key, value);
    }
}
#endif



void test() {
    BindCore(0);

    perf::PerfTool* reporter; 

    uint64_t load_size = FLAGS_keyspace;
    if (FLAGS_insert) {
        load_size = FLAGS_keyspace / 2;
    }


#ifdef OBATCH    
    if (FLAGS_index == "hash") {
        hash_index = ThreadLocalIndex::get_instance("cuckoo64");
        for (uint64_t i = 0; i < load_size; i++) {
            hash_index->put_long(key_space[i],i);
        }
    }
    else if (FLAGS_index == "tree") {
        tree_index = ThreadLocalIndex_masstree::get_instance();
        tree_index->thread_init(0);
        for (uint64_t i = 0; i < load_size; i++) {
            tree_index->put_long(key_space[i], key_space[i] * 2);
        }
    }
#endif

#ifdef COMT
    tree_ = new MasstreeWrapper();
    tree_->thread_init(0);

    for (uint32_t i = 0; i < FLAGS_keyspace; i++) {
        sync_wait_coro(tree_->insert(key_space[i], key_space[i] * 2));
    }
    uint64_t ret;

    sync_wait_coro(tree_->search(key_space[66], ret));
    printf("ret = %lx %lx\n", key_space[66], ret);

#endif


    reporter = perf::PerfTool::get_instance_ptr();
    reporter->worker_init(0);
    reporter->thread_begin(0);

    puts("Begin!");

#ifdef COMT
    puts("COMT");
    std::vector<ermia::coro::task<void>> tasks(CTX_NUM);
    for (int i = 0; i < FLAGS_batch; i++) {
        // puts("?");
        tasks[i] = worker(i);
        tasks[i].start();
        ctx[i].free = true;
    }
    puts("COMT INIT");
#endif
    
    // reqs.init();
    uint64_t test_cnt = 0;
    bool flag = false;
    
    while (true) {

        for (int i = 0; i < FLAGS_batch; i++) {
            if (test_cnt % 1000 == 0) {
                if (test_cnt != 0)
                    reporter->many_end(1000,0,0,1);
                reporter->begin(0,0,1);
                reporter->begin(0,0,0);
                flag = true;
            }
            test_cnt++;

            // reporter->begin(0,i,0);
            if (FLAGS_insert)
                generate_key(test_cnt + load_size, (reqs.req[i]).key);
            else
                reqs.req[i].key = key_space[mehcached_zipf_next(&state)];
            reqs.req_ptr[i] = &reqs.req[i];
        }

        reqs.sort_batch();

        auto last_key = (uint64_t)(-1);

#ifdef OBATCH
        for (int i = 0; i < FLAGS_batch; i++) {
            if (last_key != reqs.req_ptr[i]->key) {
                if (FLAGS_search)
                    read(reqs.req_ptr[i]->key, &reqs.req_ptr[i]->value);
                else
                    write(reqs.req_ptr[i]->key, reqs.req_ptr[i]->value);
                last_key = reqs.req_ptr[i]->key;
            }
            uint32_t id = (reqs.req_ptr[i] - reqs.req); 

            // printf("id = %d\n",id);
            // reporter->end(0,id,0);
        }
#endif

#ifdef COMT

        int cnt = 0;
        // puts("batch begin");
        for (int i = 0; i < FLAGS_batch; i++) {
            if (last_key != reqs.req_ptr[i]->key) {
                cnt++;
                tasks[i].resume();
                if (ctx[i].free == true) {
                    cnt--;
                }
                last_key = reqs.req_ptr[i]->key;
            }
        }

        // puts("batch phase1");
        
        while (cnt > 0) {
            for (int i = 0; i < FLAGS_batch; i++) {
                if (ctx[i].free != true) {
                    tasks[i].resume();
                    if (ctx[i].free == true) {
                        cnt--;
                    }
                }
            }
        }
        // puts("batch end");
        // sleep(10);
#endif



        if (flag) {
            reporter->end(0,0,0);
            flag = false;
        }

        
    }
}



int main(int argc, char** argv) {

	FLAGS_logtostderr = 1;
	gflags::SetUsageMessage("Usage ./rdma_clean_client --help");
	gflags::ParseCommandLineFlags(&argc, &argv, true);
	google::InitGoogleLogging(argv[0]);

	perf::PerfConfig perf_config = {
        .thread_cnt = 1,
        .coro_cnt = 2,
        .type_cnt = 2,
        .slowest_latency = 333,
    };
	perf::PerfTool* reporter = perf::PerfTool::get_instance_ptr(&perf_config);

    
    reporter->new_type("lat "+std::to_string(FLAGS_batch));
    reporter->new_type("tp "+std::to_string(FLAGS_batch));

	reporter->master_thread_init();

	key_space = (uint64_t*)malloc(sizeof(uint64_t) * FLAGS_keyspace);

    for (uint64_t i = 0; i < FLAGS_keyspace; i++) {
        generate_key(i, key_space[i]);
    }

    srand(233);
    mehcached_zipf_init(&state, FLAGS_keyspace - 1, FLAGS_zipf, rand() % 1000);

	mehcached_zipf_init(&op_state, 100, 0.0, rand() % 1000);

	std::thread* th = new std::thread(test);

	while (true) {
		reporter->try_wait();
		reporter->try_print(perf_config.type_cnt);
	}

    BindCore(1);

	th->join();

	return 0;
}
