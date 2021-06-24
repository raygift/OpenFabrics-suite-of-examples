/* server.c - client uses both rdma_write and rdma_read to ping-pong user data */
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


/* this is the server-agent who deals with 1 client on this event_cm_id
 * connect_info contains the private data sent in the client's connect request
 */
static int
our_agent(struct rdma_cm_id *event_cm_id, struct our_connect_info *connect_info,
	  struct our_options *listener_options)
{
	struct our_control	*agent_conn;
	struct our_options	options;
	int			result;

	/* assume there is an error somewhere along the line */
	result = EXIT_FAILURE;

	/* create a local copy of the listener's options for this agent,
	 * because this agent will change some to agree with client's values
	 */
	memcpy(&options, listener_options, sizeof(struct our_options));

	agent_conn = our_create_control_struct(&options);
	if (agent_conn == NULL)
		goto out0;

	if (our_migrate_id(agent_conn, event_cm_id, connect_info, &options) != 0)
		goto out1;

	/* set up a new thread to handle agent's cm events */
	if (our_create_cm_event_thread(agent_conn, &options) != 0)
		goto out1a;

	if (our_setup_qp(agent_conn, agent_conn->cm_id, &options) != 0)
		goto out2;

	if (our_setup_agent_buffers(agent_conn, &options) != 0)
		goto out3;

	/* post first receive for remote buffer info on the agent_conn */
	if (our_post_recv(agent_conn,
		&agent_conn->remote_buffer_info_work_request, &options) != 0)
		goto out4;

	if (our_agent_connect(agent_conn, &options) != 0)
		goto out4;

	our_trace_ptr("Agent", "accepted our_control", agent_conn, &options);

	/* the agent now subscribes to data published by the client */
	if (our_agent_operation(agent_conn, &options) != 0) {
		goto out5;
	}

	/* the agent finished successfully, continue into tear-down phase */
	result = EXIT_SUCCESS;
out5:
	our_disconnect(agent_conn, &options);
out4:
	our_unsetup_buffers(agent_conn, &options);
out3:
	our_unsetup_qp(agent_conn, &options);
out2:
	our_destroy_cm_event_thread(agent_conn, &options);
out1a:
	our_destroy_id(agent_conn, &options);
out1:
	our_destroy_control_struct(agent_conn, &options);
out0:
	free(connect_info);
	return result;
}	/* our_agent */


/* this main program is really the server-listener */
int
main(int argc, char *argv[])
{
	struct our_control	*listen_conn;
	struct our_options	*options;
	struct rdma_cm_id	*event_cm_id;
	struct our_connect_info	*connect_info;
	int			result;

	/* assume there is an error somewhere along the line */
	result = EXIT_FAILURE;

	/* process the command line options -- don't go on if any errors */
	options = our_process_options(argc, argv);
	if (options == NULL) {
		goto out0;
	}

	/* allocate our own control structure for listener's connection */
	listen_conn = our_create_control_struct(options);
	if (listen_conn == NULL) {
		goto out1;
	}

	if (our_create_id(listen_conn, options) != 0)
		goto out2;

	/* set up a separate thread to handle cm events */
	if (our_create_cm_event_thread(listen_conn, options) != 0)
		goto out2a;

	if (our_listener_bind(listen_conn, options) != 0)
		goto out3;

	/* listener all setup, just wait for a client to request a connect */
	if (our_await_cm_event(listen_conn, RDMA_CM_EVENT_CONNECT_REQUEST,
			"listener", &event_cm_id, &connect_info, options) != 0)
		goto out3;
	
	/* hand the client's request over to a new agent */
	if (our_agent(event_cm_id, connect_info, options) != 0)
		goto out3;

	/* the agent finished successfully, continue into tear-down phase */
	result = EXIT_SUCCESS;
out3:
	our_destroy_cm_event_thread(listen_conn, options);
out2a:
	our_destroy_id(listen_conn, options);
out2:
	our_destroy_control_struct(listen_conn, options);
out1:
	our_unprocess_options(options);
out0:
	exit(result);
}	/* main */
