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
	struct ibv_wc	*work_completion;
	uint64_t	count;
	int		ret, first;

	first = 1;
	agent_conn->wc_recv = agent_conn->wc_send = 0;
	for (count = 0; ; count++) {
		/* wait for next work completion */
		ret = our_await_completion(agent_conn, &work_completion,
								options);
		if (ret != 0) {
			/* hit error or FLUSH_ERR, in either case leave now */
			goto out0;
		}

		if (first) {
			/* mark the time we start receiving from client */
			first = 0;
			our_get_current_time(&agent_conn->start_time);
			our_get_current_usage(&agent_conn->start_usage);
		}
		/* see whether it was the send or recv that just completed */
		switch (work_completion->opcode) {
		case IBV_WC_RECV:
			agent_conn->wc_recv++;
			if (work_completion->byte_len != options->data_size) {
				fprintf(stderr,
					"%s: received %d bytes, expected %lu\n",
					options->message,
					work_completion->byte_len,
					options->data_size);
			}

			if (options->flags & VERBOSE_TRACING) {
				fprintf(stderr, "%s: %lu recv completed ok\n",
							options->message,count);
			}

			/* echo back the data we just received */

			/* repost our receive to catch the client's next send */
			ret = our_post_recv(agent_conn,
				&agent_conn->user_data_recv_work_request[0],
				options);
			if (ret != 0) {
				goto out0;
			}

			/* post a send to reply back to the client */
			ret = our_post_send(agent_conn,
				&agent_conn->user_data_send_work_request[0],
				options);
			if (ret != 0) {
				goto out0;
			}
			break;

		case IBV_WC_SEND:
			agent_conn->wc_send++;
			if (options->flags & VERBOSE_TRACING) {
				fprintf(stderr, "%s: %lu send completed ok\n",
							options->message,count);
			}
			if (agent_conn->wc_send >= options->limit) {
				ret = 0;
				goto out0;
			}
			break;

		default:
			our_report_ulong("ibv_poll_cq",
					"bad work completion opcode",
					work_completion->opcode, options);
			ret = -1;
			goto out0;
		}	/* switch */
	}	/* for */

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
