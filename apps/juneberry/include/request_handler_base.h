#if !defined(_REQUEST_HANDLER_BASE_H_)
#define _REQUEST_HANDLER_BASE_H_

#include "message.h"

namespace juneberry {

class RequestHandlerBase {
protected:
  uint8_t node_id;
  uint8_t thread_id;

public:
  RequestHandlerBase(uint8_t node_id, uint8_t thread_id)
      : node_id(node_id), thread_id(thread_id) {}

  virtual void handle_request(Message *req, Message *resp) = 0;
};

class MockRequestHandler : public RequestHandlerBase {
public:
  MockRequestHandler(uint8_t node_id, uint8_t thread_id)
      : RequestHandlerBase(node_id, thread_id) {}

  void handle_request(Message *req, Message *resp) override {}
};

} // namespace juneberry

#endif // _REQUEST_HANDLER_BASE_H_
