/* agent.c - using only rdma_write to ping-pong user data */
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

#include "prototypes.h"


/* when called, caller has already done (prior to the accept):
 *	ping buffer has been initialized to all zeros
 *	post a recv for remote buffer info
 * The agent does the following once:
 *	wait for outstanding recv (remote buffer info) to complete
 *	use received remote buffer info to fill in rdma_write work request
 *	post a send for local buffer info to client
 *	wait for outstanding send (local buffer info) to complete
 * The agent performs the following loop "limit" times:
 *	spin testing for ping buffer to become all non-zeros
 *	copy ping buffer to pong buffer
 *	reset ping buffer to all zeros
 *	post an RDMA_WRITE send to send pong data back to client
 *	wait for outstanding send (RDMA_WRITE) to complete
 *
 * The agent breaks out of the loop on any error, including the FLUSH_ERR,
 * which means the client has disconnected.
 *
 * returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
int
our_agent_operation(struct our_control *agent_conn, struct our_options *options)
{
	struct ibv_wc	*work_completion;
	uint64_t	count;
	uint64_t	max_spin_count;
	unsigned long	expected_size;
	int		ret;

	/* keep track of maximum number of "spins" in busy wait loop */
	max_spin_count = 0;

	/* this should count up to options->limit */
	agent_conn->wc_rdma_write = 0;

	/* number of bytes of remote buffer info we are expecting */
	expected_size = agent_conn->remote_buffer_info_work_request.num_sge
			* sizeof(struct our_buffer_info);

	/* wait for the recv remote buffer info to complete */
	if (options->flags & VERBOSE_TRACING)
		our_report_string("waiting completion of",
			"recv remote buffer info", "event IBV_WC_RECV",options);

	ret = our_await_completion(agent_conn, &work_completion, options);
	if (ret != 0) {
		goto out0;
	}

	if (work_completion->byte_len != expected_size) {
		fprintf(stderr, "%s: received %d bytes, "
			"expected %lu\n", options->message,
			work_completion->byte_len, expected_size);
		ret = -1;
		goto out0;
	}

	/* use remote client's buffer info to fill in
	 * rdma part of our RDMA_WRITE work request
	 */
	agent_conn->user_data_send_work_request[0].wr.rdma.remote_addr
			= (agent_conn->remote_buffer_info[0].addr);
			// = ntohll(agent_conn->remote_buffer_info[0].addr);
	agent_conn->user_data_send_work_request[0].wr.rdma.rkey
			= (agent_conn->remote_buffer_info[0].rkey);
			// = ntohl(agent_conn->remote_buffer_info[0].rkey);

	/* now we send our local buffer info to the remote client */
	ret = our_post_send(agent_conn,
			&agent_conn->local_buffer_info_work_request, options);
	if (ret != 0) {
		goto out0;
	}

	/* wait for the send local buffer info to complete */
	if (options->flags & VERBOSE_TRACING)
		our_report_string("waiting completion of",
			"send local buffer info", "event IBV_WC_SEND", options);

	ret = our_await_completion(agent_conn, &work_completion, options);
	if (ret != 0) {
		goto out0;
	}

	/* mark the time we start waiting for user data from client */
	our_get_current_time(&agent_conn->start_time);
	our_get_current_usage(&agent_conn->start_usage);

	for (count = 0; count < options->limit; count++) {

		/* busy wait until our local buffer gets full ping pattern
		 * (i.e., until all bytes in the buffer become non-zero)
		 */
		ret = our_all_non_zero_completion_match(agent_conn,
			agent_conn->user_data[0], options->data_size,
			&max_spin_count, options);
		if (ret != 0) {
			/* remote side disconnected, stop everything */
			goto out1;
		}

		/* copy ping buffer into pong buffer, then clear ping buffer */
		memcpy(agent_conn->user_data[1], agent_conn->user_data[0],
							options->data_size);
		memset(agent_conn->user_data[0], 0, options->data_size);

		/* now we send our RDMA_WRITE to the remote client */
		ret = our_post_send(agent_conn,
				&agent_conn->user_data_send_work_request[0],
				options);
		if (ret != 0) {
			goto out1;
		}

		/* wait for the send RDMA_WRITE to complete */
		if (options->flags & VERBOSE_TRACING)
			our_report_string("waiting completion of",
			"send RDMA_WRITE", "event IBV_WC_RDMA_WRITE", options);

		ret = our_await_completion(agent_conn,&work_completion,options);
		if (ret != 0) {
			goto out1;
		}
		agent_conn->wc_rdma_write++;
	}	/* for */
out1:
	our_print_statistics(agent_conn, options);
	our_report_uint64("busy wait loop", "max_spin_count", max_spin_count,
								options);
out0:
	return ret;
}	/* our_agent_operation */
