#if !defined(_REQUEST_GENERATOR_H_)
#define _REQUEST_GENERATOR_H_

#include <cstdint>
#include <cstdio>
#include <gflags/gflags.h>
#include <random>

#include "message.h"

DECLARE_int64(hw_resp);

namespace juneberry {

class RequestGeneratorBase {

protected:
  uint8_t node_id;
  uint8_t thread_id;
  uint8_t server_thread_cnt;

public:
  RequestGeneratorBase(uint8_t node_id, uint8_t thread_id,
                       uint8_t server_thread_cnt)
      : node_id(node_id), thread_id(thread_id),
        server_thread_cnt(server_thread_cnt) {}

  virtual uint8_t next_target_thread_id() = 0;
  virtual void fill_request(Message *msg) = 0;
};

class MockRequestGenerator : public RequestGeneratorBase {

private:
  unsigned int random_seed;

public:
  MockRequestGenerator(uint8_t node_id, uint8_t thread_id,
                       uint8_t server_thread_cnt)
      : RequestGeneratorBase(node_id, thread_id, server_thread_cnt),
        random_seed(thread_id) {}

  uint8_t next_target_thread_id() override {
    return rand_r(&random_seed) % server_thread_cnt;
  }

  void fill_request(Message *msg) override {
    msg->hw_resp = rand_r(&random_seed) % 100 < FLAGS_hw_resp;
  }
};

} // namespace juneberry

#endif // _REQUEST_GENERATOR_H_
