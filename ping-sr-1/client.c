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
	struct ibv_wc	work_completion;
	unsigned char	*ptr;
	uint64_t	i;
	int		ret;

	
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
	
	client_conn->wc_recv = client_conn->wc_send = 0;

	/* post our first receive to catch the server's first reply */
	ret = our_post_recv(client_conn,
			&client_conn->user_data_recv_work_request[0], options);
	if (ret != 0) {
		goto out0;
	}

	/* mark the time we start sending to server */
	our_get_current_time(&client_conn->start_time);
	our_get_current_usage(&client_conn->start_usage);

	/* post our first send to send our data to the server */
	ret = our_post_send(client_conn,
			&client_conn->user_data_send_work_request[0], options);
	if (ret != 0) {
		goto out1;
	}

	for ( ; ; ) {
		/* wait for next work completion */
		ret = our_await_completion(client_conn,
						&work_completion, options);
		if (ret != 0) {
			/* hit error or FLUSH_ERR, in either case leave now */
			goto out1;
		}

		/* see whether it was the send or recv that just completed */
		switch (work_completion.opcode) {
		case IBV_WC_RECV:
			client_conn->wc_recv++;
			if (work_completion.byte_len != options->data_size) {
				fprintf(stderr, "%s: received %d bytes, "
					"expected %lu\n", options->message,
					work_completion.byte_len,
					options->data_size);
			}

			if (options->flags & VERBOSE_TRACING) {
				fprintf(stderr, "%s: %lu recv completed ok\n",
					options->message, client_conn->wc_recv);
			}

			if (options->flags & VERIFY) {
				if (memcmp(client_conn->user_data[0],
						client_conn->user_data[1],
						options->data_size) != 0) {
					fprintf(stderr,"%s: %lu ping data %s\n",
						options->message,
						client_conn->wc_recv,
						client_conn->user_data[0]);
					fprintf(stderr,"%s: %lu pong data %s\n",
						options->message,
						client_conn->wc_recv,
						client_conn->user_data[1]);
					fprintf(stderr, "%s: %lu verification "
						"failed\n", options->message,
						client_conn->wc_recv);
				} else {
					if (options->flags & VERBOSE_TRACING) { 
						fprintf(stderr, "%s: %lu "
							"verification ok\n",
							options->message,
							client_conn->wc_recv);
					}
					memset(client_conn->user_data[1], 0,
						options->data_size);
				}
			}

			/* don't send any more data if we reached our limit */
			if (client_conn->wc_recv >= options->limit) {
				ret = 0;
				goto out1;
			}

			/* post next receive to catch server's next reply */
			ret = our_post_recv(client_conn,
				&client_conn->user_data_recv_work_request[0],
				options);
			if (ret != 0) {
				goto out1;
			}

			/* post next send to resend our data to the server */
			ret = our_post_send(client_conn,
				&client_conn->user_data_send_work_request[0],
				options);
			if (ret != 0) {
				goto out1;
			}

			break;

		case IBV_WC_SEND:
			client_conn->wc_send++;
			if (options->flags & VERBOSE_TRACING) {
				fprintf(stderr, "%s: %lu send completed ok\n",
					options->message, client_conn->wc_recv);
			}
			break;

		default:
			our_report_ulong("", "bad work completion opcode",
				work_completion.opcode, options);
			ret = -1;
			goto out1;
		}	/* switch */
	}	/* for */

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
	if (options == NULL)
		goto out0;

	/* allocate our own control structure to keep track of new connection */
	client_conn = our_create_control_struct(options);
	if (client_conn == NULL)
		goto out1;

	if (our_create_id(client_conn, options) != 0)
		goto out2;

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

	/* the client now ping-pongs data with the server */
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
	our_destroy_id(client_conn, options);
out2:
	our_destroy_control_struct(client_conn, options);
out1:
	our_unprocess_options(options);
out0:
	exit(result);
}	/* main */
