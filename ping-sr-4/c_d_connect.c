/* c_d_connect.c */
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


/* set up parameters to define properties of the new connection */
static void
our_setup_conn_params(struct rdma_conn_param *params,
			void *private_data, unsigned long private_data_len)
{
	memset(params, 0, sizeof(*params));

	params->private_data = private_data;
	params->private_data_len = private_data_len;
	params->responder_resources = 2;
	params->initiator_depth = 2;
	params->retry_count = 5;
	params->rnr_retry_count = 5;
}	/* our_setup_conn_params */


/*
 * returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
int
our_client_connect(struct our_control *client_conn, struct our_options *options)
{
	struct rdma_conn_param	client_params;
	struct our_connect_info	connect_info;
	int			ret;

	/* pass client's limit and data_size to the server as private data */
	connect_info.remote_limit = htonll(options->limit);
	connect_info.remote_data_size = htonll(options->data_size);

	/* set up parameters to define properties of the new connection */
	our_setup_conn_params(&client_params,&connect_info,sizeof(connect_info));

	our_trace_ulong("rdma_connect", "private_data_len",
						sizeof(connect_info), options);
	errno = 0;
	ret = rdma_connect(client_conn->cm_id, &client_params);
	if (ret != 0) {
		our_report_error(ret, "rdma_connect", options);
		goto out0;
	}

	/* in this demo, rdma_connect() operates asynchronously */
	ret = our_await_cm_event(client_conn, RDMA_CM_EVENT_ESTABLISHED,
					"rdma_connect", NULL, NULL, options);
	if (ret != 0) {
		goto out0;
	}

	/* client connection established ok */
	our_report_ptr("rdma_connect", "connected cm_id", client_conn->cm_id,
								options);
out0:
	return ret;
}	/* our_client_connect */


/*
 * returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
int
our_agent_connect(struct our_control *agent_conn, struct our_options *options)
{
	struct rdma_conn_param	agent_params;
	int			ret;

	our_setup_conn_params(&agent_params, NULL, 0);

	errno = 0;
	ret = rdma_accept(agent_conn->cm_id, &agent_params);
	if (ret != 0) {
		our_report_error(ret, "rdma_accept", options);
		goto out0;
	}

	/* in this demo, rdma_accept() operates asynchronously */
	ret = our_await_cm_event(agent_conn, RDMA_CM_EVENT_ESTABLISHED,
					"rdma_accept", NULL, NULL, options);
	if (ret != 0) {
		goto out0;
	}

	/* agent connection established ok */
	our_report_ptr("rdma_accept", "accepted cm_id", agent_conn->cm_id,
								options);

out0:
	return ret;
}	/* our_agent_connect */


/*
 * returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
int
our_disconnect(struct our_control *conn, struct our_options *options)
{
	int	ret;
	char	*cptr;

	if (conn->disconnected != 0) {
		/* remote side disconnected before we did, don't do it again */
		ret = 0;
		goto out0;
	}

	conn->disconnected = 1;
	errno = 0;
	ret = rdma_disconnect(conn->cm_id);
	if (ret != 0) {
		if (errno == EINVAL) {
			cptr = "remote";
			ret = 0;
		} else {
			our_report_error(ret, "rdma_disconnect",  options);
			goto out0;
		}
	} else {
		cptr = "rdma_disconnect";
	}

	/* in this demo, rdma_disconnect() operates asynchronously */
	ret = our_await_cm_event(conn, RDMA_CM_EVENT_DISCONNECTED, cptr,
							NULL, NULL, options);
	if (ret != 0) {
		goto out0;
	}

	/* connection disconnected ok */
	our_report_ptr(cptr, "disconnected cm_id", conn->cm_id, options);
out0:
	return ret;
}	/* our_disconnect */
