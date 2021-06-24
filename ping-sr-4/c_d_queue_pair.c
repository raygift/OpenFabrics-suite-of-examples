/* c_d_queue_pair.c */
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
 * Called only from our_setup_qp() to create a queue pair on conn
 *
 * Returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
static int
our_create_qp(struct our_control *conn, struct our_options *options)
{
	struct ibv_qp_init_attr	init_attr;
	int			ret;


	/* set up parameters to define properties of the new queue pair */
	memset(&init_attr, 0, sizeof(init_attr));
	init_attr.qp_context = conn;
	init_attr.send_cq = conn->completion_queue;
	init_attr.recv_cq = conn->completion_queue;
	init_attr.srq = NULL;
	init_attr.cap.max_send_wr = options->send_queue_depth;
	init_attr.cap.max_recv_wr = options->recv_queue_depth;
	init_attr.cap.max_send_sge = options->max_send_sge;
	init_attr.cap.max_recv_sge = options->max_recv_sge;
	init_attr.cap.max_inline_data = 0;
	init_attr.qp_type = IBV_QPT_RC;
	init_attr.sq_sig_all = 0;
	init_attr.xrc_domain = NULL;

	/* allocate a queue pair associated with the cm_id for this connection
	 * and transition it for sending and receiving.  On return, it is ready
	 * to handle posting of sends and receives.
	 */
	errno = 0;
	ret = rdma_create_qp(conn->cm_id, conn->protection_domain, &init_attr);
	if (ret != 0) {
		our_report_error(ret, "rdma_create_qp", options);
	} else {
		conn->queue_pair = conn->cm_id->qp;
		our_trace_ptr("rdma_create_qp", "created queue pair",
				conn->queue_pair, options);
		our_trace_ulong("rdma_create_qp", "max_send_wr",
				init_attr.cap.max_send_wr, options);
		our_trace_ulong("rdma_create_qp", "max_recv_wr",
				init_attr.cap.max_recv_wr, options);
		our_trace_ulong("rdma_create_qp", "max_send_sge",
				init_attr.cap.max_send_sge, options);
		our_trace_ulong("rdma_create_qp", "max_recv_sge",
				init_attr.cap.max_recv_sge, options);
		our_trace_ulong("rdma_create_qp", "max_inline_data",
				init_attr.cap.max_inline_data, options);
	}

	return ret;
}	/* our_create_qp */


/*
 * Called only from our_setup_qp() and our_destroy_qp()
 * to destroy the queue pair on conn
 *
 * Returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
static int
our_destroy_qp(struct our_control *conn, struct our_options *options)
{
	int	ret;

	errno = 0;
	ret = ibv_destroy_qp(conn->queue_pair);
	if (ret != 0) {
		our_report_error(ret, "ibv_destroy_qp", options);
	} else {
		our_trace_ptr("ibv_destroy_qp", "destroyed queue pair",
				conn->queue_pair, options);
	}

	return ret;
}	/* our_destroy_qp */


/*
 * Called only from our_setup_qp() to create a completion queue on conn
 *
 * Returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
static int
our_create_cq(struct our_control *conn, struct rdma_cm_id *cm_id,
		struct our_options *options)
{
	int	ret, ret1;

	errno = 0;
	conn->completion_queue = ibv_create_cq(cm_id->verbs,
					options->send_queue_depth * 2,
					conn, conn->completion_channel, 0);
	if (conn->completion_queue == NULL) {
		ret = ENOMEM;
		our_report_error(ret, "ibv_create_cq", options);
		goto out0;
	}
	our_trace_ptr("ibv_create_cq", "created completion queue",
			conn->completion_queue, options);
	our_trace_ulong("ibv_create_cq", "returned cqe",
			conn->completion_queue->cqe, options);

	/* allocate an array big enough to never fill up, even if
	 * completion queue is full when polled
	 */
	conn->max_n_work_completions = conn->completion_queue->cqe + 1;
	conn->work_completion = our_calloc(conn->max_n_work_completions
			* sizeof(struct ibv_wc), "work_completion array");
	if (conn->work_completion == NULL) {
		ret = ENOMEM;
		goto out1;
	}

	conn->completion_stats = our_calloc((conn->max_n_work_completions + 1)
			* sizeof(unsigned long), "completion_stats");
	if (conn->completion_stats == NULL) {
		ret = ENOMEM;
		goto out2;
	}

	/* initially there are no polled work completions in the array */
	conn->current_n_work_completions = 0;
	conn->index_work_completions = 0;
	ret = 0;
	goto out0;

out2:
	free(conn->work_completion);
out1:
	errno = 0;
	ret1 = ibv_destroy_cq(conn->completion_queue);
	if (ret1 != 0) {
		/* fails if any queue pair is still associated with this CQ */
		our_report_error(ret1, "ibv_destroy_cq", options);
	}
out0:
	return ret;
}	/* our_create_cq */


/*
 * Called only from our_setup_qp() and our_unsetup_qp()
 * to destroy the completion queue on conn
 *
 * Returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 *
 * Problematic:
 *	"To avoid races, destroying a CQ will wait for all
 *	 completion events to be acknowledged; this guarantees a
 *	 one-to_one correspondence between acks and successful gets."
 */
static int
our_destroy_cq(struct our_control *conn, struct our_options *options)
{
	int	ret;

	free(conn->completion_stats);
	free(conn->work_completion);

	errno = 0;
	ret = ibv_destroy_cq(conn->completion_queue);
	if (ret != 0) {
		/* fails if any queue pair is still associated with this CQ */
		our_report_error(ret, "ibv_destroy_cq", options);
	} else {
		our_trace_ptr("ibv_destroy_cq", "destroyed completion queue",
						conn->completion_queue,options);
	}

	return ret;
}	 /* our_destroy_cq */


/*
 * Called only from our_setup_qp() to create a completion channel on conn
 *
 * Returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
static int
our_create_comp_channel(struct our_control *conn, struct rdma_cm_id *cm_id,
			struct our_options *options)
{
	int	ret;

	/* create a completion channel for this cm_id */
	errno = 0;
	conn->completion_channel = ibv_create_comp_channel(cm_id->verbs);
	if (conn->completion_channel == NULL) {
		ret = ENOMEM;
		our_report_error(ret, "ibv_create_comp_channel", options);
		goto out0;
	}

	/* report the new completion channel created by us and its fd */
	our_trace_ptr("ibv_create_comp_channel", "created completion channel",
					conn->completion_channel, options);
	our_trace_ulong("ibv_create_comp_channel", "assigned fd",
					conn->completion_channel->fd, options);

	/* now put the fd of the new channel into non-blocking mode */
	errno = 0;
	ret = fcntl(conn->completion_channel->fd, F_GETFL);
	if (ret == -1) {
		our_report_error(ret, "fcntl F_GETFL", options);
		goto out1;
	}
	errno = 0;
	ret = fcntl(conn->completion_channel->fd, F_SETFL, ret|O_NONBLOCK);
	if (ret != 0) {
		our_report_error(ret, "fcntl F_SETFL", options);
		goto out1;
	}

	/* finally, set up poll_fds entry so we can poll the channel for read */
	conn->poll_fds[COMPLETE_FD].fd = conn->completion_channel->fd;
	conn->poll_fds[COMPLETE_FD].events = POLLIN;
	goto out0;
out1:
	ibv_destroy_comp_channel(conn->completion_channel);
out0:
	return ret;
}	/* our_create_comp_channel */


/*
 * Called only from our_setup_qp() and our_unsetup_qp()
 * to destroy the completion channel on conn
 *
 * Returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
static int
our_destroy_comp_channel(struct our_control *conn, struct our_options *options)
{
	int	ret;

	errno = 0;
	ret = ibv_destroy_comp_channel(conn->completion_channel);
	if (ret != 0) {
		/* this fails if any CQs are still associated with the
		 * completion event channel being destroyed
		 */
		our_report_error(ret, "ibv_destroy_comp_channel", options);
	} else {
		our_trace_ptr("ibv_destroy_comp_channel",
				"destroyed completion channel",
				conn->completion_channel, options);
	}

	return ret;
}	/* our_destroy_comp_channel */


/* Called only from our_setup_qp() to create a protection domain on conn
 *
 * Returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
static int
our_alloc_pd(struct our_control *conn, struct rdma_cm_id *cm_id,
		struct our_options *options)
{
	int	ret;

	ret = 0;
	errno = 0;
	conn->protection_domain = ibv_alloc_pd(cm_id->verbs);
	if (conn->protection_domain == NULL) {
		ret = ENOMEM;
		our_report_error(ret, "ibv_alloc_pd", options);
	} else {
		our_trace_ptr("ibv_alloc_pd", "allocated protection domain",
					conn->protection_domain, options);
	}
	return ret;
}	/* our_alloc_pd */


/* Called only from our_setup_qp() and our_unsetup_qp() to destroy a pd */
static void
our_dealloc_pd(struct our_control *conn, struct our_options *options)
{
	int	ret;

	errno = 0;
	ret = ibv_dealloc_pd(conn->protection_domain);
	if (ret != 0) {
		our_report_error(ret, "ibv_dealloc_pd", options);
	} else {
		our_trace_ptr("ibv_dealloc_pd", "deallocated protection domain",
					conn->protection_domain, options);
	}
}	/* our_dealloc_pd */


/*
 * Called to set up a complete queue pair on conn
 *
 * Returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
int
our_setup_qp(struct our_control *conn, struct rdma_cm_id *cm_id,
		struct our_options *options)
{
	int	ret;

	/* create a protection domain */
	ret = our_alloc_pd(conn, cm_id, options);
	if (ret != 0)
		goto err0;

	/* create a completion channel for this cm_id */
	ret = our_create_comp_channel(conn, cm_id, options);
	if (ret != 0) {
		goto err1;
	}

	/* create a completion queue */
	ret = our_create_cq(conn, cm_id, options);
	if (ret != 0) {
		goto err2;
	}

	/* create a queue pair */
	ret = our_create_qp(conn, options);
	if (ret != 0) {
		goto err3;
	}

	/* request notification when first completion is queued */
	errno = 0;
	ret = ibv_req_notify_cq(conn->completion_queue, 0);
	if (ret != 0) {
		our_report_error(ret, "ibv_req_notify_cq", options);
		goto err4;
	}

	/* everything worked ok */
	goto err0;

err4:
	our_destroy_qp(conn, options);
err3:
	our_destroy_cq(conn, options);
err2:
	our_destroy_comp_channel(conn, options);
err1:
	our_dealloc_pd(conn, options);
err0:
	return ret;
}	/* our_setup_qp */


/* Called to unsetup a complete queue pair on conn */
void
our_unsetup_qp(struct our_control *conn, struct our_options *options)
{
	our_destroy_qp(conn, options);
	our_destroy_cq(conn, options);
	our_destroy_comp_channel(conn, options);
	our_dealloc_pd(conn, options);
}	/* our_unsetup_qp */
