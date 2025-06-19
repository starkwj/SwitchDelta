#include <glog/logging.h>
#include <gflags/gflags.h>
#include "rdma.h"
#include "size_def.h"
#include "timer.h"
#include "memcached_mgr.h"
#include "bindcore.h"
#include <assert.h>

constexpr int kConfigServerCnt = 2;
constexpr int kConfigMaxThreadCnt = 8;
constexpr int kConfigMaxQPCnt = 1;

constexpr int kConfigCoroCnt = 64; 

#define kTestCount 1024
#define kWarmUp 10
