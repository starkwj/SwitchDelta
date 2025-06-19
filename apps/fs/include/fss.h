#pragma once
#include <thread>
#include <unordered_map>
#include <vector>
#include "Configuration.h"
#include "mempool.h"
#include "global.h"
#include "filesystem.h"
#include "TxManager.h"

class RPCServer {
private:
	// Configuration *conf;
	uint64_t mm;
	int ServerCount;
	TxManager *tx;
public:
	MemoryManager *mem;
	FileSystem *fs;
	RPCServer(uint32_t n_id);
	TxManager* getTxManagerInstance();
	MemoryManager* getMemoryManagerInstance();
	
	~RPCServer();
};