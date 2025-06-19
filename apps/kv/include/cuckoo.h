#pragma once

#include "base_index.h"
#include "tools/factory.h"

class CuckooHash : public BaseIndex {
public:
  CuckooHash(const BaseIndexConfig &config)
      : BaseIndex(config)
  {
    value_size_ = config.value_size;

    hash_table_ = new std::unordered_map<uint64_t, uint64_t>();

    uint64_t value_shm_size = config.capacity * config.value_size;

  }

  void Get(const uint64_t key, std::string &value, unsigned tid) override {

    base::PetKVData shmkv_data;
    std::shared_lock<std::shared_mutex> _(lock_);
    auto iter = hash_table_->find(key);

    if (iter == hash_table_->end()) {
      value = std::string();
    } else {
      uint64_t &read_value = iter->second;
      shmkv_data = *(base::PetKVData *)(&read_value);
      char *data = shm_malloc_.GetMallocData(shmkv_data.shm_malloc_offset());
#ifdef XMH_VARIABLE_SIZE_KV
      int size = shm_malloc_.GetMallocSize(shmkv_data.shm_malloc_offset());
#else
      int size = value_size_;
#endif
      value = std::string(data, size);
    }
  }

  void Put(const uint64_t key, const std::string_view &value,
           unsigned tid) override {

    base::PetKVData shmkv_data;
    char *sync_data = shm_malloc_.New(value.size());
    shmkv_data.SetShmMallocOffset(shm_malloc_.GetMallocOffset(sync_data));
    memcpy(sync_data, value.data(), value.size());

    std::unique_lock<std::shared_mutex> _(lock_);
    hash_table_->insert({key, shmkv_data.data_value});
  }

  

  std::pair<uint64_t, uint64_t> RegisterPMAddr() const override {
    return std::make_pair(0, 0);
  }

  ~CuckooHash() {
    std::cout << "exit CuckooHash" << std::endl;
    // hash_table_->hash_name();
  }

private:
  std::unordered_map<uint64_t, uint64_t> *hash_table_;
  
  std::shared_mutex lock_;

  hashtable_options_t hashtable_options;
  uint64_t counter = 0; // NOTE(fyy) IDK what is this

  std::string dict_pool_name_;
  size_t dict_pool_size_;
  int value_size_;
#ifdef XMH_SIMPLE_MALLOC
  base::PersistSimpleMalloc shm_malloc_;
#else
  base::PersistLoopShmMalloc shm_malloc_;
#endif
  base::ShmFile valid_shm_file_; // 标记 shm 数据是否合法
};

FACTORY_REGISTER(BaseKV, KVEngineMap, KVEngineMap, const BaseKVConfig &);