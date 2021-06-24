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
our_setup_conn_params(struct rdma_conn_param *params)
{
	memset(params, 0, sizeof(*params));

	params->private_data = NULL;
	params->private_data_len = 0;
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
	int			ret;

	/* set up parameters to define properties of the new connection */
	our_setup_conn_params(&client_params);

	errno = 0;
	ret = rdma_connect(client_conn->cm_id, &client_params);
	if (ret != 0) {
		our_report_error(ret, "rdma_connect", options);
		goto out0;
	}

	/* in this demo, rdma_connect() operates synchronously */

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

	our_setup_conn_params(&agent_params);

	errno = 0;
	ret = rdma_accept(agent_conn->cm_id, &agent_params);
	if (ret != 0) {
		our_report_error(ret, "rdma_accept", options);
		goto out0;
	}

	/* in this demo, rdma_accept() operates synchronously */

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

	/* in this demo, rdma_disconnect() operates synchronously */

	/* connection disconnected ok */
	our_report_ptr(cptr, "disconnected cm_id", conn->cm_id, options);
out0:
	return ret;
}	/* our_disconnect */
