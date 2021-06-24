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
int
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


/* returns == 0 if at least 1 wc in conn's work_completion array
 *	   != 0 on error (and error message has been given)
 */
int
our_fill_completion_array(struct our_control *conn,
			   struct our_options *options)
{
	int			ret;

	our_trace_ulong("our_fill_completion_array", "index_work_completions",
			conn->index_work_completions, options);
	our_trace_ulong("our_fill_completion_array","current_n_work_completions",
			conn->current_n_work_completions, options);
	our_trace_ptr("our_fill_completion_array", "our_control",
			conn, options);
	our_trace_ptr("our_fill_completion_array", "current cm_id->channel",
			conn->cm_id->channel, options);

	/* see if any work completions remain in our array */
	if (conn->index_work_completions < conn->current_n_work_completions) {
		/* we already have a polled but unprocessed completion */
		return 0;
	}

	/* busy wait for more polled completions to arrive */
	for ( ; ; ) {
		errno = 0;
		ret = ibv_poll_cq(conn->completion_queue,
				conn->max_n_work_completions,
				conn->work_completion);
		if (ret > 0)
			break;
		if (ret < 0) {
			our_report_error(ret, "ibv_poll_cq", options);
			goto out0;
		}
	}

	/* got at least 1 completion, maybe more */
	conn->current_n_work_completions = ret;
	conn->index_work_completions = 0;
	our_trace_ulong("our_fill_completion_array","current_n_work_completions",
			conn->current_n_work_completions, options);

	/* keep statistics on the number of completions obtained */
	conn->completion_stats[ret]++;

	ret = 0;
out0:
	return ret;
}	/* our_fill_completion_array */


/* returns == 0 if filled work_completion with status == 0 (no error)
 *	   != 0 on error (and error message has been given)
 */
int
our_await_completion(struct our_control *conn,
			   struct ibv_wc **work_completion,
			   struct our_options *options)
{
	int			ret;

	/* see if already processed all polled completions, wait for more */
	ret = our_fill_completion_array(conn, options);
	if (ret != 0 ) {
		goto out0;
	}

	/* we now have at least 1 unused work completion in our array */
	*work_completion=&conn->work_completion[conn->index_work_completions++];

	ret = our_check_completion_status(conn, *work_completion, options);
out0:
	return ret;
}	/* our_await_completion */


/* add this send work request to end of linked list */
void
our_add_to_work_request_list(struct our_send_work_request_list *list,
		struct ibv_send_wr *work_request, struct our_options *options)
{
	if (list->size == 0) {
		/* first item being added to empty list */
		list->head = work_request;
	} else {
		list->tail->next = work_request;
	}
	list->tail = work_request;
	work_request->next = NULL;
	list->size += 1;
}	/* our_add_to_work_request_list */


/* remove and return first item from send work request list
 * return NULL if list is empty
 */
struct ibv_send_wr *
our_get_from_work_request_list(struct our_send_work_request_list *list,
				struct our_options *options)
{
	struct ibv_send_wr	*work_request;

	if (list->size <= 0) {
		return NULL;
	}
	work_request = list->head;
	list->size--;
	if (list->size > 0) {
		list->head = list->head->next;
		work_request->next = NULL;
	}
	return work_request;
}	/* our_get_from_work_request_list */


void
print_ibv_poll_cq_stats(struct our_control *conn, struct our_options *options)
{
	int		i;
	char		buffer[64];
	unsigned long	total;
	unsigned long	weighted_total;

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
