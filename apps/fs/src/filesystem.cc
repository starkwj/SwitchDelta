/*** File system class. ***/

/** Version 2 + modifications for functional model. **/

/** Included files. **/
#include "filesystem.h"
// #include "RPCServer.hpp"
#include "fss.h"
extern RPCServer *fs_server;
bool Dotx = true;
uint64_t TxLocalBegin() {
	if (!Dotx)
		return 0;
	fs_server->getTxManagerInstance()->TxLocalBegin();
	// printf("TxLocalBegin %lx %lx\n", fs_server, fs_server->getTxManagerInstance());
	return 0;
}

void TxWriteData(uint64_t TxID, uint64_t address, uint64_t size) {
	if (!Dotx)
		return;
	fs_server->getTxManagerInstance()->TxWriteData(TxID, address, size);
}

uint64_t getTxWriteDataAddress(uint64_t TxID) {
	if (!Dotx)
		return 0;
	return fs_server->getTxManagerInstance()->getTxWriteDataAddress(TxID);
}

void TxLocalCommit(uint64_t TxID, bool action) {
	if (!Dotx)
		return;
	fs_server->getTxManagerInstance()->TxLocalCommit(TxID, action);
}

uint64_t TxDistributedBegin() {
	if (!Dotx)
		return 0;
	return fs_server->getTxManagerInstance()->TxDistributedBegin();
}

void TxDistributedPrepare(uint64_t TxID, bool action) {
	if (!Dotx)
		return;
	fs_server->getTxManagerInstance()->TxDistributedPrepare(TxID, action);
}

void TxDistributedCommit(uint64_t TxID, bool action) {
	if (!Dotx)
		return;
	fs_server->getTxManagerInstance()->TxDistributedCommit(TxID, action);
}


/* Constructor of file system.
	 @param   buffer              Buffer of memory.
	 @param   countFile           Max count of file.
	 @param   countDirectory      Max count of directory.
	 @param   countBlock          Max count of blocks.
	 @param   countNode           Max count of nodes.
	 @param   hashLocalNode       Local node hash. From 1 to countNode. */
FileSystem::FileSystem(char* buffer, char* bufferBlock, uint64_t countFile,
	uint64_t countDirectory, uint64_t countBlock,
	uint64_t countNode, NodeHash hashLocalNode)
{
	if ((buffer == NULL) || (bufferBlock == NULL) || (countFile == 0) || (countDirectory == 0) ||
		(countBlock == 0) || (countNode == 0) || (hashLocalNode < 1) ||
		(hashLocalNode > countNode)) {
		fprintf(stderr, "FileSystem::FileSystem: parameter error.\n");
		exit(EXIT_FAILURE);             /* Exit due to parameter error. */
	}
	else {
		this->addressHashTable = (uint64_t)buffer;
		storage = new Storage(buffer, bufferBlock, countFile, countDirectory, countBlock, countNode); /* Initialize storage instance. */
		lock = new LockService((uint64_t)buffer);
	}
}
/* Destructor of file system. */
FileSystem::~FileSystem()
{
	delete storage;                     /* Release storage instance. */
}


void FileSystem::rootInitialize(NodeHash LocalNode)
{
	this->hashLocalNode = LocalNode; /* Assign local node hash. */
	/* Add root directory. */
	UniqueHash hashUnique;
	HashTable::getUniqueHash("/", strlen("/"), &hashUnique);
	NodeHash hashNode = storage->getNodeHash(&hashUnique); /* Get node hash. */
	Debug::debugItem("root node: %d", (int)hashNode);
	if (hashNode == this->hashLocalNode) { /* Root directory is here. */
		Debug::notifyInfo("Initialize root directory.");
		DirectoryMeta metaDirectory;
		metaDirectory.count = 0;    /* Initialize count of files in root directory is 0. */
		uint64_t indexDirectoryMeta;
		if (storage->tableDirectoryMeta->create(&indexDirectoryMeta, &metaDirectory) == false) {
			fprintf(stderr, "FileSystem::FileSystem: create directory meta error.\n");
			exit(EXIT_FAILURE);             /* Exit due to parameter error. */
		}
		else {
			if (storage->hashtable->put("/", indexDirectoryMeta, true) == false) { /* true for directory. */
				fprintf(stderr, "FileSystem::FileSystem: hashtable put error.\n");
				exit(EXIT_FAILURE);             /* Exit due to parameter error. */
			}
			else {
				;
			}
		}
	}
}

bool FileSystem::checkLocal(NodeHash hashNode)
{
	if (hashNode == hashLocalNode) {
		return true;                    /* Succeed. Local node. */
	}
	else {
		return false;                   /* Succeed. Remote node. */
	}
}


uint64_t FileSystem::lockWriteHashItem(NodeHash hashNode, AddressHash hashAddress)
{
	//NodeHash hashNode = storage->getNodeHash(hashUnique); /* Get node hash. */
	//AddressHash hashAddress = hashUnique->value[0] & 0x00000000000FFFFF; /* Get address hash. */
	uint64_t* value = (uint64_t*)(this->addressHashTable + hashAddress * sizeof(HashItem));
	Debug::debugItem("value before write lock: %lx", *value);
	uint64_t ret = lock->WriteLock((uint16_t)(hashNode), (uint64_t)(sizeof(HashItem) * hashAddress));
	Debug::debugItem("value after write lock, address: %lx, key: %lx", value, ret);
	return ret;
}

/* Unlock hash item. */
void FileSystem::unlockWriteHashItem(uint64_t key, NodeHash hashNode, AddressHash hashAddress)
{
    uint64_t *value = (uint64_t *)(this->addressHashTable + hashAddress * sizeof(HashItem));
    Debug::debugItem("value before write unlock: %lx", *value);
    lock->WriteUnlock(key, (uint16_t)(hashNode), (uint64_t)(sizeof(HashItem) * hashAddress));
    Debug::debugItem("value after write unlock: %lx address = %lx", value, *value);
}


/* Lock hash item for read. */
uint64_t FileSystem::lockReadHashItem(NodeHash hashNode, AddressHash hashAddress)
{
    uint64_t *value = (uint64_t *)(this->addressHashTable + hashAddress * sizeof(HashItem));
    Debug::debugItem("value before read lock: %lx", *value);
    uint64_t ret = lock->ReadLock((uint16_t)(hashNode), (uint64_t)(sizeof(HashItem) * hashAddress));
    Debug::debugItem("value after read lock, address: %lx, key: %lx", *value, ret);
    return ret;
}
/* Unlock hash item. */
void FileSystem::unlockReadHashItem(uint64_t key, NodeHash hashNode, AddressHash hashAddress)
{
    uint64_t *value = (uint64_t *)(this->addressHashTable + hashAddress * sizeof(HashItem));
    Debug::debugItem("value before read unlock: %lx", *value);
    lock->ReadUnlock(key, (uint16_t)(hashNode), (uint64_t)(sizeof(HashItem) * hashAddress));
    Debug::debugItem("value after read unlock: %lx", *value);
}



/* Write extent. That is to parse the part to write in file position information. Attention: do not unlock here. Unlock is implemented in updateMeta.
	 @param   path        Path of file.
	 @param   size        Size of data to write.
	 @param   offset      Offset of data to write.
	 @param   fpi         File position information.
	 @param   metaFile    File meta buffer.
	 @param   key         Key buffer to unlock.
	 @return              If operation succeeds then return true, otherwise return false. */
bool FileSystem::extentWrite(const char* path, uint64_t size, uint64_t offset, file_pos_info* fpi, file_extent_info* fei_in, file_extent_info* fei_out, uint64_t DataBaseAddress) /* Write. */
{
	Debug::debugTitle("FileSystem::writedata");
	Debug::debugItem("Stage 1. Entry point. Path: %s.", path);
	Debug::debugItem("write, size = %x, offset = %x", size, offset);
	if ((path == NULL) || (fpi == NULL)) /* Judge if path, file position information buffer and key buffer are valid. */
		return false;                   /* Null parameter error. */
	// UniqueHash hashUnique;
	bool result = true;

	// uint64_t indexFileMeta;
	// bool isDirectory;
	// FileMeta* metaFile = (FileMeta*)malloc(sizeof(FileMeta));
	Debug::debugItem("Stage 2. Get meta.");
	if (((0xFFFFFFFFFFFFFFFF - offset) < size) || (size == 0)) {
		return false; /* Fail due to offset + size will cause overflow or size is zero. */
	}

	Debug::debugItem("Stage 3.");
	uint64_t countExtraBlock; /* Count of extra blocks. At least 1. */
	// int64_t boundCurrentExtraExtent; /* Bound of current extra extent. */
	/* Consider 0 size file. */

	countExtraBlock = (offset + size - 1) / BLOCK_SIZE - (offset) / BLOCK_SIZE + 1;

	uint64_t indexCurrentExtraBlock; /* Index of current extra block. */
	// uint64_t indexLastCreatedBlock;
	Debug::debugItem("Stage 4.");
	bool resultFor = true;
	fpi->len = countExtraBlock;
	fei_out->len = countExtraBlock;
	if (countExtraBlock == 1) {
		Debug::debugItem("only, i = %d", 1);
		if (storage->tableBlock->create(&indexCurrentExtraBlock) == false) {
			resultFor = false; /* Fail due to no enough space. Might cause inconsistency. */
		}
		else {
			fei_out->tuple[0].block_id = indexCurrentExtraBlock;
			// fei_out->tuple[0].count = 1;
			fpi->tuple[0].offset = indexCurrentExtraBlock * BLOCK_SIZE + offset % BLOCK_SIZE; /* Assign offset. */
			fpi->tuple[0].size = size;
			if (size != BLOCK_SIZE && fei_in->tuple[0].block_id != NULLBLCOK_ID) {
				// cpy
				uint64_t cpy_addr = DataBaseAddress + indexCurrentExtraBlock * BLOCK_SIZE;
				uint64_t in_addr = DataBaseAddress + fei_in->tuple[0].block_id * BLOCK_SIZE;
				Debug::debugItem("Stage 5. copy from %lx to %lx", in_addr, cpy_addr);
				memcpy((char*)cpy_addr, (char*)in_addr, BLOCK_SIZE);
			}
		}
	}
	else {
		for (uint64_t i = 0; i < countExtraBlock; i++) { /* No check on count of extra blocks. */
			Debug::debugItem("for loop, i = %d", i);
			if (storage->tableBlock->create(&indexCurrentExtraBlock) == false) {
				resultFor = false; /* Fail due to no enough space. Might cause inconsistency. */
				break;
			}
			else { /* So we need to modify the allocation way in table class. */
				fei_out->tuple[i].block_id = indexCurrentExtraBlock;
				// fei_out->tuple[i].count = 1;
				fpi->tuple[i].offset = indexCurrentExtraBlock * BLOCK_SIZE; /* Assign offset. */
				fpi->tuple[i].size = BLOCK_SIZE; /* Assign size. */

				if (i == 0 && offset % BLOCK_SIZE != 0) {
					fpi->tuple[0].offset = indexCurrentExtraBlock * BLOCK_SIZE + offset % BLOCK_SIZE; /* Assign offset. */
					fpi->tuple[0].size = BLOCK_SIZE - offset % BLOCK_SIZE;
					if (fei_in != nullptr && fei_in->tuple[i].block_id != NULLBLCOK_ID) { /* Judge if new blocks need to be created. */
						// cpy
						uint64_t cpy_addr = DataBaseAddress + indexCurrentExtraBlock * BLOCK_SIZE;
						uint64_t in_addr = DataBaseAddress + fei_in->tuple[i].block_id * BLOCK_SIZE;
						Debug::debugItem("Stage 5.0. copy from %lx to %lx", in_addr, cpy_addr);
						memcpy((char*)cpy_addr, (char*)in_addr, offset % BLOCK_SIZE);
					}
				}
				if (i == countExtraBlock - 1 && (offset + size) % BLOCK_SIZE != 0) {
					fpi->tuple[i].size = (offset + size) % BLOCK_SIZE; /* Assign size. */
					if (fei_in != nullptr && fei_in->tuple[i].block_id != NULLBLCOK_ID) {
						// cpy
						uint64_t cpy_addr = DataBaseAddress + fpi->tuple[0].offset + fpi->tuple[i].size;
						uint64_t in_addr = DataBaseAddress + fei_in->tuple[i].block_id * BLOCK_SIZE + (offset + size) % BLOCK_SIZE;
						Debug::debugItem("Stage 5.1. copy from %lx to %lx", in_addr, cpy_addr);
						memcpy((char*)cpy_addr, (char*)in_addr, BLOCK_SIZE - (offset + size) % BLOCK_SIZE);
					}
				}
			}
		}
	}
	return result & resultFor;
}

bool FileSystem::extentRead(const char *path, uint64_t size, uint64_t offset, file_extent_info *fei,  file_pos_info *fpi) {
	Debug::debugTitle("FileSystem::read");
	uint64_t countExtraBlock = fei->len;
	Debug::debugItem("len = %d %d", fei->len, countExtraBlock);
	for (uint64_t i = 0; i < countExtraBlock; i++) { /* No check on count of extra blocks. */
		fpi->tuple[i].offset = fei->tuple[0].block_id * BLOCK_SIZE; /* Assign offset. */
		fpi->tuple[i].size = BLOCK_SIZE; /* Assign size. */
		Debug::debugItem("extentRead for loop, i = %d %d", i, countExtraBlock);
		if (i == 0 && offset % BLOCK_SIZE != 0) {
			fpi->tuple[0].offset = fei->tuple[0].block_id * BLOCK_SIZE + offset % BLOCK_SIZE; /* Assign offset. */
			fpi->tuple[0].size = BLOCK_SIZE - offset % BLOCK_SIZE;
		}
		if (i == countExtraBlock - 1 && (offset + size) % BLOCK_SIZE != 0) {
			fpi->tuple[i].size = (offset + size) % BLOCK_SIZE; /* Assign size. */
		}
		Debug::debugItem("for loop, i = %d,  addr = %lx, size = %lx", i, fpi->tuple[i].offset, fpi->tuple[i].size);	
	}
	return true;
}


bool FileSystem::extentMetaWrite(const char* path, uint64_t size, uint64_t offset, file_extent_info* fei) /* Write. */
{
	Debug::debugTitle("FileSystem::writeMeta");
	Debug::debugItem("Stage 1. Entry point. Path: %s.", path);
	Debug::debugItem("write, size = %x, offset = %x", size, offset);
	if ((path == NULL) || (fei == NULL)) /* Judge if path, file position information buffer and key buffer are valid. */
		return false;                   /* Null parameter error. */
	else {
		UniqueHash hashUnique;
		HashTable::getUniqueHash(path, strlen(path), &hashUnique); /* Get unique hash. */
		NodeHash hashNode = storage->getNodeHash(&hashUnique); /* Get node hash by unique hash. */
		AddressHash hashAddress = HashTable::getAddressHash(&hashUnique); /* Get address hash by unique hash. */
		bool result;
		uint64_t key = lockWriteHashItem(hashNode, hashAddress);  /* Lock hash item. */
		// uint64_t key_offset = hashAddress;
		{
			uint64_t indexFileMeta;
			bool isDirectory;
			FileMeta* metaFile = (FileMeta*)malloc(sizeof(FileMeta));
			if (storage->hashtable->get(&hashUnique, &indexFileMeta, &isDirectory) == false) { /* If path does not exist. */
				result = false;     /* Fail due to path does not exist. */
			}
			else {
				Debug::debugItem("Stage 2. Get meta.");
				if (isDirectory == true) { /* If directory meta. */
					result = false; /* Fail due to not file. */
				}
				else {
					if (storage->tableFileMeta->get(indexFileMeta, metaFile) == false) {
						result = false; /* Fail due to get file meta error. */
					}
					else {
						if (((0xFFFFFFFFFFFFFFFF - offset) < size) || (size == 0)) {
							Debug::debugItem("Error before Stage 3. %d %d", offset, size );
							result = false; /* Fail due to offset + size will cause overflow or size is zero. */
						}
						else {
							Debug::debugItem("Stage 3.");
							if ((metaFile->size == 0) || ((offset + size - 1) / BLOCK_SIZE > (metaFile->size - 1) / BLOCK_SIZE)) { /* Judge if new blocks need to be created. */
								metaFile->size = offset + size;
								result = true;
							}
							else {
								metaFile->size = (offset + size) > metaFile->size ? (offset + size) : metaFile->size; /* Update size of file in */
								result = true; /* Succeed. */
							}
							
							uint64_t begin_id = offset / BLOCK_SIZE; /* Count of extra blocks. At least 1. */

							for (uint64_t i = metaFile->count - 1; i < begin_id + fei->len - 1; i++) {
								metaFile->tuple[i].countExtentBlock = 0;
							}

							if (metaFile->count < begin_id + fei->len) {
								metaFile->count = begin_id + fei->len;
							}

							for (uint64_t i = 0; i < fei->len; i++) { /* No check on count of extra blocks. */
								Debug::debugItem("for loop, i = %d", i);
								
								metaFile->tuple[i+begin_id].indexExtentStartBlock = fei->tuple[i].block_id;
								metaFile->tuple[i+begin_id].countExtentBlock = 1;
							}
							if (metaFile->timeLastModified < fei->timeLastModified) {
								metaFile->timeLastModified = fei->timeLastModified;
							}
						}
					}
				}
			}
			if (result)
			{
				// metaFile->timeLastModified = time(NULL);
				storage->tableFileMeta->put(indexFileMeta, metaFile);
			}
			free(metaFile);
		}
		unlockWriteHashItem(key, hashNode, hashAddress);  /* Unlock hash item. */
		Debug::debugItem("Stage end.");
		return result;              /* Return specific result. */
	}
	return false;
}


bool FileSystem::mknodcd(const char *path) 
{
    Debug::debugTitle("FileSystem::mknod");
    Debug::debugItem("Stage 1. Entry point. Path: %s.", path);
    if (path == NULL) {
        return false;                   /* Fail due to null path. */
    } else {
        UniqueHash hashUnique;
        HashTable::getUniqueHash(path, strlen(path), &hashUnique); /* Get unique hash. */
        NodeHash hashNode = storage->getNodeHash(&hashUnique); /* Get node hash by unique hash. */
        AddressHash hashAddress = HashTable::getAddressHash(&hashUnique); /* Get address hash by unique hash. */
        // uint64_t DistributedTxID;
        uint64_t LocalTxID;
        if (checkLocal(hashNode) == true) { /* If local node. */
            bool result;
            uint64_t key = lockWriteHashItem(hashNode, hashAddress); /* Lock hash item. */
            {
                // DistributedTxID = TxDistributedBegin();
                LocalTxID = TxLocalBegin();
                Debug::debugItem("Stage 2. Update parent directory metadata.");
                char *parent = (char *)malloc(strlen(path) + 1);
                char *name = (char *)malloc(strlen(path) + 1);
                DirectoryMeta parentMeta;
                uint64_t parentHashAddress, parentMetaAddress;
                uint16_t parentNodeID;
                getParentDirectory(path, parent);
                getNameFromPath(path, name);
                if (readDirectoryMeta(parent, &parentMeta, &parentHashAddress, &parentMetaAddress, &parentNodeID) == false) {
                    // Debug::notifyError("readDirectoryMeta failed.");
                    // TxDistributedPrepare(DistributedTxID, false);
                    result = false;
                } else {
                    uint64_t indexMeta;
                    bool isDirectory;
                    if (storage->hashtable->get(&hashUnique, &indexMeta, &isDirectory) == true) { /* If path exists. */
                        Debug::notifyError("addMetaToDirectory failed.");
                        // TxDistributedPrepare(DistributedTxID, false);
                        result = false; /* Fail due to existence of path. */
                    } else {
                    	
                    	/* Update directory meta first. */
                    	parentMeta.count++; /* Add count of names under directory. */
                        strcpy(parentMeta.tuple[parentMeta.count - 1].names, name); /* Add name. */
                        parentMeta.tuple[parentMeta.count - 1].isDirectories = isDirectory; /* Add directory state. */
                        
                        Debug::debugItem("Stage 3. Create file meta.");
                        uint64_t indexFileMeta;
                        FileMeta metaFile;
                        metaFile.timeLastModified = time(NULL); /* Set last modified time. */
                        metaFile.count = 0; /* Initialize count of extents as 0. */
                        metaFile.size = 0;
                        /* Apply updated data to local log. */
                        TxWriteData(LocalTxID, (uint64_t)&parentMeta, (uint64_t)sizeof(DirectoryMeta));
                        /* Receive remote prepare with (OK) */
                        // TxDistributedPrepare(DistributedTxID, true);
                        /* Start phase 2, commit it. */
                        // updateRemoteMeta(parentNodeID, &parentMeta, parentMetaAddress, parentHashAddress);
                        /* Only allocate momery, write to log first. */
                        if (storage->tableFileMeta->create(&indexFileMeta, &metaFile) == false) {
                            result = false; /* Fail due to create error. */
                        } else {
                            if (storage->hashtable->put(&hashUnique, indexFileMeta, false) == false) { /* false for file. */
                                result = false; /* Fail due to hash table put. No roll back. */
                            } else {
                                result = true;
                            }
                        }
                    }
                }
                free(parent);
            	free(name);
            }
            if (result == false) {
                TxLocalCommit(LocalTxID, false);
                // TxDistributedCommit(DistributedTxID, false);
            } else {
                TxLocalCommit(LocalTxID, true);
                // TxDistributedCommit(DistributedTxID, true);
            }
            unlockWriteHashItem(key, hashNode, hashAddress);  /* Unlock hash item. */
            Debug::debugItem("Stage end.");
            return result;              /* Return specific result. */
        } else {                        /* If remote node. */
            return false;
        }
    }
}

bool FileSystem::mkdircd(const char *path)
{
    Debug::debugTitle("FileSystem::mkdir");
    Debug::debugItem("Stage 1. Entry point. Path: %s.", path);
    if (path == NULL) {
        return false;                   /* Fail due to null path. */
    } else {
        UniqueHash hashUnique;
        HashTable::getUniqueHash(path, strlen(path), &hashUnique); /* Get unique hash. */
        NodeHash hashNode = storage->getNodeHash(&hashUnique); /* Get node hash by unique hash. */
        AddressHash hashAddress = HashTable::getAddressHash(&hashUnique); /* Get address hash by unique hash. */
        // uint64_t DistributedTxID;
        uint64_t LocalTxID;
        if (checkLocal(hashNode) == true) { /* If local node. */
            bool result;
            uint64_t key = lockWriteHashItem(hashNode, hashAddress); /* Lock hash item. */
            {
                // DistributedTxID = TxDistributedBegin();
                LocalTxID = TxLocalBegin();
                Debug::debugItem("Stage 2. Check parent.");
                char *parent = (char *)malloc(strlen(path) + 1);
                char *name = (char *)malloc(strlen(path) + 1);
                DirectoryMeta parentMeta;
                uint64_t parentHashAddress, parentMetaAddress;
                uint16_t parentNodeID;
                getParentDirectory(path, parent);
                getNameFromPath(path, name);
                if (readDirectoryMeta(parent, &parentMeta, &parentHashAddress, &parentMetaAddress, &parentNodeID) == false) {
                    // TxDistributedPrepare(DistributedTxID, false);
                    result = false;
                } else {
                    uint64_t indexMeta;
                    bool isDirectory;
                    if (storage->hashtable->get(&hashUnique, &indexMeta, &isDirectory) == true) { /* If path exists. */
                        // TxDistributedPrepare(DistributedTxID, false);
                        result = false; /* Fail due to existence of path. */
                    } else {

                    	/* Update directory meta first. */
                    	parentMeta.count++; /* Add count of names under directory. */
                        strcpy(parentMeta.tuple[parentMeta.count - 1].names, name); /* Add name. */
                        parentMeta.tuple[parentMeta.count - 1].isDirectories = true; /* Add directory state. */
                        
                        Debug::debugItem("Stage 3. Write directory meta.");
                        uint64_t indexDirectoryMeta;
                        DirectoryMeta metaDirectory;
                        metaDirectory.count = 0; /* Initialize count of names as 0. */
                        /* Apply updated data to local log. */
                        TxWriteData(LocalTxID, (uint64_t)&parentMeta, (uint64_t)sizeof(DirectoryMeta));
                        /* Receive remote prepare with (OK) */
                        // TxDistributedPrepare(DistributedTxID, true);
                        /* Start phase 2, commit it. */
                        updateRemoteMeta(parentNodeID, &parentMeta, parentMetaAddress, parentHashAddress);
                        
                        if (storage->tableDirectoryMeta->create(&indexDirectoryMeta, &metaDirectory) == false) {
                            result = false; /* Fail due to create error. */
                        } else {
                            Debug::debugItem("indexDirectoryMeta = %d", indexDirectoryMeta);
                            if (storage->hashtable->put(&hashUnique, indexDirectoryMeta, true) == false) { /* true for directory. */
                                result = false; /* Fail due to hash table put. No roll back. */
                            } else {
                                result = true;
                            }
                        }
                    }
                }
                free(parent);
            	free(name);
            }

            if (result == false) {
                TxLocalCommit(LocalTxID, false);
                // TxDistributedCommit(DistributedTxID, false);
            } else {
                TxLocalCommit(LocalTxID, true);
                // TxDistributedCommit(DistributedTxID, true);
            }

            unlockWriteHashItem(key, hashNode, hashAddress);  /* Unlock hash item. */
            Debug::debugItem("Stage end.");
            return result;              /* Return specific result. */
        } else {                        /* If remote node. */
            return false;
        }
    }
}

bool FileSystem::getParentDirectory(const char *path, char *parent) { /* Assume path is valid. */
    if ((path == NULL) || (parent == NULL)) { /* Actually there is no need to check. */
        return false;
    } else {
        strcpy(parent, path);           /* Copy path to parent buffer. Though it is better to use strncpy later but current method is simpler. */
        uint64_t lengthPath = strlen(path); /* FIXME: Might cause memory leak. */
        if ((lengthPath == 1) && (path[0] == '/')) { /* Actually there is no need to check '/' if path is assumed valid. */
            return false;               /* Fail due to root directory has no parent. */
        } else {
            bool resultCut = false;
            for (int i = lengthPath - 1; i >= 0; i--) { /* Limit in range of signed integer. */
                if (path[i] == '/') {
                    parent[i] = '\0';   /* Cut string. */
                    resultCut = true;
                    break;
                } 
            }
            if (resultCut == false) {
                return false;           /* There is no '/' in string. It is an extra check. */
            } else {
                if (parent[0] == '\0') { /* If format is '/path' which contains only one '/' then parent is '/'. */
                    parent[0] = '/';
                    parent[1] = '\0';
                    return true;        /* Succeed. Parent is root directory. */ 
                } else {
                    return true;        /* Succeed. */
                }
            }
        }
    }
}

bool FileSystem::getNameFromPath(const char *path, char *name) { /* Assume path is valid. */
    if ((path == NULL) || (name == NULL)) { /* Actually there is no need to check. */
        return false;
    } else {
        uint64_t lengthPath = strlen(path); /* FIXME: Might cause memory leak. */
        if ((lengthPath == 1) && (path[0] == '/')) { /* Actually there is no need to check '/' if path is assumed valid. */
            return false;               /* Fail due to root directory has no parent. */
        } else {
            bool resultCut = false;
            int i;
            for (i = lengthPath - 1; i >= 0; i--) { /* Limit in range of signed integer. */
                if (path[i] == '/') {
                    resultCut = true;
                    break;
                } 
            }
            if (resultCut == false) {
                return false;           /* There is no '/' in string. It is an extra check. */
            } else {
                strcpy(name, &path[i + 1]); /* Copy name to name buffer. path[i] == '/'. Though it is better to use strncpy later but current method is simpler. */
                return true;
            }
        }
    }
}

void FileSystem::updateRemoteMeta(uint16_t parentNodeID, DirectoryMeta *meta, uint64_t parentMetaAddress, uint64_t parentHashAddress) {
	Debug::debugTitle("updateRemoteMeta");
	/* Prepare imm data. */
	uint32_t imm, temp;
	/*
	|  12b  |     20b    |
	+-------+------------+
	| 0XFFF | HashAdress |
	+-------+------------+
	*/
	temp = 0XFFF;
	imm = (temp << 20);
	imm += (uint32_t)parentHashAddress;
	/* Remote write with imm. */
	uint64_t SendBuffer;
	fs_server->getMemoryManagerInstance()->getServerSendAddress(parentNodeID, &SendBuffer);
	uint64_t RemoteBuffer = parentMetaAddress;
	Debug::debugItem("imm = %x, SendBuffer = %lx, RemoteBuffer = %lx", imm, SendBuffer, RemoteBuffer);
	memcpy((void *)(RemoteBuffer + fs_server->getMemoryManagerInstance()->getDmfsBaseAddress()), (void *)meta, sizeof(DirectoryMeta));
	//unlockWriteHashItem(0, parentNodeID, parentHashAddress);
	return;
    
}

bool FileSystem::readDirectoryMeta(const char *path, DirectoryMeta *meta, uint64_t *hashAddress, uint64_t *metaAddress, uint16_t *parentNodeID) {
    Debug::debugTitle("FileSystem::readDirectoryMeta");
    Debug::debugItem("Stage 1. Entry point. Path: %s.", path);
    if (path == NULL) /* Judge if path and list buffer are valid. */
        return false;                   /* Null parameter error. */
    else {
    	bool result;
        UniqueHash hashUnique;
        HashTable::getUniqueHash(path, strlen(path), &hashUnique); /* Get unique hash. */
        NodeHash hashNode = storage->getNodeHash(&hashUnique); /* Get node hash by unique hash. */
        *hashAddress = HashTable::getAddressHash(&hashUnique); /* Get address hash by unique hash. */
        *parentNodeID = (uint16_t)hashNode;
        if (checkLocal(hashNode) == true) { /* If local node. */
            uint64_t key = lockReadHashItem(hashNode, *hashAddress); /* Lock hash item. */
            {
                uint64_t indexDirectoryMeta;
                bool isDirectory;
                if (storage->hashtable->get(&hashUnique, &indexDirectoryMeta, &isDirectory) == false) { /* If path does not exist. */
                	Debug::debugItem("path does not exist");
                    result = false;     /* Fail due to path does not exist. */
                } else {
                    Debug::debugItem("Stage 2. Get meta.");
                    if (isDirectory == false) { /* If file meta. */
                    	Debug::notifyError("Not a directory");
                        result = false; /* Fail due to not directory. */
                    } else {
                        if (storage->tableDirectoryMeta->get(indexDirectoryMeta, meta, metaAddress) == false) {
                        	Debug::notifyError("Fail due to get directory meta error.");
                            result = false; /* Fail due to get directory meta error. */
                        } else {
                        	Debug::debugItem("metaAddress = %lx, getDmfsBaseAddress = %lx", *metaAddress, fs_server->getMemoryManagerInstance()->getDmfsBaseAddress());
                        	*metaAddress = *metaAddress - fs_server->getMemoryManagerInstance()->getDmfsBaseAddress();
                            result = true; /* Succeed. */
                        }
                    }
                }
            }
            unlockReadHashItem(key, hashNode, *hashAddress); /* Unlock hash item. */
            Debug::debugItem("Stage end.");
            return result;              /* Return specific result. */
        } 
    }
	return false;
}

bool FileSystem::access(const char *path)
{
    Debug::debugTitle("FileSystem::access");
    Debug::debugItem("Stage 1. Entry point. Path: %s.", path);
    if (path == NULL)                   /* Judge if path is valid. */
        return false;                   /* Null path error. */
    else {
        Debug::debugItem("Stage 2. Check access.");
        UniqueHash hashUnique;
        HashTable::getUniqueHash(path, strlen(path), &hashUnique); /* Get unique hash. */
        NodeHash hashNode = storage->getNodeHash(&hashUnique); /* Get node hash by unique hash. */
        AddressHash hashAddress = HashTable::getAddressHash(&hashUnique); /* Get address hash by unique hash. */
        Debug::debugItem("Stage 3.");
        if (checkLocal(hashNode) == true) { /* If local node. */
            bool result;
            Debug::debugItem("Stage 4.");
            uint64_t key = lockReadHashItem(hashNode, hashAddress); /* Lock hash item. */
            {
                uint64_t indexMeta;
                bool isDirectory;
                if (storage->hashtable->get(&hashUnique, &indexMeta, &isDirectory) == false) { /* If path does not exist. */
                    result = false;     /* Fail due to path does not exist. */
                } else {
                    result = true;      /* Succeed. Do not need to check parent. */
                }
            }
            unlockReadHashItem(key, hashNode, hashAddress); /* Unlock hash item. */
            Debug::debugItem("Stage end.");
            return result;              /* Return specific result. */
        } else {                        /* If remote node. */
            return false;
        }
    }
}


bool FileSystem::getattr(const char *path, FileMeta *attribute)
{
    Debug::debugTitle("FileSystem::getattr");
    Debug::debugItem("Stage 1. Entry point. Path: %s.", path);
    if ((path == NULL) || (attribute == NULL)) /* Judge if path and attribute buffer are valid. */
        return false;                   /* Null parameter error. */
    else {
        UniqueHash hashUnique;
        HashTable::getUniqueHash(path, strlen(path), &hashUnique); /* Get unique hash. */
        NodeHash hashNode = storage->getNodeHash(&hashUnique); /* Get node hash by unique hash. */
        AddressHash hashAddress = HashTable::getAddressHash(&hashUnique); /* Get address hash by unique hash. */
        if (checkLocal(hashNode) == true) { /* If local node. */
            // return true;
            bool result;
            uint64_t key = lockReadHashItem(hashNode, hashAddress); /* Lock hash item. */
            {
                uint64_t indexMeta;
                bool isDirectory;
                Debug::debugItem("Stage 1.1.");
                if (storage->hashtable->get(&hashUnique, &indexMeta, &isDirectory) == false) { /* If path does not exist. */
                    Debug::debugItem("Stage 1.2.");
                    result = false;     /* Fail due to path does not exist. */
                } else {
                    Debug::debugItem("Stage 2. Get meta.");
                    if (isDirectory == false) { /* If file meta. */
                        if (storage->tableFileMeta->get(indexMeta, attribute) == false) {
                            result = false; /* Fail due to get file meta error. */
                        } else {
                            Debug::debugItem("FileSystem::getattr, meta.size = %d", attribute->size);
                            result = true; /* Succeed. */
                        }
                    } else {
                    	attribute->count = MAX_FILE_EXTENT_COUNT; /* Inter meaning, representing directoiries */
						// attribute->size = (-1);
                        result = true;
                    }
                }
            }
            unlockReadHashItem(key, hashNode, hashAddress); /* Unlock hash item. */
            Debug::debugItem("Stage end.");
            return result;              /* Return specific result. */
        } else {                        /* If remote node. */
            return false;
        }
    }
}