/* c_d_buffers.c */
/*
 * The OpenFabrics suite of examples is code developed for the Programming
 * with OpenFabrics Software Training Course.
 *
 * Copyright (c) 2011 OpenFabrics Alliance, Inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * GNU_GPL_OFA.txt in the directory containing this source file, or the
 * OpenIB.org BSD license, available from the file BSD_for_OFA.txt in the
 * directory containing this source file.
 */


#define _POSIX_C_SOURCE 200112L
#define _ISOC99_SOURCE
#define _XOPEN_SOURCE 600

static int buffer_size = 64, inline_size = 64;

#include "prototypes.h"


static void
our_print_buffer_numbers(struct our_control *conn, const char *name,
						struct our_options *options)
{
	/* print out our buffer allocation numbers */
	our_trace_ulong(name, "n_user_data_bufs",
					conn->n_user_data_bufs, options);
}	/* our_print_buffer_numbers */


static void
our_setup_sge(void *addr, unsigned int length, unsigned int lkey,
		struct ibv_sge *sge)
{
	/* point at the memory area */
	sge->addr = (uint64_t)(unsigned long)addr;

	/* set the number of bytes in that memory area */
	sge->length = length;

	/* set the registration key for that memory area */
	sge->lkey = lkey;
}	/* our_setup_sge */


static void
our_setup_send_wr(struct our_control *conn, struct ibv_sge *sg_list,
		enum ibv_wr_opcode opcode, int n_sges,
		struct ibv_send_wr *send_work_request)
{
	/* set the user's identification to be our conn pointer */
	send_work_request->wr_id = (uint64_t)(unsigned long)conn;

	/* not chaining this work request to other work requests */
	send_work_request->next = NULL;

	/* point at array of scatter-gather elements for this send */
	send_work_request->sg_list = sg_list;

	/* number of scatter-gather elements in array actually being used */
	send_work_request->num_sge = n_sges;

	/* the type of send */
	send_work_request->opcode = opcode;

	/* set SIGNALED flag so every send generates a completion
	 * (if we don't do this, then the send queue fills up!)
	 */
	send_work_request->send_flags = IBV_SEND_SIGNALED;

	/* not sending any immediate data */
	send_work_request->imm_data = 0;
}	/* our_setup_send_wr */


static void
our_setup_recv_wr(struct our_control *conn, struct ibv_sge *sg_list,
		int n_sges, struct ibv_recv_wr *recv_work_request)
{
	/* set the user's identification to be our conn pointer */
	recv_work_request->wr_id = (uint64_t)(unsigned long)conn;

	/* not chaining this work request to other work requests */
	recv_work_request->next = NULL;

	/* point at array of scatter-gather elements for this recv */
	recv_work_request->sg_list = sg_list;

	/* number of scatter-gather elements in array actually being used */
	recv_work_request->num_sge = n_sges;

}	/* our_setup_recv_wr */


/* deals with "n_user_bufs" user_data buffers
 * for each buffer
 *	allocates space for user_data and registers it
 *	then fills in user_data_mr, user_data_sge
 *
 * when called MUST have n_user_bufs <= OUR_MAX_USER_DATA_BUFFERS
 *
 * returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
static int
our_setup_user_data(struct our_control *conn, int n_user_bufs, int access[],
		  struct our_options *options)
{
	int	ret, i;


	conn->n_user_data_bufs = 0;
	for (i = 0; i < n_user_bufs; i++) {

		/* allocate space to hold user data, plus 1 for '\0' */
		conn->user_data[i] = our_calloc(options->data_size + 1,
							options->message);
		if (conn->user_data[i] == NULL) {
			ret = ENOMEM;
			goto out1;
		}

		/* register each user_data buffer for appropriate access */
		errno = 0;
		conn->user_data_mr[i] = ibv_reg_mr(conn->protection_domain,
						   conn->user_data[i],
						   options->data_size,
						   access[i]);
		if (conn->user_data_mr[i] == NULL) {
			ret = ENOMEM;
			our_report_error(ret, "ibv_reg_mr user_data", options);
			free(conn->user_data[i]);
			goto out1;
		}

		/* keep count of number of buffers allocated and registered */
		conn->n_user_data_bufs++;

		/* fill in fields of user data's scatter-gather element */
		our_setup_sge(conn->user_data[i], options->data_size,
			conn->user_data_mr[i]->lkey, &conn->user_data_sge[i]);
	}
	/* all user_data buffers set up ok */
	ret = 0;
	goto out0;
out1:
	our_unsetup_buffers(conn, options);
out0:
	return ret;
}	/* our_setup_user_data */


/* for this demo, the client defines:
 *	2 user_data buffers (to hold the original and the played-back data
 *			client will send() from first, recv() into second)
 *
 * returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
*/
int
our_setup_client_buffers(struct our_control *conn, struct our_options *options)
{
	int	ret;
	int	access[2] = {0, IBV_ACCESS_LOCAL_WRITE};

	/* client needs 2 buffers, first to send(), second to recv() */
	if ((ret = our_setup_user_data(conn, 2, access, options)) != 0) {
		goto out0;
	}

	/* client needs 2 work requests, first to send(), second to recv() */
	our_setup_send_wr(conn, &conn->user_data_sge[0], IBV_WR_SEND, 1,
					&conn->user_data_send_work_request[0]);
	our_setup_recv_wr(conn, &conn->user_data_sge[1], 1,
					&conn->user_data_recv_work_request[0]);

	/* print out our buffer allocation numbers */
	our_print_buffer_numbers(conn, "our_setup_client_buffers", options);

out0:
	return ret;
}	/* our_setup_client_buffers */

void
rsocket_setup_sockopt(struct our_control *conn, struct our_options *options)
{
int val;

	if (buffer_size) {
		rsetsockopt(conn->cm_id->channel->fd, SOL_SOCKET, SO_SNDBUF, (void *) &buffer_size,
			      sizeof buffer_size);
		rsetsockopt(conn->cm_id->channel->fd, SOL_SOCKET, SO_RCVBUF, (void *) &buffer_size,
			      sizeof buffer_size);
	} else {
		val = 1 << 19;
		rsetsockopt(conn->cm_id->channel->fd, SOL_SOCKET, SO_SNDBUF, (void *) &val, sizeof val);
		rsetsockopt(conn->cm_id->channel->fd, SOL_SOCKET, SO_RCVBUF, (void *) &val, sizeof val);
	}

	val = 1;
	rsetsockopt(conn->cm_id->channel->fd, IPPROTO_TCP, TCP_NODELAY, (void *) &val, sizeof(val));

// rfcntl 不同于fcntl
// 将cm_id -> recv_cq_channel ，server 的rs->accept_queue[0]或client 的cm_id->channel->fd 都调用fcntl 设为nonblock
	rfcntl(conn->cm_id->channel->fd, F_SETFL, O_NONBLOCK);

//  还可以设置inline size 和 keepalive
	rsetsockopt(conn->cm_id->channel->fd, SOL_RDMA, RDMA_INLINE, &inline_size,
				      sizeof inline_size);
		// /* Inline size based on experimental data */
		// if (optimization == opt_latency) {
		// 	rs_setsockopt(fd, SOL_RDMA, RDMA_INLINE, &inline_size,
		// 		      sizeof inline_size);
		// } else if (optimization == opt_bandwidth) {
		// 	val = 0;
		// 	rs_setsockopt(fd, SOL_RDMA, RDMA_INLINE, &val, sizeof val);
		// }
	

	// if (keepalive)
	// 	set_keepalive(fd);
}	/* rsocket_setup_sockopt

/* for this demo, the agent defines:
 *	1 user_data buffer (to recv() the original data and then send() it back)
 *
 * returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
*/
int
our_setup_agent_buffers(struct our_control *conn, struct our_options *options)
{
	int	ret;
	int	access[1] = {IBV_ACCESS_LOCAL_WRITE};

	/* agent needs 1 buffer for both recv() and send() */
	if ((ret = our_setup_user_data(conn, 1, access, options)) != 0) {
		goto out0;
	}

	/* fill in fields of user_data's work requests for agent's data buffer
	 * First request is to recv() into it from the client
	 * Second request is to send() out of it back to the client
	 */
	our_setup_recv_wr(conn, &conn->user_data_sge[0], 1,
					&conn->user_data_recv_work_request[0]);
	our_setup_send_wr(conn, &conn->user_data_sge[0], IBV_WR_SEND, 1,
					&conn->user_data_send_work_request[0]);

	/* print out our buffer allocation numbers */
	our_print_buffer_numbers(conn, "our_setup_agent_buffers", options);

out0:
	return ret;
}	/* our_setup_agent_buffers */


/* undoes the successful allocations and registrations of all buffers */
void
our_unsetup_buffers(struct our_control *conn, struct our_options *options)
{	int	ret, i;

	for (i = 0; i < conn->n_user_data_bufs; i++) {
		errno = 0;
		ret = ibv_dereg_mr(conn->user_data_mr[i]);
		if (ret != 0) {
			our_report_error(ret, "ibv_dereg_mr user_data_mr",
								options);
		}
		free(conn->user_data[i]);
	}
}	/* our_unsetup_buffers */
