/* agent.c - client uses both rdma_write and rdma_read to ping-pong user data */
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
 *	post a recv for remote buffer info
 * The agent does the following once:
 *	post a recv for only ACK from client
 *	wait for outstanding recv (remote buffer info) to complete
 *	use received remote buffer info to fill in rdma_write work request
 *	post a send for local buffer info to client
 *	wait for outstanding send (local buffer info) to complete
 * The agent does nothing in a loop -- everything is driven by the client
 * The agent does the following once:
 *	wait for outstanding recv (ACK) to complete
 *	post a send ACK
 *	wait for outstanding send (ACK) to complete
 *
 * The agent cannot count the number of transactions, because there is no loop,
 * so it can only calculate a rate based on the number sent in the only ACK
 * from the client
 *
 * returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
int
our_agent_operation(struct our_control *agent_conn, struct our_options *options)
{
	struct ibv_wc	*work_completion;
	unsigned long	expected_size;
	int		ret;

	agent_conn->wc_rdma_both = 0;

	/* post a receive to catch the remote client's only ACK */
	ret = our_post_recv(agent_conn,
			&agent_conn->recv_ack_work_request, options);
	if (ret != 0) {
		goto out0;
	}

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

	/* in this demo the agent's data work requests are never setup or used,
	 * so we don't have to use anything out of the client's meta_data
	 */

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

	/* mark the time we start waiting for client to finish */
	our_get_current_time(&agent_conn->start_time);
	our_get_current_usage(&agent_conn->start_usage);

	/* wait for client's only recv ACK to complete */
	if (options->flags & VERBOSE_TRACING)
		our_report_string("waiting completion of", "recv ACK",
					"event IBV_WC_RECV", options);

	ret = our_await_completion(agent_conn,&work_completion,options);
	if (ret != 0) {
		/* hit error or FLUSH_ERR, in either case leave now */
		goto out1;
	}

	/* dig the number of iterations the client finished out of the ACK */
	agent_conn->wc_rdma_both = ntohl(agent_conn->recv_ack.ack_count);

	/* now we send an ACK to the remote client */
	ret = our_post_send(agent_conn,
			&agent_conn->send_ack_work_request, options);
	if (ret != 0) {
		goto out1;
	}

	/* wait for our only send ACK to complete */
	if (options->flags & VERBOSE_TRACING)
		our_report_string("waiting completion of", "send ACK",
					"event IBV_WC_SEND", options);

	ret = our_await_completion(agent_conn,&work_completion,options);
	if (ret != 0) {
		goto out1;
	}
out1:
	our_print_statistics(agent_conn, options);
out0:
	return ret;
}	/* our_agent_operation */
