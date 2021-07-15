/* bind_client.c */
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
our_client_bind(struct our_control *client_conn, struct our_options *options)
{
	struct addrinfo		*aptr, hints;
	int			ret;

	/* get structure for remote host node on which server resides */
	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	ret = getaddrinfo(options->server_name, options->server_port,
								&hints, &aptr);
	if (ret != 0) {
		fprintf(stderr, "%s getaddrinfo server_name %s port %s: %s\n",
			options->message, options->server_name,
			options->server_port, gai_strerror(ret));
		goto out0;
	}

	errno = 0;
	ret = rdma_resolve_addr(client_conn->cm_id,
				NULL,
				(struct sockaddr *)aptr->ai_addr,
				2000);
	if (ret != 0) {
		our_report_error(ret, "rdma_resolve_addr", options);
		goto out1;
	}

	/* in this demo, rdma_resolve_addr() operates asynchronously */
	ret = our_await_cm_event(client_conn, RDMA_CM_EVENT_ADDR_RESOLVED,
				"rdma_resolve_addr", NULL, NULL, options);
	if (ret != 0)
		goto out1;

	errno = 0;
	ret = rdma_resolve_route(client_conn->cm_id, 2000);
	if (ret != 0) {
		our_report_error(ret, "rdma_resolve_route", options);
		goto out1;
	}

	/* in this demo, rdma_resolve_route() operates asynchronously */
	ret = our_await_cm_event(client_conn, RDMA_CM_EVENT_ROUTE_RESOLVED,
				"rdma_resolve_route", NULL, NULL, options);
	if (ret != 0)
		goto out1;

	/* everything worked ok, fall thru, because ret == 0 already */

out1:
	freeaddrinfo(aptr);
out0:
	return ret;
}	/* our_client_bind */

int rsocket_client_bind(struct our_control *client_conn, struct our_options *options){
	struct addrinfo *ai = NULL, *ai_src = NULL;
		struct addrinfo		*aptr, hints;
		int ret, err;
	ret = getaddrinfo(options->server_name, options->server_port,
								&hints, &aptr);
	if (ret) {
		printf("getaddrinfo: %s\n", gai_strerror(ret));
		goto out0;
	}
	options->ai = aptr;


out1:
	freeaddrinfo(aptr);
out0:
	return ret;
}
