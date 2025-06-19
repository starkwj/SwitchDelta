#pragma once

#include "rpc.h"
#include "qp_info.h"
#include <gflags/gflags_declare.h>
#include "fscommon.h"

constexpr int visibility = 2;
constexpr int kClientId = 2;
using KvKeyType = uint64_t;
using VersionType = uint64_t;
constexpr int kMaxDataSize = BLOCK_SIZE;


extern KvKeyType *key_space;

enum TestType {
  kEnumRI = 0,
  kEnumWI,
  kEnumRV,
  kEnumWV,
  kEnumR,
  kEnumW,
  kEnumSW,
  kEnumSRP,
  kEnumSRS,
};

DECLARE_uint64(keyspace);
DECLARE_uint64(logspace);

DECLARE_double(zipf);
DECLARE_int32(kv_size_b);
DECLARE_bool(visibility);
DECLARE_bool(batch);
DECLARE_uint32(batch_size);
DECLARE_uint32(read);

DECLARE_int32(c_thread);
DECLARE_int32(dn_thread);
DECLARE_int32(mn_thread);

DECLARE_uint32(block_size);

constexpr int kMaxCurrentIndexReq = 1024;
constexpr int kTestTypeNum = 6;

extern int test_percent[kTestTypeNum];
extern int pre_sum[kTestTypeNum];
extern TestType test_type[100];

extern rdma::GlobalConfig<rpc::kConfigServerCnt> global_config;
extern std::vector<rdma::ServerConfig> server_config;
