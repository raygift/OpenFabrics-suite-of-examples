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


/* this waits for completion events
 * returns == 0 if got a completion notification event ok,
 *	   != 0 on error (and error message has been given)
 */
static int
our_wait_for_notification(struct our_control *conn, struct our_options *options)
{
	struct ibv_cq	*event_queue;
	void		*event_context;
	int		ret;

	/* wait for a completion notification (this verb blocks) */
	errno = 0;
	ret = ibv_get_cq_event(conn->completion_channel,
						&event_queue, &event_context);
	if (ret != 0) {
		our_report_error(ret, "ibv_get_cq_event", options);
		goto out0;
	}

	conn->cq_events_that_need_ack++;
	if (conn->cq_events_that_need_ack == UINT_MAX) {
		ibv_ack_cq_events(conn->completion_queue, UINT_MAX);
		conn->cq_events_that_need_ack = 0;
	}

	/* request a notification when next completion arrives
	 * into an empty completion queue.
	 * See examples on "man ibv_get_cq_event" for how an
	 * "extra event" may be triggered due to a race between
	 * this ibv_req_notify() and the subsequent ibv_poll_cq()
	 * that empties the completion queue.
	 * The number of occurrences of this race will show up as
	 * the value of completion_stats[0].
	 */
	errno = 0;
	ret = ibv_req_notify_cq(conn->completion_queue, 0);
	if (ret != 0) {
		our_report_error(ret, "ibv_req_notify_cq", options);
		goto out0;
	}

	if (event_queue != conn->completion_queue) {
		fprintf(stderr, "%s, %s got notify for completion "
			"queue %p, exected queue %p\n",
			options->message, "ibv_get_cq_event",
			event_queue, conn->completion_queue);
		ret = -1;
		goto out0;
	}
	
	if (event_context != conn) {
		fprintf(stderr, "%s, %s got notify for completion "
			"context %p, exected context %p\n",
			options->message, "ibv_get_cq_event",
			event_context, conn);
		ret = -1;
		goto out0;
	}
	
out0:
	return ret;
}	/* our_wait_for_notification */


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
			pthread_mutex_lock(&conn->cm_event_info_lock);
			if (conn->disconnected == 0) {
				/* flush provoked remotely,
				 * no need to call rdma_disconnect() locally
				 */
				conn->disconnected = 3;
			}
			pthread_mutex_unlock(&conn->cm_event_info_lock);
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
			   struct ibv_wc **work_completion,
			   struct our_options *options)
{
	int	ret;

	/* wait for next work completion to appear in completion queue */
	do	{
		if (conn->index_work_completions
					< conn->current_n_work_completions) {
			/* we already have a polled but uprocessed completion */
			*work_completion = &conn->work_completion
						[conn->index_work_completions++];
			break;
		}
		/* already processed all polled completions,
		 * wait for notification of more arrivals
		 */
		ret = our_wait_for_notification(conn, options);
		if (ret != 0) {
			goto out0;
		}
		conn->notification_stats++;
		/* now collect every completion in the queue */
		errno = 0;
		ret = ibv_poll_cq(conn->completion_queue,
				conn->max_n_work_completions,
				conn->work_completion);
		if (ret < 0) {
			our_report_error(ret, "ibv_poll_cq", options);
			goto out0;
		}
		/* keep statistics on the number of items returned */
		if (ret > conn->max_n_work_completions) {
			our_report_ulong("WARNING: ibv_poll_cq", "returned",
								ret, options);
			our_report_ulong("WARNING: but",
					"max_n_work_completions",
					conn->max_n_work_completions, options);
		} else {
			conn->completion_stats[ret]++;
		}
		if (ret > 0) {
			/* got at least 1 completion, use element 0 */
			conn->current_n_work_completions = ret;
			conn->index_work_completions = 1;
			*work_completion = &conn->work_completion[0];
		}
	} while (ret == 0);

	/* have picked up a work completion */
	ret = our_check_completion_status(conn, *work_completion, options);
out0:
	return ret;
}	/* our_await_completion */


void
print_ibv_poll_cq_stats(struct our_control *conn, struct our_options *options)
{
	int		i;
	char		buffer[64];
	unsigned long	total;
	unsigned long	weighted_total;

	our_report_ulong("ibv_get_cq_event", "calls",
			conn->notification_stats, options);

	/* find last non-zero histogram bin */
	for (i = conn->max_n_work_completions; i >= 0; i--) {
		if (conn->completion_stats[i] > 0) {
			break;
		}
	}
	total = 0;
	weighted_total = 0;
	for (; i >= 0; i--) {
		snprintf(buffer, 64, "%s[%d]", "work_completion_array_size", i);
		our_report_ulong(buffer, "occurrences",
			conn->completion_stats[i], options);
		total += conn->completion_stats[i];
		weighted_total += conn->completion_stats[i] * i;
	}

	our_report_ulong("total", "occurrences", total, options);
	our_report_ulong("total", "work completions", weighted_total, options);
}	/* print_ibv_poll_cq_stats */
