/* client.c -- using send/recv */
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


/* The client performs the following loop "limit" times:
 *	post a receive
 *	send data to the server
 *	wait for the outstanding send to complete
 *	wait for the outstanding receive to complete
 *
 * returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
static int
our_client_operation(struct our_control *client_conn,
		     struct our_options *options)
{
	struct ibv_wc				*work_completion;
	struct ibv_send_wr			*work_request;
	struct ibv_send_wr			*send_list, *last_in_list;
	struct our_send_work_request_list	data_list;
	unsigned char				*ptr;
	uint64_t				i, ack_count, credit_count;
	uint64_t				n_replies, n_acked_buffers;
	int					ret, index;
	unsigned int				list_size;

	
	/* counts total number of data buffers sent */
	n_replies = 0;

	/* counts total number of data buffers acked */
	n_acked_buffers = 0;

	/* gets number of sends acked by remote agent in one recv */
	ack_count = 0;

	/* number of recvs remote agent currently has posted for client to use*/
	credit_count = options->n_data_buffers;

	/* put all user_data_send_work_requests onto local list */
	data_list.size = 0;
	for (index = 0; index < options->n_data_buffers; index++) {
		our_add_to_work_request_list(&data_list,
			&client_conn->user_data_send_work_request[index], options);
	}

	/* wc_recv counts total number of send work requests posted to agent
	 * wc_send counts total number of send work requests to agent completed
	 * at the end these two values should be equal
	 */
	client_conn->wc_recv = 0;
	client_conn->wc_send = 0;

	/* fill in the client's data buffers with a meaningful pattern */
	ret = ' ';
	for (index = 0; index < options->n_data_buffers; index++) {
		ptr = client_conn->user_data[index];
		for (i = 0; i < options->data_size; ) {
			if (isprint(ret)) {
				*ptr++ = ret++;
				i++;
			} else {
				ret = ' ';
			}
		}
	}
	if (options->flags & VERBOSE_TRACING) {
		if (options->data_size <= 200) {
			fprintf(stderr, "%s: user_data[0] (%lu bytes): %s\n",
					options->message, options->data_size,
					client_conn->user_data[0]);
		}
	}
	
	/* post our receives to catch the server's acks */
	for (index = 0; index < OUR_MAX_ACK_MESSAGES; index++) {
		ret = our_post_recv(client_conn,
			&client_conn->recv_ack_work_request[index], options);
		if (ret != 0)
			goto out0;
	}

	/* mark the time we start sending to server */
	our_get_current_time(&client_conn->start_time);
	our_get_current_usage(&client_conn->start_usage);

	do	{
		if (options->flags & VERBOSE_TRACING) {
			our_trace_ulong("our_post_send", "work_request_list size",
					data_list.size, options);
			our_trace_ulong("our_post_send", "credit_count",
					credit_count, options);
		}
		if (data_list.size > 0 && credit_count > 0){
			/* we have something to send, and we have credits */
			send_list = data_list.head;	/* start sending here */
			if (data_list.size <= credit_count) {
				/* send entire work request list to agent */
				list_size = data_list.size;
			} else {
				/* send only 1st part of request list to agent*/
				list_size = credit_count;
				last_in_list = send_list;
				for (i = 1; i < list_size; i++) {
					last_in_list = last_in_list->next;
				}
				/* cut the data_list here */
				data_list.head = last_in_list->next;
				last_in_list->next = NULL;
			}
			data_list.size -= list_size;
			credit_count -= list_size;
				
			if (options->flags & VERBOSE_TRACING) {
				our_report_ulong("our_post_send",
					"send list size",
					list_size, options);
				our_report_ulong("our_post_send",
					"credit_count",
					credit_count, options);
			}
			ret = our_post_send(client_conn, send_list, options);
			if (ret != 0)
				goto out1;

			/* that work request list was posted ok, count it */
			client_conn->wc_recv += list_size;

			/* keep distribution of work_request chain sizes */
			if (list_size > OUR_MAX_WORK_REQUESTS) {
				our_report_ulong("send list size",
					"too big", list_size, options);
			} else {
				client_conn->send_stats++;
				client_conn->wr_chain_stats[list_size]++;
			}
		}

		/* wait for next work completion */
		ret = our_fill_completion_array(client_conn, options);
		if (ret != 0) {
			/* hit error or cm event, in either case leave now */
			goto out1;
		}

		/* busy wait until something completes */
		if (client_conn->current_n_work_completions == 0)
			continue;

		/* we now have at least 1 unprocessed work completion in array
		 * form a list of all work requests from send work completions
		 */
		while (client_conn->index_work_completions
				< client_conn->current_n_work_completions) {
			work_completion = &client_conn->work_completion
					[client_conn->index_work_completions++];
			ret = our_check_completion_status(client_conn,
					work_completion, options);
			if (ret != 0)
				goto out1;

			/* was it a send or recv that just completed? */
			switch (work_completion->opcode) {
			case IBV_WC_RECV:
				/* just completed receiving an ack from agent */
				n_replies++;
				if (options->flags & VERBOSE_TRACING) {
					fprintf(stderr,
						"%s: %lu recv completed ok\n",
						options->message, n_replies);
				}
				if (work_completion->byte_len
					!= sizeof(union our_ack_info)) {
					fprintf(stderr,"%s: received %d bytes, "
						"expected %lu\n",
						options->message,
						work_completion->byte_len,
						sizeof(union our_ack_info));
					goto out1;
				}
				index = work_completion->wr_id;
				ack_count = ntohll(client_conn->
						recv_ack[index].ack_count);
				credit_count += ack_count;
				n_acked_buffers += ack_count;
				/* post wr to recv another ack */
				ret = our_post_recv(client_conn,
					&client_conn->recv_ack_work_request[index],
					options);
				if (ret != 0)
					goto out1;
				our_trace_uint64("our_post_recv", "ack_count",
							ack_count, options);
				break;

			case IBV_WC_SEND:
				/* just completed sending 1 data buffer to agent */
				client_conn->wc_send++;
				if (options->flags & VERBOSE_TRACING) {
					fprintf(stderr,
						"%s: %lu send completed ok\n",
						options->message,
						client_conn->wc_send);
				}
				/* see if we need to post this send again */
				if ((client_conn->wc_recv + data_list.size)
							< options->limit) {
					index = work_completion->wr_id;
					work_request = &client_conn->
						user_data_send_work_request[index];
					our_add_to_work_request_list( &data_list,
							work_request, options);
				}
				break;

			default:
				our_report_ulong("","bad work completion opcode",
				work_completion->opcode, options);
				ret = -1;
				goto out1;
			}	/* switch */
		}	/* while */
	} while(client_conn->wc_recv < options->limit
				|| n_acked_buffers < options->limit);

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
	if (options == NULL)
		goto out0;

	/* allocate our own control structure to keep track of new connection */
	client_conn = our_create_control_struct(options);
	if (client_conn == NULL)
		goto out1;

	if (our_create_id(client_conn, options) != 0)
		goto out2;

	if (our_create_cm_event_thread(client_conn, options) != 0)
		goto out2a;

	if (our_client_bind(client_conn, options) != 0)
		goto out3;

	if (our_setup_qp(client_conn, client_conn->cm_id, options) != 0)
		goto out3;

	if (our_setup_client_buffers(client_conn, options) != 0)
		goto out4;

	if (our_client_connect(client_conn, options) != 0)
		goto out5;

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
