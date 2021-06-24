/* agent.c */
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


/* when called, the first receive work request has already been posted.
 * The agent performs the following loop:
 *	wait for the outstanding receive to complete
 *	post another receive
 *	post a send of the received data
 *	wait for the outstanding send to complete
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
	struct ibv_wc				*work_completion;
	struct ibv_recv_wr			*data_work_request;
	struct ibv_send_wr			*ack_work_request;
	struct our_send_work_request_list	ack_list;
	uint64_t				ack_count, threshhold, i;
	int					ret, first, index;

	first = 1;	/* reset on first receive (used to start timing) */
	ack_count = 0;	/* current number of un-acked recv's from client */

	/* threshhold for when to send an ack containing value ack_count */
	threshhold = options->n_data_buffers/OUR_MAX_ACK_MESSAGES;
	if (threshhold == 0)
		threshhold = 1;

	/* put all send_ack_work_requests onto local list */
	ack_list.size = 0;
	for (index = 0; index < OUR_MAX_ACK_MESSAGES; index++) {
		our_add_to_work_request_list(&ack_list,
			&agent_conn->send_ack_work_request[index], options);
	}

	/* wc_recv counts total number of completed recv's from remote client
	 * wc_send counts total number of credits sent back to client
	 */
	agent_conn->wc_recv = 0;
	agent_conn->wc_send = 0;

	do	{
		/* wait for next work completion */
		ret = our_fill_completion_array(agent_conn, options);
		if (ret != 0) {
			/* hit error or cm event, in either case leave now */
			goto out0;
		}

		/* busy wait until something completes */
		if (agent_conn->current_n_work_completions == 0)
			continue;

		if (first) {
			/* mark the time we start receiving from client */
			first = 0;
			our_get_current_time(&agent_conn->start_time);
			our_get_current_usage(&agent_conn->start_usage);
		}

		/* we now have at least 1 unprocessed work completion in array*/
		while (agent_conn->index_work_completions
				< agent_conn->current_n_work_completions) {
			work_completion = &agent_conn->work_completion
					[agent_conn->index_work_completions++];
			index = work_completion->wr_id;
			ret = our_check_completion_status(agent_conn,
					work_completion, options);
			if (ret != 0) {
				goto out0;
			}

			/* was it a send or recv that just completed? */
			switch (work_completion->opcode) {
			case IBV_WC_RECV:
				/* just completed receiving data from client */
				agent_conn->wc_recv++;
				ack_count++;
				if (options->flags & VERBOSE_TRACING) {
					fprintf(stderr,
						"%s: %lu recv completed ok, "
						"ack_count %lu, wr_id %d\n",
						options->message,
						agent_conn->wc_recv,
						ack_count, index);
				}
				if (work_completion->byte_len
						!= options->data_size) {
					fprintf(stderr,
					"%s: received %d bytes, expected %lu\n",
					options->message,
					work_completion->byte_len,
					options->data_size);
				}

				if (agent_conn->wc_recv < options->limit) {
					/* expecting more data from client */
					data_work_request = &agent_conn->
					     user_data_recv_work_request[index];
					/* repost recv for client's next send */
					ret = our_post_recv(agent_conn,
						data_work_request, options);
					if (ret != 0) {
						goto out0;
					our_trace_ok("our_post_recv", options);
					}
				} else {/* we have posted expected no. recv's */
					threshhold = 0;
				}
				break;

			case IBV_WC_SEND:
				i = ntohll(agent_conn->send_ack[index].ack_count);
				agent_conn->wc_send += i;
				ack_work_request
				    = &agent_conn->send_ack_work_request[index];
				our_add_to_work_request_list(&ack_list,
						ack_work_request, options);
				if (options->flags & VERBOSE_TRACING) {
					fprintf(stderr,
						"%s: %lu %lu acks sent ok, "
						"wr_id = %d\n",
						options->message,
						agent_conn->wc_send, i, index);
				}
				break;

			default:
				our_report_ulong("ibv_poll_cq",
					"bad work completion opcode",
					work_completion->opcode, options);
				ret = -1;
				goto out0;
			}	/* switch */
		}	/* while */

		/* all current work completions have been processed */
		if (ack_count >= threshhold) {
			/* we have accumulated enough un-acked receives
			 * from client to try to send an ack for them all now
			 */
			ack_work_request
			= our_get_from_work_request_list(&ack_list, options);
			if (ack_work_request != NULL) {
				/* post a send to ack back to the client */
				index = ack_work_request->wr_id;
				agent_conn->send_ack[index].ack_count
							= htonll(ack_count);
				ret = our_post_send(agent_conn,
					&agent_conn->send_ack_work_request[index],
					options);
				if (ret != 0) {
					goto out0;
				}
				our_trace_uint64("our_post_send", "ack_count",
							ack_count, options);
				ack_count = 0;
			}
		}
	} while(agent_conn->wc_send < options->limit);

out0:
	if (first) {
		/* never got the start time due to early error */
		fprintf(stderr,
			"%s: %lu recv completions, %lu send completions\n",
			options->message, agent_conn->wc_recv,
			agent_conn->wc_send);
	} else {
		our_print_statistics(agent_conn, options);
	}

	return ret;
}	/* our_agent_operation */
