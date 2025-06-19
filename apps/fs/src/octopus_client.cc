#include "fscommon.h"
#include "fsrpc.h"
#include "pkt.h"
#include "qp_info.h"
// #include "fs.h"
#include <cstring>

/**
 *nrfsWrite - Write data into an open file.
 * @param fs The configured filesystem handle.
 * @param file The file handle.
 * @param buffer The data.
 * @param size The no. of bytes to write.
 * @param offset The offset of the file where to write.
 * @return Returns the number of bytes written, -1 on error.
 **/
 // #define PERF
Timestamp32 ts_generator;
void correct(const char* old_path, char* new_path) {
  int i, count = 0;
  int len = strlen(old_path);
  for (i = 0; i < len; i++) {
    if ((old_path[i] == '/') && (old_path[i + 1] == '/'))
      continue;
    new_path[count++] = old_path[i];
  }
  if (new_path[count - 1] == '/') {
    new_path[count - 1] = '\0';
  }
  else {
    new_path[count] = '\0';
  }
  if (new_path[0] == '\0') {
    new_path[0] = '/';
    new_path[1] = '\0';
  }
}

// func1:
// 1.write data 2.data rpc 3.metadata rpc

// func2:
// 1.data rpc 2.metadata rpc

// func3:
// 1.rpc 2.rdma read 3.metadata rpc

// vt: file -> offset, size, addr



void FsRpc::fill_raw_header(RawVMsg *raw, FsCtx *app_ctx) {
  raw->flag = FLAGS_visibility ? visibility : 0;

  raw->rpc_header = {
    .dst_s_id = (uint8_t)-1,
    .src_s_id = server_id,
    .src_t_id = thread_id,
    .src_c_id = app_ctx->coro_id,
    .local_id = 0,
    .op = (uint8_t)-1,
  };

  raw->switch_key = get_switch_key(app_ctx->hashUnique.value[3]);
  raw->kv_g_id = client->get_global_id();
  raw->kv_c_id = app_ctx->coro_id;
  raw->magic = app_ctx->magic;
  // servermap
  // qp map
  // coro_id
  raw->key = app_ctx->key;
  // log id
  
  // route port
  raw->route_map = my_index_bitmap;
  raw->client_port =
    rdma::toBigEndian16(client->get_thread_port(thread_id, 0));
  raw->send_cnt = 0;
  raw->index_port = rdma::toBigEndian16(client->get_thread_port(app_ctx->index_t_id, 0));
  // magic_in_switch
  raw->timestamp = 0;


  return;
}


int FsRpc::nrfsRead(CoroMaster& sink, rdma::CoroCtx& c_ctx, int cnt) {

#ifdef LOGDEBUG
  LOG(INFO) << "nrfsRead begin";
#endif
  auto& app_ctx = *(FsCtx*)c_ctx.app_ctx;

  reporter->begin(thread_id, app_ctx.coro_id, 0);

  nrfsFile _file = app_ctx._file;

  // const void* buffer = app_ctx.buffer;
  uint64_t size = app_ctx.size;
  uint64_t offset = app_ctx.offset;

  uint8_t qp_id = 0;

  // file_pos_info fpi;
  uint64_t length_copied = 0;

  // TODO: read metadata


  rdma::QPInfo* qp_meta = rpc_get_qp(rdma::LOGRAW, app_ctx.index_s_id, app_ctx.index_t_id, qp_id);

  auto msg0 = (MetaReadReq*)qp_meta->get_send_msg_addr();

  fill_raw_header(&msg0->raw, &app_ctx);
  msg0->raw.rpc_header.dst_s_id = app_ctx.index_s_id;
  msg0->raw.rpc_header.op = kEnumReadIndexReq;
  msg0->raw.flag = FLAGS_visibility ? visibility : 0;
  msg0->raw.log_id = UINT64_MAX;

  msg0->meta_op = kEnumFsMetaRead;
  correct((char*)_file, msg0->path);

#ifdef LOGDEBUG
  LOG(INFO) << "send msg waiting for the mn reply";
#endif

  qp_meta->modify_smsg_size(sizeof(MetaReadReq));
  qp_meta->append_signal_smsg();
  qp_meta->post_appended_smsg(&sink);
  
  FileMeta file_meta;
  file_extent_info fei;
  
  RawVMsg* reply_header = (RawVMsg*)c_ctx.msg_ptr;

  if (reply_header->rpc_header.op == kEnumReadIndexReq) {
    fei = ((MetaReadReq *)c_ctx.msg_ptr)->fei;
    app_ctx.index_in_switch = true;	
    sink();
  }

  

  MetaReadReply* reply = (MetaReadReply*)c_ctx.msg_ptr;
  file_meta = reply->attribute; 

  if (reply->attribute.count == MAX_FILE_EXTENT_COUNT) {
    return 0;
  }

  // memcpy((char *)file_meta, );
  uint64_t cur_offset = offset;

  for (int i = 0; i < cnt && cur_offset < file_meta.size; i++, cur_offset = i * size + offset) {
    

    rdma::QPInfo* qp = rpc_get_qp(rdma::LOGRAW, app_ctx.data_s_id, app_ctx.data_t_id, qp_id);

    auto msg1 = (DataReadReq*)qp->get_send_msg_addr();
    
    // write 4KB per request
    uint64_t cur_size = size;
    if (cur_size + cur_offset > file_meta.size) {
      cur_size = file_meta.size - cur_offset;
    }

    uint b_id = cur_offset / BLOCK_SIZE;
    uint e_id = (cur_offset +  cur_size + BLOCK_SIZE - 1) / BLOCK_SIZE; 
    msg1->fei.len = 0;

    for (uint j = 0; j < e_id - b_id + 1; j++) {
      if (file_meta.count > j + b_id && file_meta.tuple[j + b_id].countExtentBlock != 0) {
        msg1->fei.tuple[j].block_id = file_meta.tuple[j + b_id].indexExtentStartBlock;
        msg1->fei.len++;
      }
    }

    msg1->size =  cur_size ;
    msg1->offset = offset;
    correct((char*)_file, msg1->path);

    fill_raw_header(&msg1->raw, &app_ctx);
    msg1->raw.rpc_header.dst_s_id = app_ctx.data_s_id;
    msg1->raw.rpc_header.op = kEnumReadLog;

#ifdef LOGDEBUG
    printf("read cur_offset=%d, size=%d\n", cur_offset, cur_size);
    LOG(INFO) << "send read msg waiting for the dn reply";
#endif

    qp->modify_smsg_size(sizeof(DataReadReq));
    qp->append_signal_smsg();
    qp->post_appended_smsg(&sink);

#ifdef LOGDEBUG
    LOG(INFO) << "get reply1";
#endif


#ifdef LOGDEBUG
    auto reply1 = (ExtentWriteSendBuffer*)c_ctx.msg_ptr;
    LOG(INFO) << "get reply1" << reply1->raw.rpc_header.op;
#endif
    length_copied += cur_size;
    cur_offset += cur_size;
  }
  uint64_t res = reporter->end(thread_id, app_ctx.coro_id, 0);
  reporter->end_copy(thread_id, app_ctx.coro_id, 1, res);
  return length_copied;
}


int FsRpc::nrfsWrite(CoroMaster& sink, rdma::CoroCtx& c_ctx, int cnt) {
#ifdef LOGDEBUG
  LOG(INFO) << "nrfsWrite begin";
#endif
  auto& app_ctx = *(FsCtx*)c_ctx.app_ctx;

  reporter->begin(thread_id, app_ctx.coro_id, 0);

  nrfsFile _file = app_ctx._file;
  // const void* buffer = app_ctx.buffer;
  uint64_t size = app_ctx.size;
  uint64_t offset = app_ctx.offset;

  uint8_t qp_id = 0;

  // file_pos_info fpi;
  uint64_t length_copied = 0;

  FileMeta file_meta;
  file_extent_info fei;

  // TODO: read metadata
  if (size % BLOCK_SIZE != 0 || offset % BLOCK_SIZE != 0) {
    rdma::QPInfo* qp_meta = rpc_get_qp(rdma::LOGRAW, app_ctx.index_s_id, app_ctx.index_t_id, qp_id);

    auto msg0 = (MetaReadReq*)qp_meta->get_send_msg_addr();

    fill_raw_header(&msg0->raw, &app_ctx);
    msg0->raw.rpc_header.dst_s_id = app_ctx.index_s_id;
    msg0->raw.rpc_header.op = kEnumReadIndexReq;

    msg0->meta_op = kEnumFsMetaRead;
    correct((char*)_file, msg0->path);
    msg0->raw.flag = FLAGS_visibility ? visibility : 0;

  #ifdef LOGDEBUG
    LOG(INFO) << "send msg waiting for the mn reply";
  #endif

    qp_meta->modify_smsg_size(sizeof(MetaReadReq));
    qp_meta->append_signal_smsg();
    qp_meta->post_appended_smsg(&sink);
    
    
    
    RawVMsg* reply_header = (RawVMsg*)c_ctx.msg_ptr;

    if (reply_header->rpc_header.op == kEnumReadIndexReq) {
      fei = ((MetaReadReq *)c_ctx.msg_ptr)->fei;
      app_ctx.index_in_switch = true;	
      sink();
    }

    auto reply = (MetaReadReply*)c_ctx.msg_ptr;
    file_meta = reply->attribute; 
  } 
  else {
    file_meta.count = 0;
  }

  if (file_meta.count == MAX_FILE_EXTENT_COUNT) {
    return 0;
  }

  // memcpy((char *)file_meta, );

  for (int i = 0; i < cnt; i++) {

    rdma::QPInfo* qp = rpc_get_qp(rdma::LOGRC, app_ctx.data_s_id, app_ctx.data_t_id, qp_id);

    auto msg1 = (DataBuffer*)qp->get_send_msg_addr();
    
    // write 4KB per request

    uint64_t cur_offset = i * size + offset;
    int b_id = cur_offset / BLOCK_SIZE;
    int e_id = (cur_offset + size + BLOCK_SIZE - 1) / BLOCK_SIZE; 

    if (e_id > MAX_FILE_EXTENT_COUNT) {
      break;
    }
    
    msg1->fei.len = b_id - e_id + 1;

    for (uint j = 0; j < msg1->fei.len; j++) {
      if (file_meta.count > j + b_id && file_meta.tuple[j + b_id].countExtentBlock != 0)
        msg1->fei.tuple[j].block_id = file_meta.tuple[j + b_id].indexExtentStartBlock;
      else
        msg1->fei.tuple[j].block_id = UINT32_MAX;
    }

    msg1->size = size;
    msg1->offset = offset;
    correct((char*)_file, msg1->path);

    fill_raw_header(&msg1->raw, &app_ctx);
    msg1->raw.rpc_header.dst_s_id = app_ctx.data_s_id;
    msg1->raw.rpc_header.op = kEnumAppendReq;
    msg1->raw.flag = FLAGS_visibility ? visibility : 0;
    

#ifdef LOGDEBUG
    LOG(INFO) << "send msg waiting for the dn reply";
#endif

    qp->modify_smsg_size(msg1->get_size());
    qp->append_signal_smsg();
    qp->post_appended_smsg(&sink);

#ifdef LOGDEBUG
    LOG(INFO) << "get reply1";
#endif

    auto reply1 = (MetaUpdateReq*)c_ctx.msg_ptr;

    if (reply1->raw.rpc_header.op == kEnumMultiCastPkt) {
      length_copied += size;
      continue;
    }

    qp = rpc_get_qp(rdma::LOGRAW, app_ctx.index_s_id, app_ctx.index_t_id, qp_id);
    auto msg2 = (MetaUpdateReq*)qp->get_send_msg_addr();
    memcpy((void*)msg2, (void*)reply1, sizeof(MetaUpdateReq));
    msg2->raw.rpc_header = {
        .dst_s_id = app_ctx.index_s_id,
        .src_s_id = server_id,
        .src_t_id = thread_id,
        .src_c_id = app_ctx.coro_id,
        .local_id = 0,
        .op = kEnumUpdateIndexReq,
    };
    msg2->raw.send_cnt = 0;


    // memcpy(msg->get_key_addr(), &app_ctx.long_key, sizeof(KvKeyType));

#ifdef LOGDEBUG
    LOG(INFO) << "send req2";
#endif

    qp->modify_smsg_size(sizeof(MetaUpdateReq));
    qp->append_signal_smsg();
    qp->post_appended_smsg(&sink);


    // if (reply2->raw.rpc)

#ifdef LOGDEBUG
    auto reply2 = (GeneralReceiveBuffer*)c_ctx.msg_ptr;
    LOG(INFO) << "get reply2" << reply2->raw.rpc_header.op;
#endif

    
    length_copied += size;
  }

  uint64_t res = reporter->end(thread_id, app_ctx.coro_id, 0);

  reporter->end_copy(thread_id, app_ctx.coro_id, 2, res);

  return length_copied;
}

