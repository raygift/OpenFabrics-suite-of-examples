/* client.c - client periodically uses rdma_read to get latest data from agent */
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


/* The client does the following once
 *	post a recv for remote buffer info from agent
 *	post a send for local buffer info to agent
 *	wait for outstanding send (local buffer info) to complete
 *	wait for outstanding recv (remote buffer info) to complete
 *	use received remote buffer info to fill in rdma_write work request
 *	use received remote buffer info to fill in rdma_read work request
 *	
 * The client then performs the following loop "limit" times:
 *	sleep for 2 seconds
 *	post an RDMA_READ to recv published data from the agent
 *	wait for outstanding send (RDMA_READ) to complete
 *	print the new data if it differs from previously read data
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
	uint64_t	count, repeats;
	unsigned long	expected_size;
	int		ret;
	struct timespec	delay;

	

	/* this should count up to options->limit */
	client_conn->wc_rdma_both = 0;

	/* number of bytes of remote buffer info we are expecting */
	expected_size = client_conn->remote_buffer_info_work_request.num_sge
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
	 * rdma part of our RDMA_READ 
	 * to point to the remote agent's single buffer
	 */
	client_conn->user_data_send_work_request[0].wr.rdma.remote_addr
			= ntohll(client_conn->remote_buffer_info[0].addr);
	client_conn->user_data_send_work_request[0].wr.rdma.rkey
			= ntohl(client_conn->remote_buffer_info[0].rkey);

	/* mark the time we start receiving from server */
	our_get_current_time(&client_conn->start_time);
	our_get_current_usage(&client_conn->start_usage);

	repeats = 0;
	for (count = 0; count < options->limit; count++) {
		/* periodically read the agent's published data */
		delay.tv_sec = 1;
		delay.tv_nsec = 350000000;	/* 0.35 secs */
		if (nanosleep(&delay, NULL) != 0) {
			if (errno != EINTR)
				perror("nanosleep");
		}
		/* now do RDMA_READ to get latest data from remote agent */
		ret = our_post_send(client_conn,
			&client_conn->user_data_send_work_request[0], options);
		if (ret != 0) {
			goto out1;
		}

		/* wait for the RDMA_READ to complete */
		if (options->flags & VERBOSE_TRACING)
			our_report_string("waiting completion of",
			"send RDMA_READ", "event IBV_WC_RDMA_READ", options);

		ret = our_await_completion(client_conn,&work_completion,options);
		if (ret != 0) {
			goto out1;
		}

		/* compare received data with last known published data */
		if (memcmp(client_conn->user_data[1], client_conn->user_data[0],
						options->data_size) != 0) {
			/* received data is new, print previous value */
			if (repeats != 0) {
				fprintf(stderr,
					"%s: %5lu (%lu times): %s\n",
					options->message, count, repeats,
					client_conn->user_data[1]);
			}
			/* then remember new value */
			repeats = 1;
			memcpy(client_conn->user_data[1], client_conn->user_data[0],
						options->data_size);
		} else {
			/* published value is unchanged */
			repeats++;
			if (options->flags & VERBOSE_TRACING) {
				fprintf(stderr,
					"%s: %5lu (%lu sofar): %s\n",
					options->message, count, repeats,
					client_conn->user_data[1]);
			}
		}
		client_conn->wc_rdma_both++;
	}
	/* print the last published value */
	if (repeats != 0) {
		fprintf(stderr, "%s: %5lu (%lu times): %s\n",
			options->message, count, repeats,
			client_conn->user_data[1]);
	}

	/* post a receive to catch the remote agent's only ACK */
	ret = our_post_recv(client_conn,
				&client_conn->recv_ack_work_request, options);
	if (ret != 0) {
		goto out1;
	}

	/* tell the agent the number of iterations we finished */
	client_conn->send_ack.ack_count = htonl(client_conn->wc_rdma_both);

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

	/* the client now publishes data to the server */
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
