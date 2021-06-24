/* process_completions.c */
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


/* returns == 0 if work_completion has status == 0 (no error)
 *	   != 0 on error (and error message has been given)
 */
static int
our_check_completion_status(struct our_control *conn,
			   struct ibv_wc *work_completion,
			   struct our_options *options)
{
	int	ret;

	ret = work_completion->status;
	if (ret != 0) {
		if (ret == IBV_WC_WR_FLUSH_ERR) {
			our_report_string( "ibv_poll_cq", "completion status",
							"flushed", options);
		} else if (our_report_wc_status(ret, "ibv_poll_cq", options)) {
			our_report_ulong("ibv_poll_cq", "completion status",
								ret, options);
		}
	}
	return ret;
}	/* our_check_completion_status */


/* returns == 0 if filled work_completion with status == 0 (no error)
 *	   != 0 on error (and error message has been given)
 */
int
our_await_completion(struct our_control *conn,
			   struct ibv_wc *work_completion,
			   struct our_options *options)
{
	int		ret;

	/* busy wait for next work completion to appear in completion queue */
	do	{
		errno = 0;
		ret = ibv_poll_cq(conn->completion_queue, 1, work_completion);
	} while (ret == 0);

	/* should have gotten exactly 1 work completion */
	if (ret != 1) {
		/* ret cannot be 0, and should never be > 1, so must be < 0 */
		our_report_error(ret, "ibv_poll_cq", options);
	} else {
		ret = our_check_completion_status(conn,work_completion,options);
	}
	return ret;
}	/* our_await_completion */
