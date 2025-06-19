#if !defined(JUNEBERRY_COMMON_H_)
#define JUNEBERRY_COMMON_H_

#include "distribution.h"
#include "juneberry_rpc_base.h"
#include "message.h"

namespace juneberry {

DECLARE_int32(client_thread);
DECLARE_int32(server_thread);
DECLARE_int32(client_cnt);

extern rdma::GlobalConfig<rpc::kConfigServerCnt> global_config;
extern std::vector<rdma::ServerConfig> machine_config;
extern perf::PerfConfig perf_config;

#define NO_RESP_FOR_OPEN_LOOP
// #define EMULATE_HW_ACK

#define kLogNumOfStrides 10
#define kLogStrideSize 6

<<<<<<< HEAD
=======
struct Message {
  uint8_t src_s_id;
  uint8_t src_t_id;
  bool hw_flag;
  bool is_open_loop;
  uint8_t pad[60];
} __attribute__((packed));
// static_assert(sizeof(RawKvMsg) == 64, "XXX");

>>>>>>> 7702194... fix
inline void config() {
  rdma::ServerConfig item;

  global_config.func = rdma::ConnectType::T_LOCAL_SRQ;
<<<<<<< HEAD
  global_config.rc_msg_size = kMaxMsgSize;
=======
  global_config.rc_msg_size = sizeof(Message);
>>>>>>> 7702194... fix
  global_config.srq_config.mp_flag = true;
  global_config.srq_config.log_stride_size = kLogStrideSize;
  global_config.srq_config.log_num_of_strides = kLogNumOfStrides;

  global_config.one_sided_rw_size = 4;
  global_config.link_type = rdma::kEnumIB;

  machine_config.push_back(item = {
                               .thread_num = FLAGS_server_thread,
                               .numa_id = 0,
                               .numa_size = 18,
                               .dev_id = 0,
                               .srq_receiver = 1,
                               .srq_sender = 0,
                               .recv_cq_len = 512 * 128,
                               .send_cq_len = 512 * 4,
                           });

  for (int i = 0; i < FLAGS_client_cnt; ++i) {
    machine_config.push_back(item = {
                                 .thread_num = FLAGS_client_thread,
                                 .numa_id = 0,
                                 .numa_size = 12,
                                 .dev_id = 0,
                                 .srq_receiver = 0,
                                 .srq_sender = 1,
                                 .recv_cq_len = 128 * 16,
                                 .send_cq_len = 128 * 16,
                             });
  }

  if (FLAGS_client_cnt == 4) {
    machine_config[4].dev_id = 2;
  }

  for (int i = 0; i < rpc::kConfigServerCnt; i++) {
    for (int j = 0; j < rpc::kConfigServerCnt; j++) {
      if (i != 0 && j != 0) {
        continue;
      }
      global_config.matrix[i][j] = rdma::TopoType::All;
      global_config.matrix_TSRQ[i][j] = rdma::TopoType::All;
    }
  }

  assert(machine_config.size() <= rpc::kConfigServerCnt);
}
} // namespace juneberry
#endif // JUNEBERRY_COMMON_H_

/*
=============================================
client:

SQ length: kStaticSendBuf = kMaxSignal * 4; // qp_info.h:489
SQ buffer size: global_config.rc_msg_size
SQ_CQ length: machine_config.send_cq_len

RQ length: qp_len
RQ buffer size: global_config.rc_msg_size
RQ_CQ length: machine_config.recv_cq_len

==============================================
server:

SQ length: kStaticSendBuf = kMaxSignal * 4; // qp_info.h:489
SQ buffer size: global_config.rc_msg_size
SQ_CQ length: machine_config.send_cq_len

RQ length: 0 (srq != nullptr)
SRQ length: POSTPIPE
RQ buffer size: MP stride * MP num
RQ_CQ length: machine_config.recv_cq_len

*/