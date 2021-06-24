/* c_d_id.c */
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
 * create a communication identifier used to identify which
 * RDMA device a cm event is being reported about
 *
 * in this demo, do not create a communication channel,
 * which means all cm operations will be performed synchronously
 * (and rdma_create_id will create a channel for us)
 *
 * returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
int
our_create_id(struct our_control *conn, struct our_options *options)
{
	int	ret;

	errno = 0;
	ret = rdma_create_id(NULL, &conn->cm_id, conn, RDMA_PS_TCP);
	if (ret != 0) {
		our_report_error(ret, "rdma_create_id", options);
		goto out0;
	}
	our_trace_ptr("rdma_create_id", "created cm_id", conn->cm_id, options);

	/* report new communication channel created for us and its fd */
	our_trace_ptr("rdma_create_id", "returned cm_id->channel",
				conn->cm_id->channel, options);
	our_trace_ulong("rdma_create_id", "assigned fd",
				conn->cm_id->channel->fd, options);
out0:
	return ret;
}	/* our_create_id */


/* called only by a newly created local agent
 * already have a communication identifier,
 * just copy it and set its context to be this new conn
 *
 * returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
int
our_migrate_id(struct our_control *conn, struct rdma_cm_id *new_cm_id,
		struct our_connect_info *connect_info,
		struct our_options *options)
{
	/* replace agent's limit and data_size with values from connect_info */
	our_trace_uint64("option", "count", options->limit, options);
	options->limit = ntohll(connect_info->remote_limit);
	our_report_uint64("client", "count", options->limit, options);

	our_trace_uint64("option", "data_size", options->data_size, options);
	options->data_size = ntohll(connect_info->remote_data_size);
	our_report_uint64("client", "data_size", options->data_size, options);

	/* simple when we have not created our own channel */
	conn->cm_id = new_cm_id;
	new_cm_id->context = conn;

	/* report new cm_id created for us */
	our_trace_ptr("our_migrate_id", "migrated cm_id", conn->cm_id, options);
	return 0;
}	/* our_migrate_id */


/* release a communication identifier, canceling any outstanding
 * asynchronous operation on it.
 *
 * returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
int
our_destroy_id(struct our_control *conn, struct our_options *options)
{
	int	ret;

	errno = 0;
	ret = rdma_destroy_id(conn->cm_id);
	if (ret != 0) {
		our_report_error(ret, "rdma_destroy_id", options);
	} else {
		our_trace_ptr("rdma_destroy_id", "destroyed cm_id",
							conn->cm_id, options);
	}
	return ret;
}	/* our_destroy_id */
