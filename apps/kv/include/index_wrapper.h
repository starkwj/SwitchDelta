
#include <cstdint>
#include <gflags/gflags.h>
#include <libcuckoo/cuckoohash_map.hh>
#include "masstree_wrapper_string.h"
#include <string>
#include <vector>
using KvKeyType = uint64_t;
using VersionType = uint64_t;
DECLARE_uint64(keyspace);

struct ValueAndVersion {
    uint64_t value;
    VersionType version;
};

class ThreadLocalIndex {
public:
	// std::unordered_map<uint64_t, uint64_t> table;
	static ThreadLocalIndex* get_instance(std::string index_type = "cuckoo32") {
        static ThreadLocalIndex index(index_type);
        return &index;
    }
	// static ThreadLocalIndex* get_instance_local() {
	// 	thread_local static ThreadLocalIndex local_index;
	// 	return &local_index;
	// }
    libcuckoo::cuckoohash_map<uint32_t, uint32_t> table;
    libcuckoo::cuckoohash_map<KvKeyType, KvKeyType> table2;
    libcuckoo::cuckoohash_map<KvKeyType, ValueAndVersion> table3;

	ThreadLocalIndex(std::string &index_type) {
        if (index_type == "cuckoo32") {
            table.clear();
            table.reserve(FLAGS_keyspace);
        }
        else if (index_type == "cuckoo64") {
            table2.clear();
            table2.reserve(FLAGS_keyspace);
        }
        else if (index_type == "cuckoo128") {
            table3.clear();
            table3.reserve(FLAGS_keyspace);
        }
        else if (index_type == "masstree") {

        }
	}

	bool get(uint32_t key, uint32_t* addr_addr) {
		return table.find(key, *addr_addr);
	}

	bool put(uint32_t key, uint32_t addr) {
		table.insert(key, addr);
		return true;
	}

	bool get_long(KvKeyType &key, uint64_t* addr_addr) {
		return table2.find(key, *addr_addr);
	}

	bool put_long(KvKeyType &key, uint64_t addr) {
		table2.insert(key, addr);
		return true;
	}

    bool get_value_version(KvKeyType key, ValueAndVersion * value) {
        return table3.find(key, * value);
    }

    bool put_value_version(KvKeyType key, ValueAndVersion * value) {
        table3.insert(key, *value);
        return true;
    }

    bool get_and_put_value_version(KvKeyType key, ValueAndVersion* value, ValueAndVersion* old_value) {
        // return 
        return table3.new_insert(key, *value, *old_value);
        // return true;
    }
};

class ThreadLocalIndex_masstree {
public:
    static ThreadLocalIndex_masstree* get_instance() {
        static ThreadLocalIndex_masstree index;
        return &index;
    }

    void thread_init(int thread_id) {
        mt_->thread_init(thread_id);
    }
	

	bool get(uint32_t key, uint32_t* addr_addr) {
		// return table.find(key, *addr_addr);
        return false;
	}

	bool put(uint32_t key, uint32_t addr) {
		// table.insert(key, addr);
		return true;
	}

	bool get_long(KvKeyType &key, uint64_t* addr) {
		uint64_t val;
        bool found = mt_->search(key, val);
        if (found) {
            *addr = (uint64_t)val; // TODO:
            return true;
        } 
        return found;
	}

	bool put_long(KvKeyType &key, uint64_t addr) {
        mt_->insert(key, (uint64_t)addr);
		return true;
	}

    bool get_vector_key(void * key_addr, int key_size, uint64_t& value) {
        bool found = mt_->search(key_addr, key_size, value);
        return found;
    }

    int scan(void * key_addr, int key_size, std::vector<uint64_t>& res, int cnt) {
        mt_->scan(key_addr, key_size, res, cnt);
        return res.size();
    }

    bool put_vector_key(void* key_addr, int key_size, uint64_t value) {
        // printf("insert %lx%lx, %lx", *(uint64_t *)(key_addr), *(uint64_t*)(key_addr+8), value);
        mt_->insert(key_addr, key_size, value);
        return true;
    }

    bool remove(void* key_addr, int key_size) {
        return mt_->remove(key_addr, key_size);
        // return true;
    }


    // virtual void Scan(const Slice &key, int cnt,
    //                 std::vector<ValueType> &vec) override {
    //     mt_->scan(*(KeyType *)key.data(), cnt, vec);
    // }
private:
    MasstreeWrapper *mt_;
    ThreadLocalIndex_masstree() {
        mt_ = new MasstreeWrapper();
	}
    // DISALLOW_COPY_AND_ASSIGN(ThreadLocalIndex_masstree);
};


