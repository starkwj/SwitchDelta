#pragma once

#include <glog/logging.h>
#include <gflags/gflags.h>
#include "rdma.h"
#include "size_def.h"
#include "timer.h"
#include "memcached_mgr.h"
#include "bindcore.h"
#include <assert.h>

constexpr int32_t kMRNum = 8;

constexpr int32_t kMaxLocalMRNum = kMRNum;
#define kMaxRemoteMRNum kMRNum
#define kMaxQPNum kMRNum
#define kBatchWRNum 32
#define kNodeNum 2
#define kTestCount 1024
#define kWarmUp 10