/* send_recv.c */
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


/* called to post a work request to the send queue
 * returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
int
our_post_send(struct our_control *conn, struct ibv_send_wr *send_work_request,
		struct our_options *options)
{
	struct ibv_send_wr	*bad_wr;
	int	ret;

	errno = 0;
	ret = ibv_post_send(conn->queue_pair, send_work_request, &bad_wr);
	if (ret != 0) {
		if (our_report_wc_status(ret, "ibv_post_send", options) != 0) {
			our_report_error(ret, "ibv_post_send", options);
		}
	}
	return ret;
}	/* our_post_send */


/* called to post a work request to the receive queue
 * returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
int
our_post_recv(struct our_control *conn, struct ibv_recv_wr *recv_work_request,
		struct our_options *options)
{
	struct ibv_recv_wr	*bad_wr;
	int	ret;

	errno = 0;
	ret = ibv_post_recv(conn->queue_pair, recv_work_request, &bad_wr);
	if (ret != 0) {
		if (our_report_wc_status(ret, "ibv_post_recv", options) != 0) {
			our_report_error(ret, "ibv_post_recv", options);
		}
	}
	return ret;
}	/* our_post_recv */
