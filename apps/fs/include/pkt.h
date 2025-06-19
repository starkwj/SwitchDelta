#pragma once
#include "rpc.h"
#include "fsconfig.h"
#include "fscommon.h"
#include <cstdint>

enum RpcType {
  kEnumReadReq = 0,
  kEnumWriteReq,
  kEnumReadReply,
  kEnumWriteReply,

  kEnumAppendReq,
  kEnumAppendReply, // 5
  kEnumReadLog,

  ReplySwitch = 10,
  kEnumUpdateIndexReq,
  kEnumUpdateIndexReply,

  kEnumReadIndexReply, // 14
  kEnumReadIndexReq,   // 13

  kEnumMultiCastPkt = 21,
  kEnumReadSend,

  kEnumWriteAndRead,
  kEnumWriteReplyWithData,

  kEnumFsMetaRead,
  kEnumFsCreate,
  kEnumMkdir,
};


using KvKeyType = uint64_t;
using VersionType = uint64_t;

struct RawVMsg {

  rpc::RpcHeader rpc_header;
  uint8_t flag;
  uint16_t switch_key;
  uint8_t kv_g_id; // [s:4][t:4]
  uint8_t kv_c_id; // [c:8]
  uint16_t magic;
  uint16_t server_map;
  uint16_t qp_map;  // 2 * 8  bitmap
  uint64_t coro_id; // 8 * id
  // key value payload
  uint32_t key;
  uint64_t log_id;
  uint16_t route_port;
  uint16_t route_map;
  uint16_t client_port;
  uint16_t send_cnt = 0;
  uint16_t index_port;
  uint16_t magic_in_switch;
  uint32_t timestamp;
  // uint16_t sourceNodeID;
  /*for each thread: [xxxx] [xxxx] : [write_coro_id: 0~14] [read_coro_id:
   * 0~14]*/
  uint64_t generate_coro_id(uint8_t c_id_, RpcType op, uint8_t thread_id) {
    // return ((uint64_t)(c_id_ + 1 ) | (op == kEnumReadReq ? 0ull : 0x80ull))
    // << (thread_id*8ull);
    return ((uint64_t)(c_id_ + 1))
           << (thread_id * 8ull + (op == kEnumReadReq ? 0 : 4));
  }
  static uint32_t get_size(int op = -1) {
    switch (op) {
    case kEnumReadLog:
      return sizeof(RawVMsg);
    case kEnumReadReply:
      return sizeof(RawVMsg) + sizeof(KvKeyType) + FLAGS_kv_size_b;
    case kEnumAppendReq:
      return sizeof(RawVMsg) + sizeof(KvKeyType) + FLAGS_kv_size_b;
    case kEnumAppendReply:
      return sizeof(RawVMsg) + sizeof(KvKeyType);

    case kEnumReadIndexReq:
      return sizeof(RawVMsg) + sizeof(KvKeyType);
    case kEnumReadIndexReply:
      return sizeof(RawVMsg);

    case kEnumMultiCastPkt:
      return sizeof(RawVMsg) + sizeof(KvKeyType);
    case ReplySwitch:
      return sizeof(RawVMsg);

    case kEnumUpdateIndexReq:
      return sizeof(RawVMsg) + sizeof(KvKeyType);
    case kEnumUpdateIndexReply:
      return sizeof(RawVMsg);

    case -1:
      return sizeof(RawVMsg) + sizeof(KvKeyType) + FLAGS_kv_size_b; // max;
    default:
      return sizeof(RawVMsg); // default size
      break;
    }
  }

  void *get_value_addr() {
    return (char *)this + sizeof(KvKeyType) + sizeof(RawVMsg);
  }

  void *get_key_addr() { return (char *)this + sizeof(RawVMsg); }

  void *get_log_addr() { return (char *)this + sizeof(RawVMsg); }
  void print() {
    // printf("op=%d c_id=%d key=%x\n", rpc_header.op, rpc_header.src_c_id,
    // key);
  }
} __attribute__((packed));

typedef struct {     /* extentWrite send buffer structure. */
    RawVMsg raw;                    /* Message type. */
    char path[MAX_PATH_LENGTH];         /* Path. */
    uint64_t size;                      /* Size to read. */
    uint64_t offset;                    /* Offset to read. */
    uint32_t get_size() {
      return sizeof(RawVMsg) + sizeof(path) + 16;
    }
}__attribute__((packed)) ExtentWriteSendBuffer;



typedef struct{     /* updateMeta send buffer structure. */
    RawVMsg raw;                    /* Message type. */
    uint64_t offset;
    uint64_t key;                       /* Key to unlock. */
    uint32_t get_size() {
      return sizeof(RawVMsg) + 16;
    }
}__attribute__((packed)) UpdateMetaSendBuffer;


struct DataBuffer { // LJR: write dn
  RawVMsg raw;
  file_extent_info fei;
  char path[MAX_PATH_LENGTH];         /* Path. */
  uint64_t size;                      /* Size to read. */
  uint64_t offset;
  char* data_buffer() {return (char *)this + sizeof(DataBuffer);}
  uint32_t get_size() {return sizeof(DataBuffer) + size;}
  static uint32_t get_max_size() {return sizeof(DataBuffer) + kMaxDataSize;}
}__attribute__((packed));

// typedef struct{     /* extentWrite receive buffer structure. */
//     RawVMsg raw;                  /* Message type. */
//     char path[MAX_PATH_LENGTH];         /* Path. */
//     file_extent_info fei;                  /* File position information. */
//     bool result;                        /* Result. */
// }__attribute__((packed)) ExtentWriteReceiveBuffer; // LJR: write dn reply + update mn

typedef struct{     /* extentWrite receive buffer structure. */
    RawVMsg raw;                  /* Message type. */
    file_extent_info fei;
    char path[MAX_PATH_LENGTH];         /* Path. */  /* Result. */
    uint64_t size;                      /* Size to read. */
    uint64_t offset;
}__attribute__((packed)) MetaUpdateReq; // LJR: write dn reply + update mn

typedef struct{
    RawVMsg raw;
    file_extent_info fei;
    uint32_t meta_op;
    char path[MAX_PATH_LENGTH];
}__attribute__((packed)) MetaReadReq; // LJR: read mn req

typedef struct{
    RawVMsg raw;
    // char path[MAX_PATH_LENGTH];
    bool result;
    FileMeta attribute;
}__attribute__((packed)) MetaReadReply; // LJR: read mn reply

typedef struct{
    RawVMsg raw;
    // char path[MAX_PATH_LENGTH];
    file_extent_info fei; 
    char path[MAX_PATH_LENGTH];         /* Path. */
    uint64_t size;                      /* Size to read. */
    uint64_t offset;
}__attribute__((packed)) DataReadReq; // LJR: read data req

struct DataReadReply { // LJR: write dn
  RawVMsg raw;
  uint64_t size;                      /* Size to read. */
  char* data_buffer() {return (char *)this + sizeof(DataBuffer);}
  uint32_t get_size() {return sizeof(DataBuffer) + size;}
  static uint32_t get_max_size() {return sizeof(DataBuffer) + kMaxDataSize;}
}__attribute__((packed)); // LJR: read data reply



typedef struct {     /* General receive buffer structure. */
public:
	RawVMsg raw;                   /* Message type. */
  bool result;                        /* Result. */
}__attribute__((packed)) GeneralReceiveBuffer; // LJR: update mn ok


