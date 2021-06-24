/* process_cm_events.c */
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



/* called to wait for next cm event on this conn's cm_id->channel
 * returns == 0 if all ok,
 *	   != 0 on error, including EAGAIN (and error mesage has been given)
 */
int
our_try_get_cm_event(struct our_control *conn,
		enum rdma_cm_event_type this_event_type,
		char *name,
		struct rdma_cm_id **cm_id,
		struct our_connect_info **connect_info,
		struct our_options *options)
{
	struct rdma_cm_event	*cm_event;
	int			ret;

	errno = 0;
	ret = rdma_get_cm_event(conn->cm_id->channel, &cm_event);

	/* cm_id->channel now non-blocking, so just leave if no event found */
	if (ret != 0) {
		if (errno != EAGAIN)
			our_report_error(ret, "rdma_get_cm_event", options);
		goto out0;
	}
	if (cm_event->event == RDMA_CM_EVENT_DISCONNECTED
						&& conn->disconnected == 0) {
		/* remote side disconnected before local side
		 * we need to do it here to force "flushed" error
		 * on our local posting
		 */
		our_trace_ptr("our_try_get_cm_event", "disconnecting cm_id",
				conn->cm_id, options);
		conn->disconnected = 1;
		errno = 0;
		ret = rdma_disconnect(conn->cm_id);
		if (ret == 0) {
			our_report_ptr("our_try_get_cm_event",
					"disconnected cm_id",
					conn->cm_id, options);
		} else if (errno != EINVAL) {
			our_report_error(ret,"rdma_disconnect",options);
		} else {
			ret = 0;
		}
	}
	if (cm_event->event != this_event_type) {
		fprintf(stderr, "%s: %s unexpected cm event %d (%s)\n",
			options->message, name,
			cm_event->event, rdma_event_str(cm_event->event));
		ret = -1;
	} else if (ret == 0) {
		if (options->flags & TRACING) {
			fprintf(stderr, "%s: %s got cm event %d (%s) cm_id %p "
				"our_control %p status %d disconnected %d\n",
				options->message, name,
				cm_event->event,rdma_event_str(cm_event->event),
				cm_event->id, conn, cm_event->status,
				conn->disconnected);
		}
		if (options->flags & VERBOSE_TRACING) {
			our_report_ulong("rdma_get_cm_event","private_data_len",
				cm_event->param.conn.private_data_len, options);
		}
		if (cm_id != NULL && connect_info != NULL) {
			/* called by listener, pass back client's private data */
			*cm_id = cm_event->id;
			if (cm_event->param.conn.private_data_len >=
					sizeof(**connect_info)) {
				*connect_info
					= our_calloc(sizeof(**connect_info),
								"connect_info");
				if (*connect_info != NULL) {
					memcpy(*connect_info,
						cm_event->param.conn.private_data,
						sizeof(**connect_info));
				}
			} else {
				our_report_ulong("private_data_len","too small",
					cm_event->param.conn.private_data_len,
					options);
				ret = EPROTO;	/* Protocol error */
				our_report_error(ret, "rdma_get_cm_event", 
								options);
			}
		}
	}

	/* all cm_events returned by rdma_get_cm_event() MUST be acknowledged */
	rdma_ack_cm_event(cm_event);
	errno = 0;
out0:
	return ret;
}	/* our_try_get_cm_event */


/* called to wait for next cm event on this conn's cm_id->channel
 * returns == 0 if all ok,
 *	   != 0 on any error
 */
int
our_await_cm_event(struct our_control *conn,
		enum rdma_cm_event_type this_event_type,
		char *name,
		struct rdma_cm_id **cm_id,
		struct our_connect_info **connect_info,
		struct our_options *options)
{
	int			ret;

	if (options->flags & TRACING) {
		fprintf(stderr,
			"%s: %s awaiting next cm event %d (%s) our_control %p\n",
			options->message, name,
			this_event_type, rdma_event_str(this_event_type), conn);
	}
	if (options->flags & VERBOSE_TRACING) {
		our_report_ptr("our_await_cm_event", "current cm_id -> channel",
						conn->cm_id->channel, options);
	}
	do {
		ret = our_try_get_cm_event(conn, this_event_type, name, cm_id,
							connect_info, options);
	} while (errno == EAGAIN);
	return ret;
}	/* our_await_cm_event */
