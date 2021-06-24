/* bind_listener.c */
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


/*
 * returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
int
our_listener_bind(struct our_control *listen_conn, struct our_options *options)
{
	struct addrinfo		*aptr, hints;
	int			ret;

	/* get structure for remote host node on which server resides */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	hints.ai_flags = AI_PASSIVE;	/* this makes it a server */
	ret = getaddrinfo(options->server_name, options->server_port,
								&hints, &aptr);
	if (ret != 0) {
		fprintf(stderr, "%s: getaddrinfo server_name %s port %s %s\n",
			options->message, options->server_name,
			options->server_port, gai_strerror(ret));
		goto out0;
	}

	errno = 0;
	ret = rdma_bind_addr(listen_conn->cm_id,
				(struct sockaddr *)aptr->ai_addr);
	if (ret != 0) {
		our_report_error(ret, "rdma_bind_addr", options);
		goto out1;
	}

	our_trace_ok("rdma_bind_addr", options);

	our_trace_ptr("rdma_bind_addr", "returned cm_id -> channel",
			listen_conn->cm_id->channel, options);

	errno = 0;
	ret = rdma_listen(listen_conn->cm_id, OUR_BACKLOG);
	if (ret != 0) {
		our_report_error(ret, "rdma_listen", options);
		goto out1;
	}

	our_trace_ok("rdma_listen", options);

	our_trace_ptr("rdma_listen", "returned cm_id -> channel",
			listen_conn->cm_id->channel, options);

	/* everything worked ok, fall thru, because ret == 0 already */

	/* cleanup on error returns */
out1:
	freeaddrinfo(aptr);
out0:
	return ret;
}	/* our_listener_bind */
