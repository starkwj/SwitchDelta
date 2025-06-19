#pragma once

#include <assert.h>
#include <cstdio>
#include <gflags/gflags.h>
#include <glog/logging.h>

// #include "memcached.h"
#include "bindcore.h"
#include "perfv2.h"
#include "qp_info.h"
#include "rdma.h"
#include "size_def.h"

namespace rpc {

constexpr int kConfigServerCnt = 6;
constexpr int kConfigMaxThreadCnt = 48;
constexpr int kConfigMaxQPCnt = 1;
constexpr int kConfigCoroCnt = 64;

DECLARE_int32(node_id);
DECLARE_int32(qp_type);
DECLARE_int32(coro_num);
DECLARE_int32(port);

struct RpcHeader {
  // route header
  uint8_t dst_s_id;

  // rpc header
  uint8_t src_s_id; // 1<<3
  uint8_t src_t_id; // 1<<4
  uint8_t src_c_id; // 1<<4
  uint8_t local_id;
  uint8_t op;
  uint32_t payload_size;

} __attribute__((packed));

class Rpc {
public:
  static const int kCoroWorkerNum = kConfigCoroCnt;
  rdma::Rdma<kConfigServerCnt, kConfigMaxThreadCnt, kConfigMaxQPCnt> *agent;
  CoroSlave **coro_slave; 
  rdma::CoroCtx *coro_ctx;
  char* buffer;

  perf::PerfTool *reporter;
  uint8_t server_id;
  uint8_t thread_id;

  Rpc() {
    coro_slave = new CoroSlave *[kCoroWorkerNum];
    coro_ctx = new rdma::CoroCtx[kCoroWorkerNum];
  }

  void config_client(std::vector<rdma::ServerConfig> &server_list,
                     rdma::GlobalConfig<kConfigServerCnt> *topology,
                     int server_id_, int thread_id_) {
    // BindCore(server_list[server_id_].numa_id * server_list[server_id_].numa_size);
    BindCore(server_list[server_id_].numa_id * server_list[server_id_].numa_size + thread_id_);
    
    agent =
        new rdma::Rdma<kConfigServerCnt, kConfigMaxThreadCnt, kConfigMaxQPCnt>(
            server_list, topology, server_id_, thread_id_);
  }

  void init() {
    server_id = agent->get_server_id();
    thread_id = agent->get_thread_id();
    // BindCore(client->server.get_core_id(thread_id));
    // LOG(INFO) <<"init over ( "<< thread_id <<" )";
  }
  void connect() {
    // BindCore(agent->server.numa_id * agent->server.numa_size);
    BindCore(agent->server.get_core_id(thread_id));
    uint32_t mm_size = agent->global_config->one_sided_rw_size * MB;
    void *mm_addr = malloc(mm_size + 64);
    buffer = (char *)mm_addr;
    agent->config_rdma_region(0, (uint64_t)mm_addr, mm_size);
    // puts("?xxxx");
    
    uint32_t recv_size = agent->get_recv_buf_size();
    void *recv_addr = malloc(recv_size + 64);
    recv_addr = (void *)(((uint64_t)recv_addr + 63) & ~63);
    agent->config_recv_region((uint64_t)recv_addr, recv_size);
    // puts("?xxxx");
    agent->connect();

    uint32_t send_size = agent->get_send_buf_size();
    void *send_addr = malloc(send_size + 64);
    send_addr = (void *)(((uint64_t)send_addr + 63) & ~63);
    agent->config_send_region((uint64_t)send_addr, send_size);
    
    // BindCore(agent->server.get_core_id(thread_id));

    if (reporter != nullptr)
      reporter->thread_begin(thread_id);
    // #ifdef LOGDEBUG
    // if (server_id == 0)
    //   memset(buffer, -1, agent->global_config->one_sided_rw_size * MB);
    // #endif
  }

  virtual void server_process_msg(void *req_addr, void *reply_addr,
                                  rdma::QPInfo *qp) = 0;
  virtual void server_prepare() = 0;

  void run_server_CPC() {

    init();
    
    server_prepare();
    
    connect();

    while (true) {
      auto wc_array = agent->cq->poll_cq_try(16);
      rdma::QPInfo * qp_array[16];
      int cnt = 0;

      ibv_wc *wc = nullptr;
      while ((wc = wc_array->get_next_wc())) {
        auto qp = ((rdma::WrInfo *)wc->wr_id)->qp;
        auto req = qp->get_recv_msg_addr();
        auto &req_header = *(RpcHeader *)req;

        rdma::QPInfo* send_cq;

        // if (thread_id == 0) {
        //   send_cq = &agent->get_rc_qp(req_header.src_s_id, req_header.src_t_id);
        // }
        // else {
        send_cq = &agent->get_raw_qp(0);
        // } 
        auto reply = send_cq->get_send_msg_addr();

        auto &reply_header = *(RpcHeader *)reply;
        reply_header = req_header;

        if (send_cq->type == 1) {
          send_cq->modify_smsg_dst_port(
              agent->get_thread_port(req_header.src_t_id, send_cq->qp_id));
          reply_header.dst_s_id = req_header.src_s_id;
        } else if (send_cq->type == 2) {
          agent->modify_ud_ah(req_header.src_s_id, req_header.src_t_id,
                              send_cq->qp_id);
          reply_header.dst_s_id = req_header.src_s_id;
          // ah;
        }

        server_process_msg(req, reply, send_cq);

        qp->free_recv_msg();
        send_cq->append_signal_smsg();
        qp_array[cnt++] = send_cq;
      }
      for (int i = 0; i < cnt; i++) {
        qp_array[i]->post_appended_smsg();
      }
      // agent->get_raw_qp(0).post_appended_smsg();



    }
  }

  // void server_

  virtual void cworker_new_request(CoroMaster &sink, uint8_t coro_id) = 0;

  void client_worker(CoroMaster &sink, uint8_t coro_id) {
    sink();

    auto &ctx = coro_ctx[coro_id];
    while (true) {
      ctx.free = false;

      cworker_new_request(sink, coro_id);

      ctx.free = true;
      sink();
    }
  }

  virtual void client_prepare() = 0;

  virtual int8_t client_get_cid(void *reply_addr) = 0;

  virtual void after_process_reply(void *reply_addr, uint8_t c_id) {}

  inline rdma::QPInfo *rpc_get_qp(uint8_t type, uint8_t server_id,
                                  uint8_t thread_id, uint8_t qp_id) {
    rdma::QPInfo *qp;
    if (type == rdma::LOGRAW) {
      qp = &agent->get_raw_qp(qp_id);
      qp->modify_smsg_dst_port(
          agent->get_thread_port(thread_id, qp_id)); // remote thread_id
      return qp;
    } else if (type == rdma::LOGRC) {
      qp = &agent->get_rc_qp(server_id, thread_id, qp_id);
      return qp;
    } else if (type == rdma::LOGUD) {
      qp = &agent->get_ud_qp(qp_id);
      agent->modify_ud_ah(server_id, thread_id, qp_id);
      return qp;
    } else if (type == rdma::LOGGSRQ) {
      qp = &agent->get_global_srq_qp(server_id, thread_id, qp_id);
      return qp;
    }
    else if (type == rdma::LOGTSRQ) {
      qp = &agent->get_thread_srq_qp(server_id, thread_id, qp_id);
      return qp;
    }
    else if (type == rdma::LOGDCT) {
      qp = &agent->get_rc_qp(server_id, thread_id, qp_id, true);
      agent->modify_dct_ah(server_id, thread_id, qp_id);
      return qp;
    }
    return nullptr;
  }

  void run_client_CPC() {

    init();

    client_prepare();

    connect();

    

    sleep(5); // !!! wait for remote server !!!
    
    for (int i = 0; i < FLAGS_coro_num; i++) {
      using std::placeholders::_1;
      coro_slave[i] =
          new CoroSlave(std::bind(&Rpc::client_worker, this, _1, i));
      coro_ctx[i].async_op_cnt = -1;
    }

    while (true) {
      int one_side = 0;
      for (int i = 0; i < FLAGS_coro_num; i++) {
        if (coro_ctx[i].free) {
          (*coro_slave[i])(); // new request
        }
        if (coro_ctx[i].async_op_cnt == 0 && coro_ctx[i].free == false) {
          coro_ctx[i].async_op_cnt = -1;
          #ifdef LOGDEBUG
          LOG(INFO) << "coro " << i << " rdma";
          #endif
          (*coro_slave[i])(); // one-sided RDMA
        }
        if (coro_ctx[i].async_op_cnt != 0 && coro_ctx[i].async_op_cnt != -1) {
          one_side += coro_ctx[i].async_op_cnt;
        }
      }

      if (one_side != 0) {
        agent->send_cq->poll_cq_try(std::min(one_side, 16));
      }

      auto wc_list = agent->cq->poll_cq_try(16);
      ibv_wc *wc = nullptr;
      while ((wc = wc_list->get_next_wc())) {

        auto qp = ((rdma::WrInfo *)wc->wr_id)->qp;
        auto reply = qp->get_recv_msg_addr();
        // puts("new reply");
        // auto& reply_header = *(RpcHeader*)reply;

        int8_t c_id = client_get_cid(reply);
        if (c_id != -1) {
          coro_ctx[c_id].msg_ptr = reply;
          #ifdef LOGDEBUG
          LOG(INFO) << "coro " << (int)c_id << " reply";
          #endif
          (*coro_slave[c_id])();
          after_process_reply(reply, c_id);
        }

        qp->free_recv_msg();
      }
    }
    return;
  }
};

} // namespace rpc