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
 *	   != 0 on any error
 */
int
our_await_cm_event(struct our_control *conn,
		enum rdma_cm_event_type this_event_type,
		char *name,
		struct rdma_cm_id **cm_id,
		struct our_options *options)
{
	struct rdma_cm_event	*cm_event;
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

	/* block until we get a cm_event from the communication manager */
	errno = 0;
	ret = rdma_get_cm_event(conn->cm_id->channel, &cm_event);
	if (ret != 0) {
		our_report_error(ret, "rdma_get_cm_event", options);
		goto out0;
	}
	if (options->flags & TRACING) {
		fprintf(stderr, "%s: %s got cm event %d (%s) cm_id %p "
				"our_control %p status %d\n",
				options->message, name,
				cm_event->event,rdma_event_str(cm_event->event),
				cm_event->id, conn, cm_event->status);
	}

	if (cm_event->event != this_event_type) {
		fprintf(stderr, "%s: %s expected cm event %d (%s)\n",
			options->message, name,
			this_event_type, rdma_event_str(this_event_type));
		ret = -1;
	} else {
		if (cm_id != NULL) {
			*cm_id = cm_event->id;
		}
	}

	/* all cm_events returned by rdma_get_cm_event() MUST be acknowledged */
	rdma_ack_cm_event(cm_event);
out0:
	return ret;
}	/* our_await_cm_event */
