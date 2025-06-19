#if !defined(_JUNEBERRY_MESSAGE_H_)
#define _JUNEBERRY_MESSAGE_H_

#include <cstdint>

namespace juneberry {

#define kMaxMsgSize 256
struct Message {

  // source information
  uint8_t node_id;
  uint8_t thread_id;

  // for juneberry
  bool hw_resp;

  // for benchmarking
  bool is_open_loop;

  // for payload
  uint32_t payload_size;
  char payload[0];

  void clear() { this->payload_size = 0; }

  uint32_t get_size() { return sizeof(Message) + payload_size; }

} __attribute__((packed));

} // namespace juneberry

#endif // _JUNEBERRY_MESSAGE_H_
