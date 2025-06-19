/*
 * task_context[slave_num]: op(read/write key value) 
 * free_slave[slave_num]: free?
 * event_counter[slave_num]: event_number.
 * rpc_context[rpc_num]:
 */

/* master
 * 1. init all slave
 * 2. call slaves
 * 3. while (1)
 * 		generate new task, find free_slave[i], copy to task_context[i], call slave[i]
 * 
 * 		poll send cq, call wc_info->func;
 * 		poll reply, call rpc_info->func;
 * 		msg_bit TODO:
 * 		
 *  
 */

/* slave
 * 1. while(1)
 *  	call master;
 * 		read task 
 *		get msg buf
 * 
 *		// call 3 rdma
 * 		call rdma to a qp, (count, null));
 * 		call rdma to a qp, (count, null));
 * 		call rdma to a qp, (count, master));
 * 		
 * 
 * 		// call 3 rpc
 * 		call rpc to a qp, (, null);
 * 		call rpc to a qp, (, null);
 * 		call rpc to a qp, (reply, master);
 *      get->msg;     
 * 
 * 		free reply1, reply2, reply3.
 * 		
 * 		call 
 * 
 */