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


/* set up parameters to define properties of the new queue pair */
static void
our_setup_qp_params(struct our_control *conn,
			struct ibv_qp_init_attr *init_attr,
			struct our_options *options)
{
	memset(init_attr, 0, sizeof(*init_attr));

	init_attr->qp_context = conn;
	init_attr->send_cq = conn->completion_queue;
	init_attr->recv_cq = conn->completion_queue;
	init_attr->srq = NULL;
	init_attr->cap.max_send_wr = options->send_queue_depth;
	init_attr->cap.max_recv_wr = options->recv_queue_depth;
	init_attr->cap.max_send_sge = options->max_send_sge;
	init_attr->cap.max_recv_sge = options->max_recv_sge;
	init_attr->cap.max_inline_data = 0;
	init_attr->qp_type = IBV_QPT_RC;
	init_attr->sq_sig_all = 0;
	init_attr->xrc_domain = NULL;
}	/* our_setup_qp_params */


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
	our_setup_qp_params(conn, &init_attr, options);

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
 * Called only from our_setup_qp()
 * to destroy the queue pair on conn
 */
static void
our_destroy_qp(struct our_control *conn, struct our_options *options)
{
	rdma_destroy_qp(conn->cm_id);
	our_trace_ptr("rdma_destroy_qp", "destroyed queue pair",
			conn->queue_pair, options);
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
	int	ret;

	our_trace_ulong("our_create_cq", "cm_id->verbs->num_comp_vectors",
				cm_id->verbs->num_comp_vectors, options);

	errno = 0;
	conn->completion_queue = ibv_create_cq(cm_id->verbs,
					options->send_queue_depth * 2,
					conn, NULL, 0);
	if (conn->completion_queue == NULL) {
		/* when ibv_create_cq() returns NULL, it should set errno */
		ret = ENOMEM;
		our_report_error(ret, "ibv_create_cq", options);
		goto out0;
	}
	our_trace_ptr("ibv_create_cq", "created completion queue",
			conn->completion_queue, options);
	our_trace_ulong("ibv_create_cq", "returned cqe",
			conn->completion_queue->cqe, options);
	our_trace_ptr("ibv_create_cq", "returned completion queue channel",
			conn->completion_queue->channel, options);
	ret = 0;
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

	/* create a completion queue */
	ret = our_create_cq(conn, cm_id, options);
	if (ret != 0) {
		goto err1;
	}

	/* create a queue pair */
	ret = our_create_qp(conn, options);
	if (ret != 0) {
		goto err2;
	}

	/* everything worked ok */
	goto err0;

err2:
	our_destroy_cq(conn, options);
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
	our_dealloc_pd(conn, options);
}	/* our_unsetup_qp */
