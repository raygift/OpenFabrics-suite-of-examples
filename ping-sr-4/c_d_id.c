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


/* create a communication channel on which cm events will be reported.
 * one channel can be shared by more than one cm_id.
 * Each channel is created with an associated UNIX fd assigned to it.
 *
 * returns == 0 if all ok,
 *	   != 0 on any error.
 */
static int
our_create_event_channel(struct our_control *conn, struct our_options *options)
{
	int	ret;

	errno = 0;
	conn->cm_event_channel = rdma_create_event_channel();
	if (conn->cm_event_channel == NULL) {
		ret = ENOMEM;
		our_report_error(ret, "rdma_create_event_channel", options);
		goto out0;
	}

	/* report the new communication channel created by us and its fd */
	our_trace_ptr("rdma_create_event_channel", "created cm_event_channel",
					conn->cm_event_channel, options);
	our_trace_ulong("rdma_create_event_channel", "assigned fd",
					conn->cm_event_channel->fd, options);

	/* now put the fd of the new channel into non-blocking mode */
	errno = 0;
	ret = fcntl(conn->cm_event_channel->fd, F_GETFL);
	if (ret == -1) {
		our_report_error(ret, "fcntl F_GETFL", options);
		goto out1;
	}
	errno = 0;
	ret = fcntl(conn->cm_event_channel->fd, F_SETFL, ret|O_NONBLOCK);
	if (ret != 0) {
		our_report_error(ret, "fcntl F_SETFL", options);
		goto out1;
	}

	/* finally, set up poll_fds entry so we can poll the channel for read */
	conn->poll_fds[CM_EVENT_FD].fd = conn->cm_event_channel->fd;
	conn->poll_fds[CM_EVENT_FD].events = POLLIN;
	goto out0;
out1:
	rdma_destroy_event_channel(conn->cm_event_channel);
out0:
	return ret;
}	/* our_create_event_channel */


/* destroy a communication channel on which cm events were reported. */
static void
our_destroy_event_channel(struct our_control *conn, struct our_options *options)
{
	rdma_destroy_event_channel(conn->cm_event_channel);
	our_trace_ptr("rdma_destroy_event_channel", "destroyed event_channel",
					conn->cm_event_channel, options);
}	/* our_destroy_event_channel */


/*
 * create a communication identifier used to identify which
 * RDMA device a cm event is being reported about
 *
 * also create a communication channel on which cm events will be reported,
 * which means almost all cm operations will be performed asynchronously
 * except for rdma_get-cm_event() which will still block
 *
 * returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
int
our_create_id(struct our_control *conn, struct our_options *options)
{
	int	ret;

	ret = our_create_event_channel(conn, options);
	if (ret != 0)
		goto out0;

	errno = 0;
	ret = rdma_create_id(conn->cm_event_channel, &conn->cm_id, conn,
								RDMA_PS_TCP);
	if (ret != 0) {
		our_report_error(ret, "rdma_create_id", options);
		our_destroy_event_channel(conn, options);
	} else {
		our_trace_ptr("rdma_create_id", "created cm_id", conn->cm_id,
								options);
	}
out0:
	return ret;
}	/* our_create_id */


/* already have a communication identifier,
 * migrate it to use a new channel and set its context to be this new conn
 *
 * returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
int
our_migrate_id(struct our_control *conn, struct rdma_cm_id *new_cm_id,
		struct our_connect_info *connect_info,
		struct our_options *options)
{
	int	ret;

	/* replace conn's limit and data_size with values from connect_info */
	our_trace_uint64("option", "count", options->limit, options);
	options->limit = ntohll(connect_info->remote_limit);
	our_report_uint64("client", "count", options->limit, options);

	our_trace_uint64("option", "data_size", options->data_size, options);
	options->data_size = ntohll(connect_info->remote_data_size);
	our_report_uint64("client", "data_size", options->data_size, options);

	/* create our own channel */
	ret = our_create_event_channel(conn, options);
	if (ret != 0)
		goto out0;

	errno = 0;
	ret = rdma_migrate_id(new_cm_id, conn->cm_event_channel);
	if (ret != 0) {
		our_report_error(ret, "rdma_migrate_id", options);
		goto out1;
	}
	conn->cm_id = new_cm_id;
	new_cm_id->context = conn;

	/* report new cm_id created for us */
	our_trace_ptr("rdma_migrate_id","migrated cm_id",conn->cm_id,options);
	goto out0;
out1:
	our_destroy_event_channel(conn, options);
out0:
	return ret;
}	/* our_migrate_id */


/* release a communication identifier, canceling any outstanding
 * asynchronous operation on it,
 * and destroy the communication channel we created
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
	our_destroy_event_channel(conn, options);
	return ret;
}	/* our_destroy_id */
