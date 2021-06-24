/* client.c - client uses rdma_write to blast user data without waiting for acks*/
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


static void
print_ibv_post_send_stats(struct our_control *conn, struct our_options *options)
{
	int		i;
	char		buffer[64];
	unsigned long	total;
	unsigned long	weighted_total;

	our_report_ulong("ibv_post_send", "calls", conn->send_stats, options);

	/* find last non-zero histogram bin */
	for (i = OUR_MAX_WORK_REQUESTS; i >= 0; i--) {
		if (conn->wr_chain_stats[i] > 0) {
			break;
		}
	}
	total = 0;
	weighted_total = 0;
	for (; i >= 0; i--) {
		snprintf(buffer, 64, "%s[%d]", "work_request_list_size", i);
		our_report_ulong(buffer, "occurrences",
			conn->wr_chain_stats[i], options);
		total += conn->wr_chain_stats[i];
		weighted_total += conn->wr_chain_stats[i] * i;
	}

	our_report_ulong("total", "occurrences", total, options);
	our_report_ulong("total", "work requests", weighted_total, options);
}	/* print_ibv_post_send_stats */


/* The client does the following once
 *	post a recv for remote buffer info from agent
 *	post a send for local buffer info to agent
 *	wait for outstanding send (local buffer info) to complete
 *	wait for outstanding recv (remote buffer info) to complete
 *	use received remote buffer info to fill in rdma_write work requests
 *	
 * The client then performs the following loop "limit" times:
 *	get link list of WRs ready to send
 *	post an RDMA_WRITE to send (blast) linked WRs to the agent
 *	wait for outstanding send (RDMA_WRITE) to complete
 *
 * After the loop finishes, client does the following once
 *	post a recv for only ACK from agent
 *	post a send ACK
 *	wait for outstanding send (ACK) to complete
 *	wait for outstanding recv (ACK) to complete
 *
 * returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
static int
our_client_operation(struct our_control *client_conn,
		     struct our_options *options)
{
	struct ibv_wc	*work_completion;
	unsigned char	*ptr;
	unsigned int	i;
	unsigned long	expected_size;
	int		ret, first;

	
	/* fill in the client's data with a meaningful pattern */
	ptr = client_conn->user_data[0];
	ret = ' ';
	for (i = 0; i < options->data_size; ) {
		if (isprint(ret)) {
			*ptr++ = ret++;
			i++;
		} else {
			ret = ' ';
		}
	}
	if (options->flags & VERBOSE_TRACING) {
		if (options->data_size <= 200) {
			fprintf(stderr, "%s: user_data[0] (%lu bytes): %s\n",
					options->message, options->data_size,
					client_conn->user_data[0]);
		}
	}
	
	/* this should count up to options->limit */
	client_conn->wc_rdma_write = 0;

	/* number of bytes of remote buffer info we are expecting */
	expected_size = client_conn->n_user_data_bufs
					* sizeof(struct our_buffer_info);

	/* post a receive to catch the remote agent's buffer info */
	ret = our_post_recv(client_conn,
			&client_conn->remote_buffer_info_work_request, options);
	if (ret != 0) {
		goto out0;
	}

	/* now we send our local buffer info to the remote agent */
	ret = our_post_send(client_conn,
			&client_conn->local_buffer_info_work_request, options);
	if (ret != 0) {
		goto out0;
	}

	/* wait for the send local buffer info to complete */
	if (options->flags & VERBOSE_TRACING)
		our_report_string("waiting completion of",
			"send local buffer info", "event IBV_WC_SEND", options);

	ret = our_await_completion(client_conn, &work_completion, options);
	if (ret != 0) {
		goto out0;
	}

	/* wait for the recv remote buffer info to complete */
	if (options->flags & VERBOSE_TRACING)
		our_report_string("waiting completion of",
			"recv remote buffer info", "event IBV_WC_RECV",options);

	ret = our_await_completion(client_conn, &work_completion, options);
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

	/* use remote agent's buffer info to fill in
	 * rdma part of our RDMA_WRITE requests
	 * to both point to the remote agent's buffers
	 */
	for (i = 0; i < options->n_data_buffers; i++) {
		client_conn->user_data_send_work_request[i].wr.rdma.remote_addr
			= ntohll(client_conn->remote_buffer_info[i].addr);
		client_conn->user_data_send_work_request[i].wr.rdma.rkey
			= ntohl(client_conn->remote_buffer_info[i].rkey);
		if (options->flags & VERBOSE_TRACING) {
			fprintf(stderr, "%s: %s[%d] %s %p\n", options->message,
				"remote_buffer_info", i, "addr",
				(void *)client_conn->
				user_data_send_work_request[i].wr.rdma.remote_addr);
			fprintf(stderr, "%s: %s[%d] %s %u\n", options->message,
				"remote_buffer_info", i, "rkey",
				client_conn->
				user_data_send_work_request[i].wr.rdma.rkey);
		}
	}

	/* mark the time we start sending to server */
	our_get_current_time(&client_conn->start_time);
	our_get_current_usage(&client_conn->start_usage);

	/* the first time thru, work_request_list contains ALL work requests */
	first = 1;
	for (client_conn->n_posted_buffers = 0;
	     client_conn->n_posted_buffers < options->limit;
	     client_conn->n_posted_buffers+=client_conn->work_request_list_size) {
		if (first == 0) {
			/* reload the work_request_list with work completions */
			ret = our_get_work_request_list(client_conn, options);
			if (ret != 0)
				goto out1;
		} else {
			first = 0;
		}

		/* now we send our RDMA_WRITE list to the remote agent */
		if (options->flags & VERBOSE_TRACING)
			our_report_ulong("our_post_send","work_request_list_size",
				client_conn->work_request_list_size, options);
		ret = our_post_send(client_conn,
				client_conn->work_request_list_head, options);
		if (ret != 0) {
			goto out1;
		}

		/* keep statistics on the effects of work_request chaining */
		if (client_conn->work_request_list_size > OUR_MAX_WORK_REQUESTS){
			our_report_ulong("work_request_list_size", "too big",
					client_conn->work_request_list_size,
					options);
		} else {
			client_conn->send_stats++;
			client_conn->wr_chain_stats
					[client_conn->work_request_list_size]++;
		}
	}	/* for */

	our_trace_uint64("our_post_send", "n_posted_buffers",
		     client_conn->n_posted_buffers, options);

	/* wait for all posted buffers to complete */
	while (client_conn->wc_rdma_write < client_conn->n_posted_buffers) {
		if (client_conn->disconnected != 0)
			goto out1;
		ret = our_get_work_request_list(client_conn, options);
		if (ret != 0)
			goto out1;
	}

	/* post a receive to catch the remote agent's only ACK */
	ret = our_post_recv(client_conn,
				&client_conn->recv_ack_work_request, options);
	if (ret != 0) {
		goto out1;
	}

	/* tell the agent the number of iterations we finished */
	client_conn->send_ack.ack_count = htonl(client_conn->wc_rdma_write);

	/* now we send our only ACK to the remote agent */
	ret = our_post_send(client_conn,
				&client_conn->send_ack_work_request, options);
	if (ret != 0) {
		goto out1;
	}

	/* wait for the send ACK to complete */
	if (options->flags & VERBOSE_TRACING)
		our_report_string("waiting completion of", "send ACK",
						"event IBV_WC_SEND", options);

	ret = our_await_completion(client_conn,&work_completion,options);
	if (ret != 0) {
		goto out1;
	}

	/* wait for agent's only ACK to complete */
	if (options->flags & VERBOSE_TRACING)
		our_report_string("waiting completion of", "recv ACK",
						"event IBV_WC_RECV", options);

	ret = our_await_completion(client_conn,&work_completion,options);
	if (ret != 0) {
		/* hit error or FLUSH_ERR, in either case leave now */
		goto out1;
	}
out1:
	our_print_statistics(client_conn, options);
	print_ibv_post_send_stats(client_conn, options);
out0:
	return ret;
}	/* our_client_operation */


int
main(int argc, char *argv[])
{
	struct our_control	*client_conn;
	struct our_options	*options;
	int			result;

	/* assume there is an error somewhere along the line */
	result = EXIT_FAILURE;

	/* process the command line options -- don't go on if any errors */
	options = our_process_options(argc, argv);
	if (options == NULL) {
		goto out0;
	}

	/* allocate our own control structure to keep track of new connection */
	client_conn = our_create_control_struct(options);
	if (client_conn == NULL) {
		goto out1;
	}

	if (our_create_id(client_conn, options) != 0)
		goto out2;

	/* set up a separate thread to handle cm events */
	if (our_create_cm_event_thread(client_conn, options) != 0)
		goto out2a;

	if (our_client_bind(client_conn, options) != 0) {
		goto out3;
	}

	if (our_setup_qp(client_conn, client_conn->cm_id, options) != 0) {
		goto out3;
	}

	if (our_setup_client_buffers(client_conn, options) != 0) {
		goto out4;
	}

	if (our_client_connect(client_conn, options) != 0) {
		goto out5;
	}

	our_trace_ptr("Client", "connected our_control", client_conn,
								options);

	/* the client now blasts data to the server */
	if (our_client_operation(client_conn, options) != 0) {
		goto out6;
	}

	/* the client finished successfully, continue into tear-down phase */
	result = EXIT_SUCCESS;
out6:
	our_disconnect(client_conn, options);
out5:
	our_unsetup_buffers(client_conn, options);
out4:
	our_unsetup_qp(client_conn, options);
out3:
	our_destroy_cm_event_thread(client_conn, options);
out2a:
	our_destroy_id(client_conn, options);
out2:
	our_destroy_control_struct(client_conn, options);
out1:
	our_unprocess_options(options);
out0:
	exit(result);
}	/* main */
