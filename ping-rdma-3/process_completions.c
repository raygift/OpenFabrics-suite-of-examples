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
	our_report_string("Debug","our_wait_for_notification","will call ibv_get_cq_event",options);
	ret = ibv_get_cq_event(conn->completion_channel,
						&event_queue, &event_context);
	our_report_string("Debug","our_wait_for_notification","done call ibv_get_cq_event",options);

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
	char* opStr="";

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

	fprintf(stderr, "%s: collected  %d from cq element \n",
		options->message, ret);
	for(int i=0;i<ret;i++){
switch ((&conn->work_completion[i])->opcode) {
	case IBV_WC_SEND:
		opStr="IBV_WC_SEND";
		break;
	case IBV_WC_RDMA_WRITE:
		opStr="IBV_WC_RDMA_WRITE";
		break;
	case IBV_WC_RDMA_READ:
		opStr="IBV_WC_RDMA_READ";
		break;
	case IBV_WC_COMP_SWAP:
		opStr="IBV_WC_COMP_SWAP";
		break;
	case IBV_WC_FETCH_ADD:
		opStr="IBV_WC_FETCH_ADD";
		break;
	case IBV_WC_BIND_MW:
		opStr="IBV_WC_BIND_MW";
		break;
	case IBV_WC_LOCAL_INV:
		opStr="IBV_WC_LOCAL_INV";
		break;
	case IBV_WC_RECV:
		opStr="IBV_WC_RECV";
		break;
	case IBV_WC_RECV_RDMA_WITH_IMM:
		opStr="IBV_WC_RECV_RDMA_WITH_IMM";
		break;
	case IBV_WC_RNR_RETRY_EXC_ERR:
		opStr="IBV_WC_RNR_RETRY_EXC_ERR";
		break;
	default:
		opStr="other opcode";

}
	fprintf(stderr, "%s: cq element %d's opcode is %s, wr_id is %lu,wc_flags is %d ,status is %s\n",
		options->message, i, opStr,(&conn->work_completion[i])->wr_id,(&conn->work_completion[i])->wc_flags,ibv_wc_status_str((&conn->work_completion[i])->status));
	}
	} while (ret == 0);
	// } while ((ret == 0)||(opStr=!"IBV_WC_RDMA_WRITE") );

	/* have picked up a work completion */
	ret = our_check_completion_status(conn, *work_completion, options);
out0:
	return ret;
}	/* our_await_completion */


/* busy wait until buffer gets full non-zero pattern
 * (i.e., until all bytes in the buffer become non-zero)
 *
 * Returns 0 if successful (and updates max_spin_count as appropriate)
 *	  -1 if remote side has disconnected unexpectedly
 */
int
our_all_non_zero_completion_match(struct our_control *conn, unsigned char *bptr,
			uint64_t size, uint64_t *max_spin_count,
			struct our_options *options)
{
	unsigned char	*ptr;
	uint64_t	i, spin_count;
	int		ret;
	our_report_string("busy wait",
			"our_all_non_zero_completion_match", "", options);
	ret = 0;
	for (spin_count = 0; ; spin_count++) {
		/* stop everything if remote side disconnected unexpectedly */
		if (conn->disconnected != 0) {
			ret = -1;
			errno = ECONNRESET;
			our_report_error(ret, "completion_match", options);
			break;
		}
		/* start at end of buffer and scan backwards */
		ptr = bptr + size;
		for (i = size; i > 0; i--) {
			if (*(--ptr) == 0)
				break;
		}
		/* loop finishes normally if all bytes are non-zero */
		if (i <= 0)
			break;
	}
	if (*max_spin_count < spin_count)
			*max_spin_count = spin_count;
	return ret;
}	/* our_all_non_zero_completion_match */

/* fake busy wait
 * 
 *
 * Returns 0 if successful (and updates max_spin_count as appropriate)
 *	  -1 if remote side has disconnected unexpectedly
 */
int
test_all_non_zero_completion_match(struct our_control *conn, unsigned char *bptr,
			uint64_t size, uint64_t *max_spin_count,
			struct our_options *options)
{
	// unsigned char	*ptr;
	uint64_t	spin_count;
	int		ret;
	// our_report_string("no busy wait",
	// 		"", "", options);
		fprintf(stdout,"no busy wait\n");

	ret = 0;
	for (spin_count = 0; ; spin_count++) {
		/* stop everything if remote side disconnected unexpectedly */
		if (conn->disconnected != 0) {
			ret = -1;
			errno = ECONNRESET;
			our_report_error(ret, "completion_match", options);
			break;
		}
		//尽快退出busy wait，使得server 在没有拿到client 发送的完整信息时发送pong，目的是导致ping pong数据verify失败
		break;

	}
	if (*max_spin_count < spin_count)
			*max_spin_count = spin_count;
	return ret;
}	/* test_all_non_zero_completion_match */

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
