#include "common.h"
#include "fss.h"
RPCServer::RPCServer(uint32_t n_id) {
	mm = 0;
	// UnlockWait = false;
	// conf = new Configuration();
	
	uint64_t block_count = 9ull * 1024 * 1024 * 1024 / 4096;
	if (n_id == 0) {
		mem = new MemoryManager(mm, 1, 9);
	}
	else {
		block_count = 8;
		mem = new MemoryManager(mm, 1, 0);
	}
	mm = mem->getDmfsBaseAddress();
	Debug::notifyInfo("DmfsBaseAddress = %lx, DmfsTotalSize = %lx",
		mem->getDmfsBaseAddress(), mem->getDmfsTotalSize());
	ServerCount = 1;
	// socket = new RdmaSocket(cqSize, mm, mem->getDmfsTotalSize(), conf, true, 0);
	// client = new RPCClient(conf, socket, mem, (uint64_t)mm);
	tx = new TxManager(mem->getLocalLogAddress(), mem->getDistributedLogAddress());
	// socket->RdmaListen();
	fs = new FileSystem((char *)mem->getMetadataBaseAddress(),
              (char *)mem->getDataAddress(),
              1024 * 20,/* Constructor of file system. */
              1024 * 30,
              block_count,
              1,    
              1);
	// fs->rootInitialize(n_id);
}