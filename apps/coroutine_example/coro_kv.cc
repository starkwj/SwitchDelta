#include "zipf.h"
#include "bindcore.h"

#ifdef CBMT
#include "masstree_btree.h"
#include "dbcore/sm-coroutine.h"
#endif

#ifdef COMT
#include "sm-coroutine.h"
#include "masstree_wrapper.h"
#endif

#include "perfv2.h"
#include "size_def.h"

#define CTX_NUM 4
#define COROTEST

DEFINE_uint64(keyspace, 100 * MB, "key space");
DEFINE_uint64(optype, 0, "0: search, 1: insert");

struct zipf_gen_state state;

#ifdef CBMT
constexpr ermia::epoch_num cur_epoch = 0;
#endif

uint64_t* key_space;

struct Ctx {
    uint64_t key;
    uint64_t value;
};

std::vector<ermia::coro::task<void>> tasks(CTX_NUM);
std::vector<ermia::coro::generator<bool>> coroutines_(CTX_NUM);

int finish_cnt;

Ctx ctx[CTX_NUM];

#ifdef CBMT
ermia::ConcurrentMasstree* tree_;

#endif
#ifdef COMT
MasstreeWrapper* tree_;
#endif

#ifdef CBMT
    ermia::ConcurrentMasstree::threadinfo ti(0);
#endif


inline ermia::coro::generator<bool> worker_g(int coro_id) {



    while (true) {
        finish_cnt++;
        // ctx[coro_id].key = mehcached_zipf_next(&state);
        ctx[coro_id].key = finish_cnt % FLAGS_keyspace;
        ctx[coro_id].value = ctx[coro_id].key * ctx[coro_id].key;
#ifdef CBMT
        uint32_t tmp = ctx[coro_id].value;
        std::coroutine_handle<> x = tree_->search_coro(ermia::varstr((char*)(&key_space[ctx[coro_id].key]), sizeof(uint64_t)), tmp,
            ti, nullptr).get_handle();
        while (!x.done()) {
            x.resume();
            SUSPEND;
        }
        x.destroy();
        
        // printf("out:%d %d\n", ctx[coro_id].key, ret);
#endif
    }
    RETURN true;
}



inline PROMISE(void) worker(int coro_id) {
    int cnt = 0;

#ifdef CBMT
    ermia::TXN::xid_context context_mock;
    context_mock.begin_epoch = 0;
    context_mock.owner = ermia::XID::make(0, 0);
    context_mock.xct = nullptr;
    ermia::OID ret;
#endif

    SUSPEND;

    while (true)
    {
        finish_cnt++;
        // ctx[coro_id].key = mehcached_zipf_next(&state);
        ctx[coro_id].key = finish_cnt % FLAGS_keyspace;
        ctx[coro_id].value = ctx[coro_id].key * ctx[coro_id].key;

        // printf("begin key = %d\n",ctx[coro_id].key);

#ifdef CBMT
// printf("key = %d\n",ctx[coro_id].key);
        if (FLAGS_optype == 1) {
            AWAIT tree_->insert(
                ermia::varstr((char*)(&key_space[ctx[coro_id].key]), sizeof(uint64_t)),
                ctx[coro_id].value,
                &context_mock, nullptr, nullptr);

        }
        else if (FLAGS_optype == 0) {
            AWAIT tree_->search(
                ermia::varstr((char*)(&key_space[ctx[coro_id].key]), sizeof(uint64_t)),
                ret,
                cur_epoch, nullptr);
        }
#endif

#ifdef COMT

        if (FLAGS_optype == 1) {
            AWAIT tree_->insert(key_space[ctx[coro_id].key], ctx[coro_id].value);
        }
        else if (FLAGS_optype == 0) {
            AWAIT tree_->search(key_space[ctx[coro_id].key], ctx[coro_id].value);
        }

#endif
        // printf("over key = %d\n",ctx[coro_id].key);
    }
    RETURN;
}


inline void generate_key(uint i, uint64_t& key) {
    uint64_t tmp = rand();
    key = (tmp << 32) + rand();
}

int main(int argc, char** argv) {


    gflags::ParseCommandLineFlags(&argc, &argv, true);
    google::InitGoogleLogging(argv[0]);

    BindCore(0);

    key_space = (uint64_t*)malloc(sizeof(uint64_t) * FLAGS_keyspace);

    for (uint i = 0; i < FLAGS_keyspace; i++) {
        generate_key(i, key_space[i]);
    }

#ifdef CBMT
    ermia::config::threads = 1;
    // ermia::thread::Initialize();
    ermia::config::tls_alloc = true;
    ermia::config::init();
    ermia::MM::prepare_node_memory();
    tree_ = new ermia::ConcurrentMasstree();
    ermia::TXN::xid_context context_mock;
    context_mock.begin_epoch = 0;
    context_mock.owner = ermia::XID::make(0, 0);
    context_mock.xct = nullptr;
#endif
#ifdef COMT
    tree_ = new MasstreeWrapper();
    tree_->thread_init(0);
#endif

    // puts("?");

#ifdef ADV_COROUTINE
    puts("ADV_COROUTINE!");
#endif

    uint64_t test_cnt = FLAGS_keyspace / 20;


    finish_cnt = 0;

    perf::Timer timer;

    // preload

    for (uint32_t i = 0; i < FLAGS_keyspace; i++) {
#ifdef CBMT
        sync_wait_coro(
            tree_->insert(
                ermia::varstr((char*)&key_space[i], sizeof(uint64_t)),
                i * i,
                &context_mock, nullptr, nullptr));
#endif
#ifdef COMT
        sync_wait_coro(tree_->insert(key_space[i], i * i));
#endif
    }

    mehcached_zipf_init(&state, FLAGS_keyspace - 1, 0, rand() % 1000);

    puts("begin running");

#ifndef COROTEST
    {
        const uint32_t key_length = 4;
        uint64_t value = 100;

        // insert records

        timer.begin();

        for (uint32_t i = 0; i < test_cnt; i++) {
            // sync_wait_coro(
            //     tree_->insert(
            //         ermia::varstr((char*)&key_space[mehcached_zipf_next(&state)], sizeof(uint64_t)),
            //         i * i,
            //         &context_mock, nullptr, nullptr));
#ifdef CBMT
            ermia::OID ret;
            if (FLAGS_optype == 1) {
                sync_wait_coro(
                    tree_->insert(
                        ermia::varstr((char*)&key_space[mehcached_zipf_next(&state)], sizeof(uint64_t)),
                        i * i,
                        &context_mock, nullptr, nullptr));
            }
            else if (FLAGS_optype == 0) {
                sync_wait_coro(tree_->search(
                    ermia::varstr((char*)&key_space[mehcached_zipf_next(&state)], sizeof(uint64_t)),
                    ret,
                    cur_epoch, nullptr));
            }
#endif
#ifdef COMT
            uint64_t ret;

            if (FLAGS_optype == 1) {
                sync_wait_coro(tree_->insert(key_space[mehcached_zipf_next(&state)], i * i));
            }
            else if (FLAGS_optype == 0) {
                sync_wait_coro(tree_->search(key_space[mehcached_zipf_next(&state)], ret));
            }
#endif

            finish_cnt++;

        }

        // uint64_t ret;

        // constexpr ermia::epoch_num cur_epoch = 0;
    }
#else

    for (int i = 0; i < CTX_NUM; i++) {
        ctx[i].key = test_cnt / CTX_NUM * i;
    }

    for (int i = 0; i < CTX_NUM; i++) {
        // puts("?");
        tasks[i] = worker(i);
        tasks[i].start();
    }

    timer.begin();

    while (finish_cnt < test_cnt) {
        for (int i = 0; i < CTX_NUM; i++) {
            tasks[i].resume();
        }
    }



#endif

    uint64_t res = timer.end();
    uint64_t key = 66;


#ifdef CBMT
    ermia::OID ret = 0;
    sync_wait_coro(tree_->search(
        ermia::varstr((char*)&key_space[key], sizeof(uint64_t)),
        ret,
        cur_epoch, nullptr));
    printf("%d != %d\n", ret, 66);
#endif

#ifdef COMT
    uint64_t ret = 0;
    sync_wait_coro(tree_->search(key_space[key], ret));
    printf("%lu != %lu\n", ret, 66);
#endif




    std::cout << test_cnt * 1.0 / (res * 1.0 / KB) << "M ops/s" << std::endl;


#ifdef CBMT
    
    finish_cnt = 0;

    std::vector<std::coroutine_handle<>> handles;
    handles.clear();

    for (int i = 0; i < CTX_NUM; i++) {
        handles.emplace_back(worker_g(i).get_handle());
    }

    timer.begin();
    
    while (finish_cnt < test_cnt) {
        for (int i = 0; i < CTX_NUM; i++) {
            handles[i].resume();
        }
    }

    res = timer.end();
    
    std::cout << test_cnt * 1.0 / (res * 1.0 / KB) << "M ops/s" << std::endl;
#endif


}