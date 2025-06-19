#include "fsrpc.h"
#include "filesystem.h"

DEFINE_uint64(keyspace, 100 * MB, "key space");
DEFINE_uint64(logspace, 100 * MB, "key space");

DEFINE_double(zipf, 0.0, "ZIPF");
DEFINE_int32(kv_size_b, 128, "test size B");
DEFINE_bool(visibility, false, "visibility");
DEFINE_bool(batch, false, "batch index and batch rpc");
DEFINE_uint32(batch_size, 16, "batch size");
DEFINE_uint32(read, 50, "read persentage");

DEFINE_uint32(block_size, BLOCK_SIZE, "block size");

DEFINE_int32(c_thread, 1, "client thread");
DEFINE_int32(dn_thread, 1, "dn_thread");
DEFINE_int32(mn_thread, 1, "mn_thread");

RPCServer * fs_server;
int test_percent[kTestTypeNum] = {0, 0, 0, 0, 50, 50};
int pre_sum[kTestTypeNum] = {};
TestType test_type[100];

rdma::GlobalConfig<rpc::kConfigServerCnt> global_config;
std::vector<rdma::ServerConfig> server_config;

TxManager* RPCServer::getTxManagerInstance() {
	return tx;
}
MemoryManager* RPCServer::getMemoryManagerInstance() {
	return mem;
}

KvKeyType *key_space;