#include <glog/logging.h>
#include <gflags/gflags.h>
#include "rdma.h"
#include "size_def.h"
#include "memcached_mgr.h"
#include "timer.h"
#include "bindcore.h"
#include <assert.h>
#include "test.h"

DEFINE_int32(node_id, 0, "node id");
DEFINE_int32(core_id, 0, "core id");
DEFINE_double(test_size_kb, 1024, "test size KB");
DEFINE_int32(mr_size_mb, 1, "mr size mb");
DEFINE_int32(dev_id, 0, "dev id");
// using namespace std;
// using namespace rdma;

rdma::RdmaCtx rdma_ctx;
rdma::RegionArr<kMaxLocalMRNum> local_mr;
rdma::ConnectGroup<kNodeNum, kMaxQPNum, kMaxRemoteMRNum> connects;
rdma::CQInfo cq;
rdma::BatchWr<kBatchWRNum, 1> batch_wr[kMaxQPNum][2];

std::string setKey(int my_node_id, int remote_node_id, std::string info) {
    return "FROM" + std::to_string(my_node_id)
        + "TO" + std::to_string(remote_node_id)
        + info;
}

std::string getKey(int my_node_id, int remote_node_id, std::string info) {
    return "FROM" + std::to_string(remote_node_id) +
        "TO" + std::to_string(my_node_id)
        + info;
}


int main(int argc, char** argv) {

    FLAGS_logtostderr = 1;
    gflags::SetUsageMessage("Usage ./rdma_client --help");
    gflags::ParseCommandLineFlags(&argc, &argv, true);
    google::InitGoogleLogging(argv[0]);

    BindCore(FLAGS_core_id);

    rdma::CreateCtxSimple(&rdma_ctx, FLAGS_dev_id, rdma::kEnumIB);
    rdma::CreateCq(&cq->cq, &rdma_ctx);

    // LOG(INFO) << "memory region";
    for (uint32_t i = 0; i < kMaxLocalMRNum; i++) {
        auto& rg = local_mr.region[i];
        rg.size = FLAGS_mr_size_mb * MB;
        rg.addr = (uint64_t)malloc(rg.size);
        memset((void*)rg.addr, 0, rg.size);
        rg.mr = rdma::CreateMr(&(rdma_ctx), rg.addr, rg.size);
        rg.key = rg.mr->lkey;

        // LOG(INFO) << "addr:" << rg.addr << " size:" << rg.size << " key:" << rg.key;
    }

    //LOG(INFO) << "RC QP";
    // RC
    for (int i = 0; i < kNodeNum; i++) {
        if (i == FLAGS_node_id) continue;
        for (uint32_t j = 0; j < kMaxQPNum; j++) {
            rdma::create_qp(&connects.remote_node_[i].qp[j].qp,
                &(rdma_ctx), cq.get_cq(), cq.get_cq(), IBV_QPT_RC);
            connects.remote_node_[i].local_qp_num[j] = connects.remote_node_[i].qp[j].qp->qp_num;

            //LOG(INFO) << "(node,qp):" << i << "," << j << " qp_num:" << connects.remote_node_[i].qp[j]->qp_num;
        }
    }

    // 
    Memcached::initMemcached(FLAGS_node_id);
    sleep(1);

    {
        std::string set_key = setKey(FLAGS_node_id, FLAGS_node_id, "id");
        auto& id = rdma_ctx.nic_id;
        Memcached::get_instance().memcachedSet(set_key, (char*)&id, sizeof(id));

        set_key = setKey(FLAGS_node_id, FLAGS_node_id, "mr");
        auto& mrs = local_mr;
        Memcached::get_instance().memcachedSet(set_key, (char*)&mrs, sizeof(mrs));
    }

    for (int i = 0; i < kNodeNum; i++) {
        if (i == FLAGS_node_id) continue;

        std::string set_key = setKey(FLAGS_node_id, i, "qp_num");
        auto& qps = connects.remote_node_[i].local_qp_num;
        Memcached::get_instance().memcachedSet(set_key, (char*)qps, sizeof(qps));
    }

    for (int i = 0; i < kNodeNum; i++) {
        if (i == FLAGS_node_id) continue;

        std::string get_key = getKey(FLAGS_node_id, i, "qp_num");
        auto& qpns = connects.remote_node_[i].remote_qp_num;
        Memcached::get_instance().memcachedGet(get_key, (char*)&qpns, sizeof(qpns));

        get_key = getKey(i, i, "mr");
        auto& mrs = connects.remote_node_[i].remote_mr;
        Memcached::get_instance().memcachedGet(get_key, (char*)&mrs, sizeof(mrs));

        get_key = getKey(i, i, "id");
        auto& id = connects.remote_node_[i].remote_nic_id;
        Memcached::get_instance().memcachedGet(get_key, (char*)&id, sizeof(id));

        auto& qps = connects.remote_node_[i].qp;
        for (uint32_t j = 0; j < kMaxQPNum; j++) {
            rdma::modifyQPtoInit(&rdma_ctx, qps[j].qp);
            rdma::modifyQPtoRTR(&rdma_ctx, qps[j].qp, qpns[j], id.lid, id.gid.raw);
            rdma::modifyQPtoRTS(qps[j].qp);
        }
        /*
        LOG(INFO) << "Remote info of:" << i << " lid: " << id.lid;
        for (int j = 0; j < kMaxQPNum; j++)
            LOG(INFO) << "qp num:" << qpns[j];
        for (int j = 0; j < kMaxRemoteMRNum; j++)
            LOG(INFO) << "MR info (addr, key)" << mrs.region[j].addr << "," << mrs.region[j].key;
        */
    }

    LOG(INFO) << "Connected!";

    if (FLAGS_node_id == 0) {
        while (true);
    }

    test_rdma(true);
    test_rdma(false);

    return 0;
}