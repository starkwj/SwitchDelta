#include "rdma_example.h"
DECLARE_double(test_size_kb);

extern rdma::RdmaCtx rdma_ctx;
extern rdma::RegionArr<kMaxLocalMRNum> local_mr;
extern rdma::ConnectGroup<kNodeNum, kMaxQPNum, kMaxRemoteMRNum> connects;
extern rdma::CQInfo cq;
extern rdma::BatchWr<kBatchWRNum, 1> batch_wr[kMaxQPNum][2];

/**
 * @brief
 *
 * @param flag 0:one-sided read | 1:one-sided write
 */
struct RawKvMsg {
  uint8_t dst_s_id;
  uint64_t key;
  uint64_t value;
};

void test_rdma(int flag = 0) {

	int dest_id = 0;
	int task_num = kBatchWRNum;
	std::string flag_str[5] = {"write", "read", "send", "send", "raw packet"};

	for (int qp_num = 1; qp_num <= kMaxQPNum; qp_num *= 2) {
		assert((kMaxQPNum & (kMaxQPNum - 1)) == 0);

		perf::Timer timer;

		for (int count = -kWarmUp; count < kTestCount; count++) {
			if (count == 0) {
				timer.begin();
			}
			for (int i = 0; i < qp_num; i++) {
				int batch_wr_id = i % 2;
				if (flag == 0 || flag == 1 || flag == 2)
					connects.remote_node_[dest_id].qp[i].guarantee_only_pending_n_wr(kBatchWRNum);

				for (int j = 0; j < task_num; j++) {
					int task_id = i * task_num + j;
					auto& l_mr = local_mr.region[task_id % kMaxLocalMRNum];
					auto& qp = connects.remote_node_[dest_id].qp[i];

					if (flag <= 1) {
						auto& r_mr = connects.remote_node_[dest_id].remote_mr.region[task_id % kMaxLocalMRNum];
						if (flag == 0)
							batch_wr[i][batch_wr_id].get_wr(j).set_read_op();
						else
							batch_wr[i][batch_wr_id].get_wr(j).set_write_op();
						batch_wr[i][batch_wr_id]
							.get_wr(j)
							.add_sg_to_wr(&l_mr, 0, FLAGS_test_size_kb * KB)
							.count_sg_num()
							.set_qp_info(&qp)
							.rdma_remote(&r_mr, 0)
							.clear_flags();
					}
					else if (flag == 2) {
						batch_wr[i][batch_wr_id]
							.get_wr(j)
							.reset_sg_num()
							.set_op(IBV_WR_SEND)
							.add_sg_to_wr(&l_mr, 0, FLAGS_test_size_kb * KB)
							.count_sg_num()
							.set_qp_info(&qp)
							.clear_flags();
					}
					else if (flag == 3) {
						auto addr = connects.remote_node_[dest_id].qp[i].get_send_msg_addr();
						*(uint64_t*)addr = 1;
						connects.remote_node_[dest_id].qp[i].append_signal_smsg(j == task_num - 1);
					}
					else if (flag == 4) {
						auto msg = (RawKvMsg *)connects.raw_qp[i].get_send_msg_addr();
						msg->dst_s_id = 0;
						connects.raw_qp[i].modify_smsg_dst_port(i);
						connects.raw_qp[i].append_signal_smsg(j == task_num - 1);
					}
					// printf("send qp_id = %d %d\n", i, j);
				}
				if (flag == 0 || flag == 1 || flag == 2) { 
					batch_wr[i][batch_wr_id].set_wr_n(task_num);
					batch_wr[i][batch_wr_id].generate_and_issue_batch_signal(&connects.remote_node_[dest_id].qp[i]);
				}
				else if (flag == 3) 
					connects.remote_node_[dest_id].qp[i].post_appended_smsg();
				else if (flag == 4) {
					connects.raw_qp[i].post_appended_smsg();
				}
				 	 
			}
		}

		uint64_t duration = timer.end();
		
		LOG(INFO) << "QPs:" << qp_num
			<< flag_str[flag]
			<< " bw= " << 1.0 * kTestCount * FLAGS_test_size_kb * (SDIVNS)*task_num * qp_num / (duration * (GB / KB)) << " GB/s"
			<< " lat=" << duration / (kTestCount * task_num * qp_num) / (1e3);
	}
	return;
}
