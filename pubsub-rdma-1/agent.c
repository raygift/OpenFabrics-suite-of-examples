/* agent.c - client uses rdma_read to read user data from agent's memory */
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
 * The agent loop repeatedly does:
 *	sleep for some random amount of time
 *	generate new data into its published buffer
 *	print its latest published data
 * The agent does the following once:
 *	wait for outstanding recv (ACK) to complete
 *	post a send ACK
 *	wait for outstanding send (ACK) to complete
 *
 * The agent calculates a rate based on the number sent in the only ACK
 * from the client
 *
 * returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
int
our_agent_operation(struct our_control *agent_conn, struct our_options *options)
{
	struct ibv_wc	*work_completion;
	unsigned char	*ptr;
	unsigned long	expected_size;
	int		ret;
	uint64_t	count;
	uint64_t	i;
	time_t		timet;
	long int	k;
	struct timespec	delay;

	our_trace_ulong("agent", "RAND_MAX", RAND_MAX, options);

	/* get a "random" seed for the random number generator */
	if (time(&timet) == (time_t)-1) {
		perror("time");
		timet = 123456789;
	}
	srandom(timet);

	/* this should count up to options->limit */
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

	for (count = 0; count < options->limit; count++) {

		/* fill in the agent's data with a meaningful pattern */
		ptr = agent_conn->user_data[0];
		ret = random() & 0x7F;
		for (i = 0; i < options->data_size; ) {
			if (isprint(ret)) {
				*ptr++ = ret++;
				i++;
			} else {
				ret = random() & 0x7F;
			}
		}
		if (options->data_size <= 200) {
			fprintf(stderr,
				"%s: %5lu (%lu bytes): %s\n",
				options->message, count+1, options->data_size,
				agent_conn->user_data[0]);
		}
	
		/* now wait a random amount of time before next publication */
		k = random();
		delay.tv_sec =  (k >> 5) & 0x3;		/* 0-3 secs */
		delay.tv_nsec = k % 100000000;		/* 0-099999999 nsecs */
		if (nanosleep(&delay, NULL) != 0) {
			if (errno != EINTR)
				perror("nanosleep");
		}
		agent_conn->wc_rdma_both++;
	}	/* for */

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
