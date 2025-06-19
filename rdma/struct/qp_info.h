#pragma once
#include "../operation/operation.h"
#include "memory_region.h"
#include "size_def.h"
#include <boost/coroutine/all.hpp>
#include <cstddef>
#include <cstdint>
#include <infiniband/verbs.h>

using CoroSlave = typename boost::coroutines::coroutine<void>::pull_type;
using CoroMaster = typename boost::coroutines::coroutine<void>::push_type;

#define DST_MAC                                                                \
  (uint8_t)0x01, (uint8_t)0x02, (uint8_t)0x9a, (uint8_t)0xae, (uint8_t)0x14,   \
      (uint8_t)0x00 // 113
#define SRC_MAC                                                                \
  (uint8_t)0x00, (uint8_t)0x00, (uint8_t)0x00, (uint8_t)0x00, (uint8_t)0x00,   \
      (uint8_t)0x00
#define ETH_TYPE (uint8_t)0x08, (uint8_t)0x00 // NICE

#define IP_HDRS(_message_size)                                                 \
  (uint8_t)0x45, (uint8_t)0x00, (uint8_t)0x00, (uint8_t)(32 + _message_size),  \
      (uint8_t)0x00, (uint8_t)0x00, (uint8_t)0x40, (uint8_t)0x00,              \
      (uint8_t)0x40, (uint8_t)0x11, (uint8_t)0xaf,                             \
      (uint8_t)0xb6 // NICE (uint8_t)0x11= udp afb6 checksum
#define UDP_OTHER(_message_size)                                               \
  (uint8_t)0x00, (uint8_t)(8 + _message_size), (uint8_t)0x00,                  \
      (uint8_t)0x00 // NICE

#define SRC_IP (uint8_t)0x0d, (uint8_t)0x07, (uint8_t)0xff, (uint8_t)0x66
#define DST_IP (uint8_t)0x0d, (uint8_t)0x07, (uint8_t)0xff, (uint8_t)0x7f
#define SRC_PORT (uint8_t)0x02, (uint8_t)0x9A
#define DEST_PORT (uint8_t)0x00, (uint8_t)0x00



namespace rdma {



enum ConnectType {
  RC = 1u << 0,
  RAW = 1u << 1,
  UD = 1u << 2,
  T_LOCAL_SRQ = 1u << 3,
  G_SRQ = 1u << 4,
  DCT = 1u << 5,
  ALL = (1u << 6) - 1,
};

enum ConnectTypeLog {
  LOGRC = 0,
  LOGRAW = 1,
  LOGUD = 2,
  LOGTSRQ = 3,
  LOGGSRQ = 4,
  LOGDCT = 5,
};

struct CoroCtx {
  bool free{true};
  void *msg_ptr;
  int async_op_cnt = -1;
  void add_op_cnt() {
    if (async_op_cnt == -1)
      async_op_cnt = 1;
    else
      async_op_cnt++;
  }
  void sub_op_cnt() {
    if (async_op_cnt > 0)
      --async_op_cnt;
  }

  bool over() {
    return (async_op_cnt == 0);
  }

  // TODO: void* ctx;
  void *app_ctx;
  void init() {
    async_op_cnt = -1;
  }
};

struct RoCEHeader {
  uint8_t dst_mac[6] = {DST_MAC};
  uint8_t src_mac[6] = {SRC_MAC};
  uint8_t eth_type[2] = {ETH_TYPE};
  uint8_t ip_header[12] = {IP_HDRS(0)};
  uint8_t src_ip[4] = {SRC_IP};
  uint8_t dst_ip[4] = {SRC_IP};
  uint8_t src_port[2];
  uint8_t dst_port[2];
  uint8_t udp_header[4] = {UDP_OTHER(0)};

  void set_size(uint16_t size) {
    // FIXME:
    ip_header[2] = (32 + size) >> 8;
    ip_header[3] = (32 + size) & (0xFF);
    udp_header[0] = (8 + size) >> 8;
    udp_header[1] = (8 + size) & (0xFF);
  }

  void set_src_port(uint16_t port) {
    src_port[0] = port >> 8;
    src_port[1] = port & (0xFF);
  }

  void set_dst_port(uint16_t port) {
    // if (FLAGS_port_func)
    //     port = (1u << port);
    // printf("port = %x\n", port);
    dst_port[0] = port >> 8;
    dst_port[1] = port & (0xFF);
  }

  void printf_port() { printf("port=%02x%02x\n", dst_port[0], dst_port[1]); }

  uint16_t get_dst_port() { return (dst_port[0] << 8) + dst_port[1]; }
} __attribute__((packed));

struct QPInfo;

struct WrInfo {
  // modify on "post"
  QPInfo *qp;
  uint32_t signal_cnt{0};
  uint32_t one_signal_cnt{0};
  uint32_t send0_or_recv1;

  ibv_sge *sg_list;
  int num_sge;
  int b_id;

  void *get_addr() { return (void *)sg_list[0].addr; }

  // modify on "", used on
  CoroCtx *coro_ctx;
  uint32_t coro_wr_cnt;
};

template <int kMaxSG> struct SingleWr {
  int sg_num{0};
  struct ibv_sge* sg_list;
  struct ibv_send_wr wr;
  struct ibv_exp_send_wr exp_wr;

  // FIXME: not effient for the exp_wr;
  // FIXME: only support dct;
  void copy_wr() {
    memcpy(&exp_wr, &wr, sizeof(wr) - sizeof(wr.bind_mw));
    exp_wr.reserved = 0;
    exp_wr.exp_send_flags = wr.send_flags;
  }

  WrInfo wr_info;

  SingleWr() {
    sg_list = new ibv_sge[kMaxSG];
    reset_sg_num();
    wr.wr_id = (uint64_t) & (wr_info);
    wr_info.send0_or_recv1 = 0;
  }

  inline SingleWr &set_remote_ah(ibv_ah *ah_ptr, uint32_t remote_qpn) {
    wr.wr.ud.ah = ah_ptr;
    wr.wr.ud.remote_qpn = remote_qpn;
    wr.wr.ud.remote_qkey = UD_PKEY;
    return *this;
  }

  inline SingleWr &set_remote_dct_ah(ibv_ah *ah_ptr, uint32_t remote_qpn) {
    exp_wr.dc.ah = ah_ptr;
    exp_wr.dc.dct_number = remote_qpn;
    exp_wr.dc.dct_access_key = DCT_ACCESS_KEY;
    return *this;
  }

  inline SingleWr &add_sg_to_wr(Region *rg, uint32_t offset, uint32_t size) {
    auto &sg = sg_list[sg_num++];
    sg.addr = rg->addr + offset;
    sg.length = size;
    sg.lkey = rg->key;
    return *this;
  }

  inline SingleWr &reset_sg_num() {
    sg_num = 0;
    return *this;
  }

  inline SingleWr &count_sg_num() {
    wr.sg_list = sg_list;

    wr.num_sge = sg_num;
    reset_sg_num();

    return *this;
  }

  inline SingleWr &set_qp_info(QPInfo *qp) {
    wr_info.qp = qp;
    return *this;
  }

  inline SingleWr &set_signal_cnt(uint32_t cnt_) {
    wr_info.signal_cnt = cnt_;
    return *this;
  }

  inline SingleWr &set_one_signal_cnt(uint32_t cnt_) {
    wr_info.one_signal_cnt = cnt_;
    return *this;
  }

  inline SingleWr &rdma_remote(Region *rg, uint32_t offset) {
    wr.wr.rdma.remote_addr = rg->addr + offset;
    wr.wr.rdma.rkey = rg->key;
#ifdef LOGDEBUG
    printf("sg %lx\n", wr.wr.rdma.remote_addr);
#endif
    return *this;
  }

  inline SingleWr &set_read_op() { return set_op(IBV_WR_RDMA_READ); }

  inline SingleWr &set_write_op() { return set_op(IBV_WR_RDMA_WRITE); }

  inline SingleWr &set_send_op() { return set_op(IBV_WR_SEND); }

  inline SingleWr &set_op(enum ibv_wr_opcode opcode) {
    wr.opcode = opcode;
    return *this;
  }

  inline SingleWr &add_flags(int flags) {
    wr.send_flags |= flags;
    return *this;
  }

  inline SingleWr &clear_flags(int flags = 0xF) {
    wr.send_flags &= (!flags);
    return *this;
  }

  inline void atomic_done() {}

  inline void ud_done() {}
};

const static uint32_t kMaxSignal = 32;
template <int kMaxWr, int kMaxSG> struct BatchWr {

  struct SingleWr<kMaxSG>* wr;
  int wr_n;

  SingleWr<kMaxSG> &get_wr(int id = 0) { return wr[id]; }
  BatchWr () {
     wr = new SingleWr<kMaxSG>[kMaxWr];
  }

  inline void set_wr_n(int wr_n_) {
   
    wr_n = wr_n_;
    return;
  }

  inline void print() {
    for (int i = 0; i < wr_n; i++) {
      puts("TODO:");
    }
    return;
  }

  void set_qp_info(QPInfo *qp_info) {
    for (int i = 0; i < kMaxWr; i++)
      wr[i].set_qp_info(qp_info);
  }

  bool generate_and_issue_batch(QPInfo *qp_info, int begin_id = 0,
                                int wr_cnt = -1, bool one = false,
                                bool exp = false);
  bool generate_and_issue_batch_signal(QPInfo *qp_info,
                                       bool one = false); // FIXME:
  // void check_signal(QPInfo* qp_info);
};

template <int kMaxSG> struct BatchRecvWr {

  
  WrInfo *wr_info;
  ibv_recv_wr *wr;
  ibv_sge **sglist;
  int *current_sg;
  QPInfo *qp_info;
  int kMaxRecvWr;
  uint64_t get_addr() {
    return wr_info[0].sg_list[0].addr;
  }
  // int wr_n; // static, we do not need it;

  BatchRecvWr(int kMaxRecvWr_) {
    kMaxRecvWr = kMaxRecvWr_;
    wr_info = new WrInfo[kMaxRecvWr];
    wr = new ibv_recv_wr[kMaxRecvWr];
    sglist = new ibv_sge *[kMaxRecvWr];
    for (int i = 0; i < kMaxRecvWr; i++) {
      sglist[i] = new ibv_sge[kMaxSG];
    }
    current_sg = new int[kMaxRecvWr];
    for (int i = 0; i < kMaxRecvWr; i++) {
      wr[i].next = (i == kMaxRecvWr - 1) ? nullptr : &wr[i + 1];
      wr[i].sg_list = sglist[i];
      wr[i].wr_id = (uint64_t)&wr_info[i];
      init_sglist(i);
      wr_info[i].send0_or_recv1 = 1;
    }
  }

  void init_srq_wr_info(uint32_t batch_id) {
    for (int i = 0; i < kMaxRecvWr; i++) {
      wr_info[i].sg_list = sglist[i];
      wr_info[i].b_id = batch_id;
    }
  }

  void set_qp_info(QPInfo *qp_info_) {
    for (int i = 0; i < kMaxRecvWr; i++)
      wr_info[i].qp = qp_info_;
    qp_info = qp_info_;
  }

  void set_sg_info(uint w_id) {}

  void init_sglist(uint w_id) { current_sg[w_id] = 0; }

  void add_sg_to_wr(uint w_id, Region *rg, uint32_t offset, uint32_t size) {
    auto &sg = sglist[w_id][current_sg[w_id]++];
    sg.addr = rg->addr + offset;
    sg.length = size;
    sg.lkey = rg->key;
    wr_info[w_id].num_sge = current_sg[w_id];
  }

  void generate_batch() {
    for (int i = 0; i < kMaxRecvWr; i++) {
      wr[i].num_sge = current_sg[i];
    }
  }
  void post_batch();
};
// struct QPInfo;

struct CQInfo {
  const static int kMaxWcCnt = 16;
  // bool exp_flag = false;
  bool recv_cq_only = false;

  uint32_t cq_len;

  struct WcRet {
    int cur_wc_pos;
    int total_wc_count;
    struct ibv_wc *wc_ptr;
    struct ibv_exp_wc *exp_wc_ptr;

    bool exp_flag{false};

    struct ibv_wc *get_next_wc() {
      if (cur_wc_pos == total_wc_count)
        return nullptr;
      if (!exp_flag)
        return &wc_ptr[cur_wc_pos++];
      else {
        if (exp_wc_ptr[cur_wc_pos].exp_opcode == IBV_EXP_WC_RECV_NOP) 
          LOG(INFO) <<"WARNING! you are processing a NOP WC!";
        return (ibv_wc*)(&exp_wc_ptr[cur_wc_pos++]);
      }
    }
  } wc_ret;

  ibv_cq *cq;
  uint32_t signal_counter = 0;
  struct ibv_wc wc[kMaxWcCnt];
  struct ibv_exp_wc exp_wc[kMaxWcCnt];
  bool exp_flag = false;

  CQInfo() { wc_ret.wc_ptr = wc; wc_ret.exp_wc_ptr = exp_wc; }

  void initCq(RdmaCtx *rdma_ctx, int cqe = -1, bool exp_flag_ = false) {
    CreateCq(&cq, rdma_ctx, cqe);
    exp_flag = exp_flag_;
    wc_ret.exp_flag = exp_flag_;
    if (cqe == -1)
      cq_len = FLAGS_cq_len;
    else
      cq_len = cqe;
    return;
  }

  ibv_cq *get_cq() { return cq; }

  struct ibv_wc *poll_cq_once() {
    int ret;
    if (!exp_flag) {
      if ((ret = ibv_poll_cq(cq, 1, wc)) == 0)
        return nullptr;
    }
    else {
      if ((ret =ibv_exp_poll_cq(cq, 1, exp_wc, sizeof(ibv_exp_wc))) == 0)
        return nullptr;
    }
    if (ret < 0) {
      LOG(ERROR) << "Error in " << __func__;
      exit(-1);
    }
    #ifdef LOGDEBUG
    printf("poll cq once %d %d %d %x\n", ret, wc->status, wc->opcode, wc->wc_flags);
    #endif

    modify_send_qp_info_by_wc(1);
    if (!exp_flag)
      return &wc[0];
    else 
      return (ibv_wc*)(&exp_wc[0]);
  }

  struct ibv_wc *poll_cq_a_wc();

  struct CQInfo::WcRet *poll_cqi(uint wc_count = 1) {
    uint ret = 0;
    while (ret < wc_count) {
      
      
      int tmp;
      if (!exp_flag)
        tmp = ibv_poll_cq(cq, wc_count - ret, wc + (ret));
      else 
        tmp = ibv_exp_poll_cq(cq, wc_count - ret, exp_wc + (ret), sizeof(ibv_exp_wc));
      
      ret += tmp;
      if (tmp == -1) {
        LOG(ERROR) << "Error in " << __func__;
        exit(-1);
      }
    }

    wc_ret.cur_wc_pos = 0;
    wc_ret.total_wc_count = wc_count;
    modify_send_qp_info_by_wc(wc_count);

    return &(wc_ret);
  }

  struct CQInfo::WcRet *poll_cq_try(uint wc_count = 1) {
    assert(wc_count <= kMaxWcCnt);
    int ret;
    if (!exp_flag)
      ret = ibv_poll_cq(cq, wc_count, wc);
    else 
      ret = ibv_exp_poll_cq(cq, wc_count, exp_wc, sizeof(ibv_exp_wc));

    if (ret == -1) {
      LOG(ERROR) << "Error in " << __func__;
      exit(-1);
    }

    wc_ret.cur_wc_pos = 0;
    wc_ret.total_wc_count = ret;
    modify_send_qp_info_by_wc(ret);

    return &(wc_ret);
  }

  struct ibv_wc *get_wc(uint wc_id) { return &wc[wc_id]; }

  void modify_send_qp_info_by_wc(uint32_t cnt);
};

struct AHInfo {
  int valid{0};
  ibv_ah *ah{nullptr};
};

class QPInfo {
public:
#define POSTPIPE 64

  // const static int kBatchRecv = 32; // TODO: dynamic configuration
  uint32_t kBatchRecv = FLAGS_qp_len / POSTPIPE;
  
  const static uint32_t kBatchOne = 128;
  const static int kStaticSendBuf = kMaxSignal * 4;
  const static int kMaxSgList = 1;

  const static int kRawPacketHeader = 42;
  const static int kUDHeader = 40;

  int type{rdma::LOGRC};
  uint8_t mac[6];
  uint8_t ip[4];
  uint16_t src_port;
  uint16_t my_port;
  uint16_t steering_port;
  uint8_t qp_id;
  SrqConfig mp_config;
  // ibv_srq* srq;

  QPInfo *srq_ptr{nullptr};

  void set_qp_id(uint8_t _) { qp_id = _; }

  void set_srq(QPInfo *ptr_) { srq_ptr = ptr_; }

  int send_header_size[6] = {0, kRawPacketHeader, 0, 0, 0, 0};
  int recv_header_size[6] = {0, kRawPacketHeader, kUDHeader, 0, 0, 0};

  union {
    ibv_qp *qp;
    ibv_srq *srq;
    // ibv_exp_qp* exp_qp;
  } qpunion;
  ibv_exp_dct *dct;

  uint32_t recv_buf_cnt{0};
  uint32_t recv_processing_cnt{0};
  uint32_t recv_buf_pos{0};
  uint16_t recv_postpipe_pos{0};

  uint16_t send_pending_cnt{0};
  uint32_t send_counter{0};
  uint32_t send_buf_pos{0};
  uint32_t send_begin_id{0};
  uint32_t send_cnt_bf_signal{0};

  BatchRecvWr<kMaxSgList> *rpc_recv_wr[POSTPIPE];
  std::atomic<uint32_t> gsrq_count[POSTPIPE];
  uint32_t tsrq_count[POSTPIPE];
  BatchWr<kStaticSendBuf, kMaxSgList> rpc_send_wr; // for rpc (ibv send);

  uint32_t one_wr_pos{0};
  uint32_t one_pending_cnt{0};
  uint32_t one_begin_id{0};
  uint32_t one_counter{0};
  uint32_t one_cnt_bf_signal{0};
  BatchWr<kBatchOne, kMaxSgList> one_side_wr;

  CQInfo *send_cq{nullptr};
  CQInfo *recv_cq{nullptr};

  uint32_t send_offset;
  uint32_t recv_offset;
  uint32_t send_msg_size;
  uint32_t recv_msg_size;

  QPInfo(SrqConfig* srq_config = nullptr) {
     // TODO: dynamic configuration

    //  printf("%d %d\n",FLAGS_qp_len, kBatchRecv);
    if (srq_config != nullptr) {
      mp_config = *srq_config;
      if (mp_config.mp_flag) {
        kBatchRecv = 1;
      }
      else {
        if (mp_config.normal_srq_len != 0 && mp_config.normal_srq_len > FLAGS_qp_len)
          kBatchRecv = mp_config.normal_srq_len / POSTPIPE;
      }
    }
    
    
    for (int i = 0; i < POSTPIPE; i++) {
      // kBatchRecv = FLAGS_qp_len << 5; // TODO: dynamic configuration
      rpc_recv_wr[i] = new BatchRecvWr<kMaxSgList>(kBatchRecv);
      rpc_recv_wr[i]->set_qp_info(this);
    }
    rpc_send_wr.set_qp_info(this);
    one_side_wr.set_qp_info(this);
    qpunion.qp = nullptr;
    send_cnt_bf_signal = 0;
    one_cnt_bf_signal = 0;
  }

  uint32_t get_send_msg_num() { return kStaticSendBuf; }

  uint32_t get_recv_msg_num() { return kBatchRecv * POSTPIPE; }

  void config_msg_buf(uint32_t *send_offset_, uint32_t *recv_offset_,
                      uint32_t send_msg_size_, uint32_t recv_msg_size_) {
    send_offset = *send_offset_;
    recv_offset = *recv_offset_;
    send_msg_size = ALIGNMENT_XB(send_msg_size_, 64);
    recv_msg_size = ALIGNMENT_XB(recv_msg_size_, 64);
    *send_offset_ += (send_msg_size * get_send_msg_num());
    *recv_offset_ += (recv_msg_size * get_recv_msg_num());
  }

  void config_mp_msg_buf(uint32_t *recv_offset_,
                      SrqConfig srq_config_) {
    recv_offset = *recv_offset_;
    send_msg_size = 0;
    recv_msg_size = 1 << (srq_config_.log_stride_size + srq_config_.log_num_of_strides);
    *recv_offset_ += (recv_msg_size * POSTPIPE);
    mp_config = srq_config_;
  }

  void set_dct_type() { type = rdma::LOGDCT; }

  void set_srq_dct() {
    srq_ptr = this;
    qpunion.srq = dct->srq;
  }

  void config_raw_msg_buf(uint32_t *send_offset_, uint32_t *recv_offset_,
                          uint32_t send_msg_size_, uint32_t recv_msg_size_) {

    type = rdma::LOGRAW;
    config_msg_buf(send_offset_, recv_offset_,
                   send_msg_size_ + send_header_size[type],
                   recv_msg_size_ + recv_header_size[type]);
  }

  void config_ud_msg_buf(uint32_t *send_offset_, uint32_t *recv_offset_,
                         uint32_t send_msg_size_, uint32_t recv_msg_size_) {

    type = rdma::LOGUD;
    config_msg_buf(send_offset_, recv_offset_,
                   send_msg_size_ + send_header_size[type],
                   recv_msg_size_ + recv_header_size[type]);
  }

  void init_srq_with_one_wr(Region *mr) {

    if (type != rdma::LOGDCT)
      type = rdma::LOGGSRQ;
    // puts("?");
    for (int i = 0; i < POSTPIPE ; i++) {
      rpc_recv_wr[i]->init_srq_wr_info(i);
      gsrq_count[i] = kBatchRecv;
      tsrq_count[i] = kBatchRecv;
      for (uint32_t j = 0; j < kBatchRecv; j++) {
        rpc_recv_wr[i]->init_sglist(j);
        rpc_recv_wr[i]->add_sg_to_wr(
            j, mr, recv_msg_size * (i * kBatchRecv + j), recv_msg_size);
      }
      rpc_recv_wr[i]->generate_batch();
      
      // printf("%d %d\n", i, kBatchRecv);
      rpc_recv_wr[i]->post_batch();
    }
  }
  void change_type_for_tsrq() { type = LOGTSRQ; }

  // RECV
  void init_recv_with_one_mr(Region *mr, uint32_t offset, uint32_t msg_size);
  void init_recv_with_one_mr(Region *mr) {
    init_recv_with_one_mr(mr, recv_offset, recv_msg_size);
  }

  // SEND
  void init_send_with_one_mr(Region *mr, uint32_t offset, uint32_t msg_size);
  void init_send_with_one_mr(Region *mr) {
    init_send_with_one_mr(mr, send_offset, send_msg_size);
  }
  void init_raw_send_with_one_mr(Region *mr, uint32_t offset,
                                 uint32_t msg_size);
  void init_raw_send_with_one_mr(Region *mr) {
    init_raw_send_with_one_mr(mr, send_offset, send_msg_size);
  }

  void check_recv_buf_cnt();

  void *get_send_msg_addr() {
    return (char *)(get_send_msg_sg()->addr) + send_header_size[type];
  }

  // void* get_send_msg_addr() {
  //     return (char*)get_send_msg_addr() + kRawPacketHeader;
  // }

  SingleWr<kMaxSgList> &get_send_wr() {
    rpc_send_wr.get_wr(send_buf_pos).wr_info.coro_ctx = nullptr;
    // printf("%x send pos %d\n",qp, send_buf_pos);
    return rpc_send_wr.get_wr(send_buf_pos);
  }

  SingleWr<kMaxSgList> &get_one_wr() {
    one_side_wr.get_wr(one_wr_pos).wr_info.coro_ctx = nullptr;
    return one_side_wr.get_wr(one_wr_pos);
  }

  void check_signal() {
    if (send_cq->signal_counter == send_cq->cq_len) {
      auto wc_array = send_cq->poll_cq_try(16);
      if (wc_array->total_wc_count == 0)
        send_cq->poll_cq_a_wc();
    }
  }

  ibv_sge *get_send_msg_sg() {
    check_signal();
    while (send_counter + send_pending_cnt >= kStaticSendBuf) {
      if (send_counter == 0) {
        post_appended_smsg(nullptr, type == rdma::LOGDCT); // FIXME:
      }
      #ifdef LOGDEBUG
      printf("send_counter=%d + pending=%d\n", send_counter, send_pending_cnt); 
      #endif
      auto wr_ptr = send_cq->poll_cq_a_wc();
      #ifdef LOGDEBUG
      printf("after poll send_counter=%d + pending=%d\n", send_counter, send_pending_cnt); 
      #endif
      if (wr_ptr == nullptr && send_counter == 0) {
        LOG(ERROR) << "no enough msg";
        exit(0);
      }
      // if (wr_ptr!=nullptr){
      //     printf("send counter %d %d\n",send_counter, send_pending_cnt);
      // }
    }
    return get_send_wr().sg_list;
  }

  void guarantee_only_pending_n_wr(int cnt = 0) {
    while (send_counter + send_pending_cnt > 32)
      send_cq->poll_cq_a_wc();
  }

  void append_signal_smsg(bool signal = false, CoroCtx *ctx = nullptr, bool add_ack_cnt = false) {
    if (signal)
      rpc_send_wr.get_wr(send_buf_pos).add_flags(IBV_SEND_SIGNALED);
    else
      rpc_send_wr.get_wr(send_buf_pos).clear_flags(IBV_SEND_SIGNALED);
    
    if (signal && ctx != nullptr) {
        rpc_send_wr.get_wr(send_buf_pos).wr_info.coro_ctx = ctx;
        ctx->add_op_cnt();
    }

    // if (signal && ctx == nullptr)
    //     assert(false);
    
    ADD_ROUND(send_buf_pos, kStaticSendBuf);
    send_pending_cnt++;
  }

  void append_signal_one(CoroCtx *ctx, bool signal = false) {
    if (signal)
      one_side_wr.get_wr(one_wr_pos).add_flags(IBV_SEND_SIGNALED);
    else
      one_side_wr.get_wr(one_wr_pos).clear_flags(IBV_SEND_SIGNALED);

    if (ctx == nullptr) {
      LOG(INFO) << ("one side need ctx!");
      exit(0);
    }

    one_side_wr.get_wr(one_wr_pos).wr_info.coro_ctx = ctx;
    // ctx->add_op_cnt();

    ADD_ROUND(one_wr_pos, kBatchOne);
    one_pending_cnt++;
  }

  void add_smsg_flag(int send_flags) {
    rpc_send_wr.get_wr(send_buf_pos).add_flags(send_flags);
  }

  void clear_smsg_flag(int send_flags) {
    rpc_send_wr.get_wr(send_buf_pos).clear_flags();
  }

  void modify_smsg_dst_ip(uint8_t ip[4]) {
    RoCEHeader *roce_header = (RoCEHeader *)(get_send_msg_sg()->addr);
    memcpy(roce_header->dst_ip, ip, 4);
  }

  void modify_smsm_dst_mac(uint8_t mac[6]) {
    RoCEHeader *roce_header = (RoCEHeader *)(get_send_msg_sg()->addr);
    memcpy(roce_header->dst_mac, mac, 6);
  }
  //

  // UD
  // void modif_smsg_dst_ah(ibv_ah *ah_ptr, uint32_t remote_qpn) {
  //     if (type != 2)
  //         return;

  // }

  // RAW
  void modify_smsg_dst_port(uint16_t port) {
    if (type != LOGRAW) // Raw
      return;
    RoCEHeader *roce_header = (RoCEHeader *)(get_send_msg_sg()->addr);
    roce_header->set_dst_port(port);
    // roce_header->printf_port();
  }
  void modify_smsg_size(uint16_t size) {
    get_send_msg_sg()[0].length = size + send_header_size[type];
    if (type != LOGRAW) // Raw
      return;
    RoCEHeader *roce_header = (RoCEHeader *)(get_send_msg_sg()->addr);
    roce_header->set_size(ALIGNMENT_XB(size, 64)); 
  }

  void post_appended_smsg(CoroMaster *master = nullptr, bool dct = false) {
    if (send_pending_cnt == 0)
      return;
    // #ifdef LOGDEBUG
    // printf("!!! send_cnt = %d\n", send_pending_cnt);
    // #endif
    rpc_send_wr.generate_and_issue_batch(this, send_begin_id, send_pending_cnt,
                                         false, dct);
    send_begin_id = send_buf_pos;
    send_pending_cnt = 0;
    if (master != nullptr) {
      if (one_pending_cnt != 0) {
        LOG(INFO) << "please post all one-side msg before coroutine yield!";
        exit(0);
      }
      (*master)();
    }
  }

  void post_appended_one(CoroMaster *master = nullptr, bool dct = false) {
    if (one_pending_cnt == 0)
      return;

    one_side_wr.generate_and_issue_batch(this, one_begin_id, one_pending_cnt,
                                         true, dct);
    one_begin_id = one_wr_pos;
    one_pending_cnt = 0;
    if (master != nullptr)
      (*master)();
  }

  void *get_recv_msg_addr(ibv_wc *wc_ = nullptr) {


    if (!mp_config.mp_flag) {
      if (type == LOGTSRQ || type == LOGGSRQ) {
        return (void *) (((WrInfo *)(wc_->wr_id))->sg_list[0].addr);
      }
      return (void *)(get_recv_msg_sg()->addr + recv_header_size[type]);
    }

    if (wc_ == nullptr && mp_config.mp_flag) {
      puts("for mp wr, please use get_recv_msg_addr(ibv_wc *wc_)");
      exit(0);
    }

    auto wr_info = (WrInfo *)(wc_->wr_id);
    ibv_exp_wc* wc_exp_ = (ibv_exp_wc *)wc_;

    uint64_t base = (uint64_t)wr_info->sg_list[0].addr;
    uint64_t offset = wc_exp_->mp_wr.strides_offset * (1 << mp_config.log_stride_size);
    
    // if (wc_exp_->exp_wc_flags & IBV_EXP_WC_MP_WR_CONSUMED) {
    //   // get_recv_msg_sg();
    //   puts("new wr");
    //   free_recv_msg(wr_info->b_id);
    // }
    return (void *)(base + offset + recv_header_size[type]);
  }

  void print_port(uint64_t addr) {
    auto roce_header = (RoCEHeader *)(addr - recv_header_size[type]);
    roce_header->printf_port();
  }

  // void* get_raw_packet_recv_msg_addr() {
  //     return (char*)(get_recv_msg_addr()) + kRawPacketHeader;
  // }



  ibv_sge *get_recv_msg_sg() {
    int batch_id = recv_buf_pos / kBatchRecv;
    int slot_id = recv_buf_pos % kBatchRecv;

    ibv_sge *ret = rpc_recv_wr[batch_id]->sglist[slot_id];
    ADD_ROUND(recv_buf_pos, (POSTPIPE * kBatchRecv));
    recv_processing_cnt++;
    return ret;
  }

  void mp_check(ibv_wc* wc_ = nullptr) {

  }

  inline void free_recv_msg(ibv_wc* wc_ = nullptr) { // TODO:
    if (type != LOGGSRQ && type != LOGTSRQ) {
#ifdef LOGDEBUG
      printf("recv_buf_cnt %d %d\n", recv_buf_cnt, recv_processing_cnt);
#endif
      recv_buf_cnt -= recv_processing_cnt;
      recv_processing_cnt = 0;
      check_recv_buf_cnt();
    } else if (type == LOGTSRQ){
      // puts("free");
      if (mp_config.mp_flag) {
        // usleep(100000);
        auto wc_exp_ = (ibv_exp_wc *)wc_;
        // printf("a request flags = %lx offset = %d len=%d\n", wc_exp_->exp_wc_flags, wc_exp_->mp_wr.strides_offset, wc_exp_->mp_wr.byte_len);
        if (!(wc_exp_->exp_wc_flags & IBV_EXP_WC_MP_WR_CONSUMED)) {
          return;
        }
        // printf("over flags = %lx %lx offset = %d status=%lx len=%d\n", wc_exp_->exp_wc_flags, IBV_EXP_WC_MP_WR_CONSUMED, wc_exp_->mp_wr.strides_offset, wc_exp_->status, wc_exp_->mp_wr.byte_len);
        
      }
      auto wr_info = (WrInfo *)(wc_->wr_id);
      uint64_t ret = (--tsrq_count[wr_info->b_id]);
      if (ret == 0) {
        tsrq_count[wr_info->b_id] = kBatchRecv;
        // printf("!!!!!new %d\n", wr_info->b_id);
        rpc_recv_wr[wr_info->b_id]->post_batch();
      }
    }
    else {
      auto wr_info = (WrInfo *)(wc_->wr_id);
      uint64_t ret = (--gsrq_count[wr_info->b_id]);
      if (ret == 0) {
        gsrq_count[wr_info->b_id] = kBatchRecv;
        rpc_recv_wr[wr_info->b_id]->post_batch();
      }
    }
  }

  /**
   * @brief get the new msg.
   *
   * @return void*
   */
  void *process_next_msg() {
    void *ret = get_recv_msg_addr();
    free_recv_msg();
    return ret;
  }
};

template <int kMaxSG> forceinline void BatchRecvWr<kMaxSG>::post_batch() {
  ibv_recv_wr *wrBad = nullptr;
  int ret = 0;
  if (qp_info->type == LOGGSRQ || qp_info->type == LOGTSRQ) {
    ret = ibv_post_srq_recv(qp_info->qpunion.srq, wr, &wrBad);
  } else if (qp_info->type == LOGDCT) {
    ret = ibv_post_srq_recv(qp_info->dct->srq, wr, &wrBad);
  } else {
    ret = ibv_post_recv(qp_info->qpunion.qp, wr, &wrBad);
  }
  if (ret) {
    LOG(ERROR) << ("post recv failed: ") << (uint64_t)ret << " "
               << strerror(errno);
    // exit(0);
  }
  if (wrBad!=nullptr) {
    exit(-1);
  }
  qp_info->recv_buf_cnt += kMaxRecvWr;
}

forceinline void QPInfo::init_recv_with_one_mr(Region *mr, uint32_t offset,
                                               uint32_t msg_size) {

  for (int i = 0; i < POSTPIPE; i++) {
    for (uint32_t j = 0; j < kBatchRecv; j++) {
      rpc_recv_wr[i]->init_sglist(j);
      rpc_recv_wr[i]->add_sg_to_wr(
          j, mr, offset + msg_size * (i * kBatchRecv + j), msg_size);
      // printf("recv %lu %u\n", offset + size * (i * kBatchRecv + j), size);
    }
    rpc_recv_wr[i]->generate_batch();
    rpc_recv_wr[i]->post_batch();
  }
}

forceinline void QPInfo::init_send_with_one_mr(Region *mr, uint32_t offset,
                                               uint32_t msg_size) {
  for (int i = 0; i < kStaticSendBuf; i++) {
    rpc_send_wr.get_wr(i)
        .add_sg_to_wr(mr, offset + msg_size * i, msg_size)
        .add_flags(IBV_SEND_SIGNALED) // TODO:
        .set_send_op()
        .count_sg_num();
    // printf("send %lu %u\n", offset + size * i, size);
  }
}

forceinline void QPInfo::init_raw_send_with_one_mr(Region *mr, uint32_t offset,
                                                   uint32_t msg_size) {
  RoCEHeader roce_header;
  roce_header.set_src_port(src_port);
  roce_header.set_size(msg_size);

  for (int i = 0; i < kStaticSendBuf; i++) {
    uint64_t addr = mr->addr + offset + msg_size * i;
    memcpy((void *)addr, (void *)&roce_header, sizeof(RoCEHeader));
  }

  init_send_with_one_mr(mr, offset, msg_size);
}

forceinline void
QPInfo::check_recv_buf_cnt() { // it is ok for a extra running req;
  while (recv_buf_cnt < (POSTPIPE - 1) * kBatchRecv)
    rpc_recv_wr[(recv_postpipe_pos++) % POSTPIPE]->post_batch();
}

// template<int kMaxWr, int kMaxSG>
// forceinline bool BatchWr<kMaxWr, kMaxSG>::check_signal(QPInfo* qp_info) {

// }

template <int kMaxWr, int kMaxSG>
forceinline bool BatchWr<kMaxWr, kMaxSG>::generate_and_issue_batch(
    QPInfo *qp_info, int begin_id, int wr_cnt, bool one, bool exp) {

  if (wr_cnt != -1)
    wr_n = wr_cnt;
  for (int i = 0; i < wr_n; i++) {
    int id = (i + begin_id) % kMaxWr;
    int next_id = (i + begin_id + 1) % kMaxWr;

    wr[id].wr.next = (i == wr_n - 1) ? nullptr : &wr[next_id].wr;
    if (one)
      qp_info->one_cnt_bf_signal++;
    else
      qp_info->send_cnt_bf_signal++;


    if (qp_info->send_cnt_bf_signal + qp_info->one_cnt_bf_signal ==
            kMaxSignal &&
        (wr[id].wr.send_flags & IBV_SEND_SIGNALED) == 0) {
      wr[id].add_flags(IBV_SEND_SIGNALED);
    }
#ifdef LOGDEBUG
    printf("wr id=%d qp=%lx, op=%d flag=%x %d+%d\n", id, (int64_t)qp_info, wr[id].wr.opcode, wr[id].wr.send_flags, qp_info->send_cnt_bf_signal, qp_info->one_cnt_bf_signal);
#endif

    if (wr[id].wr.send_flags & IBV_SEND_SIGNALED) {

      if (one)
        wr[id].wr_info.coro_ctx->add_op_cnt();
      qp_info->send_cq->signal_counter++;
      qp_info->check_signal();
      wr[id].set_signal_cnt(qp_info->send_cnt_bf_signal);
      wr[id].set_one_signal_cnt(qp_info->one_cnt_bf_signal);
      qp_info->send_cnt_bf_signal = 0;
      qp_info->one_cnt_bf_signal = 0;
    }
    if (exp) {
      wr[id].copy_wr();
      wr[id].exp_wr.next = (i == wr_n - 1) ? nullptr : &wr[next_id].exp_wr;
#ifdef LOGDEBUG
      printf("wr id=%d qp=%lx, op=%d dct_num=%d dct_key=%d\n", id,
             (int64_t)qp_info, wr[id].exp_wr.exp_opcode,
             wr[id].exp_wr.dc.dct_number, wr[id].exp_wr.dc.dct_access_key);
#endif
    }
    // wr->wr_id = (uint64_t) qp_info;
  }

  int ret = 0;
  if (!exp) {
    struct ibv_send_wr *wrBad;
    ret = ibv_post_send(qp_info->qpunion.qp, &wr[begin_id].wr, &wrBad);
  } else {
    struct ibv_exp_send_wr *wrBad;
    ret = ibv_exp_post_send(qp_info->qpunion.qp, &wr[begin_id].exp_wr, &wrBad);
  }

  if (ret) {
    LOG(ERROR) << ("Send failed. :") << strerror(errno);
    *(char*)0 = -1;
    // exit(0);
    return false;
  }

  // TODO: exp is true;

  if (!one)
    qp_info->send_counter += wr_n;
  else
    qp_info->one_counter += wr_n;

  return true;
}

template <int kMaxWr, int kMaxSG>
forceinline bool
BatchWr<kMaxWr, kMaxSG>::generate_and_issue_batch_signal(QPInfo *qp_info,
                                                         bool one) {
  struct ibv_send_wr *wrBad;

  for (int i = 0; i < wr_n; i++) {
    wr[i].wr.send_flags = (i == wr_n - 1) ? IBV_SEND_SIGNALED : 0;
    wr[i].wr.next = (i == wr_n - 1) ? nullptr : &wr[i + 1].wr;
    if (!one)
      qp_info->send_cnt_bf_signal++;
    else
      qp_info->one_cnt_bf_signal++;
  }
  qp_info->send_cq->signal_counter++;

  if (!one) {
    qp_info->send_counter += wr_n;
  } else
    qp_info->one_counter += wr_n;

  wr[wr_n - 1].set_signal_cnt(qp_info->send_cnt_bf_signal);
  wr[wr_n - 1].set_one_signal_cnt(qp_info->one_cnt_bf_signal);
  qp_info->send_cnt_bf_signal = 0;
  qp_info->one_cnt_bf_signal = 0;

  if (int ret = ibv_post_send(qp_info->qpunion.qp, &wr[0].wr, &wrBad)) {
    LOG(ERROR) << "Send failed. " << wrBad->wr_id;
    exit(0);
  }
  return true;
}

// TODO: call back!
forceinline void CQInfo::modify_send_qp_info_by_wc(uint32_t cnt) {
  
  if (recv_cq_only) return;

  for (uint32_t i = 0; i < cnt; i++) {
    auto wc_info = (WrInfo *)wc[i].wr_id;
    if (wc_info->send0_or_recv1 == 0) {
// assert(wc_info->signal_cnt == 32);
#ifdef LOGDEBUG
      printf("poll a wc: qp=%lx ctx=%lx s_cnt=%d o_cnt=%d\n", wc_info->qp,
             wc_info->coro_ctx, wc_info->signal_cnt, wc_info->one_signal_cnt);
      printf("wc status %d %d %x\n", wc[i].status, wc[i].opcode, wc[i].wc_flags);
#endif
      signal_counter--;
      if (wc_info->coro_ctx != nullptr) {
#ifdef LOGDEBUG
        printf("poll a wc: sub %x\n", wc_info->coro_ctx->async_op_cnt);
#endif
        wc_info->coro_ctx->async_op_cnt--;
      }
      wc_info->qp->one_counter -= wc_info->one_signal_cnt;
      wc_info->qp->send_counter -= wc_info->signal_cnt;
    }
    if (wc[i].status != IBV_WC_SUCCESS) {
      LOG(ERROR) << "Failed id=" << wc[i].status
                 << " status:" << ibv_wc_status_str(wc[i].status);
      exit(0);
    }
  }
}


forceinline struct ibv_wc * CQInfo::poll_cq_a_wc()
  {
    ibv_wc *ret = nullptr;
    while (1) {
      if ((ret = poll_cq_once()) != nullptr){
        if (exp_flag) {
          if (((ibv_exp_wc*)ret)->exp_opcode == IBV_EXP_WC_RECV_NOP) {
            #ifdef LOGDEBUG
            puts("RECV_NOP");
            #endif
            auto wr_info = (WrInfo*)ret->wr_id;
            (wr_info->qp)->free_recv_msg(ret);
            continue;
          }
        }
        break;
      }
    }
    return ret;
  }
} // namespace rdma


