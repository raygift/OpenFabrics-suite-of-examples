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


struct cm_event_thread_params {

	/* to synchronize thread start-up with main-line thread */
	sem_t				sem;

	/* our master control block */
	struct our_control		*conn;

	/* connection manager event channel on which to get events */
	struct rdma_event_channel	*cm_event_channel;

	/* general options we are running under */
	struct our_options		*options;
};


/* this is the asynchronous thread that waits for cm events from the cm
 * and relays them back to the appropriate mainline thread
 */
static void *
our_cm_event_thread(void *param)
{
	struct cm_event_thread_params	*params;
	struct rdma_event_channel	*cm_event_channel;
	struct our_options		*options;
	struct rdma_cm_event		*cm_event;
	struct rdma_cm_id		*cm_id;
	struct our_control		*conn;
	enum rdma_cm_event_type		this_event_type;
	int				this_event_status;
	int				ret, disconnected, give_signal;

	/* extract the parameters from short-lived param structure */
	params = param;
	conn = params->conn;
	cm_event_channel = params->cm_event_channel;
	options = params->options;

	/* signal main-line that we are alive and kicking */
	errno = 0;
	ret = sem_post(&params->sem);
	if (ret != 0) {
		our_report_error(ret, "sem_post", options);
		return (void *)(-1);
	}

	do {
		/* wait for the next event from cm on this cm_event_channel */
		errno = 0;
		ret = rdma_get_cm_event(cm_event_channel, &cm_event);
		if (ret != 0) {
			/* this is really fatal */
			our_report_error(ret, "rdma_get_cm_event", options);
			exit(EXIT_FAILURE);
		}

		/* synch with main-line thread on who calls rdma_disconnect() */
		pthread_mutex_lock(&conn->cm_event_info_lock);

		/* extract the cm_id for which this event is being reported */
		cm_id = cm_event->id;

		/* extract the type of event this was */
		this_event_type = cm_event->event;

		disconnected = conn->disconnected;
		if (this_event_type == RDMA_CM_EVENT_DISCONNECTED
							&& disconnected == 0) {
				/* main-line did not make call, we must do it */
				conn->disconnected = 1;
			}

		/* extract the status of this event */
		this_event_status = cm_event->status;

		/* put this event where main thread for this conn can find it */
		conn->latest_cm_event_type = this_event_type;
		conn->latest_status = this_event_status;
		conn->latest_cm_id = cm_id;
		conn->latest_private_data_len
					= cm_event->param.conn.private_data_len;
		if (conn->latest_private_data_len > 0) {
			/* event has private data, save it in conn */
			if (conn->latest_private_data_len
					>= sizeof(struct our_connect_info)) {
				memcpy(&conn->latest_private_data,
					cm_event->param.conn.private_data,
					sizeof(struct our_connect_info));
			} else {
				our_report_ulong("private_data_len","too small",
					conn->latest_private_data_len, options);
				ret = EPROTO;	/* Protocol error */
				our_report_error(ret, "rdma_get_cm_event", 
								options);
			}
		}

		give_signal = conn->waiting_on_info_notify;

		pthread_mutex_unlock(&conn->cm_event_info_lock);

		/* all cm_events returned by rdma_get_cm_event() MUST be acked*/
		rdma_ack_cm_event(cm_event);

		/* tell anyone waiting that conditions have changed */
		if (give_signal != 0) {
			pthread_cond_signal(&conn->cm_event_info_notify);
		}

		if (options->flags & TRACING) {
			fprintf(stderr,
				"%s: %s got cm event %d (%s) cm_id %p "
				"our_control %p status %d disconnected %d\n",
				options->message, "cm_event_thread",
				this_event_type,rdma_event_str(this_event_type),
				cm_id, conn, this_event_status, disconnected);
		}

		/* check the conn to which this cm_id belongs */
		if (conn != cm_id->context) {
			our_report_ptr("cm_event_thread",
					"WARNING: conn", conn, options);
			our_report_ptr("cm_event_thread",
					"WARNING: cm_id->context",
					cm_id->context, options);
		}

		if (this_event_type == RDMA_CM_EVENT_DISCONNECTED) {
			if (disconnected == 0) {
				/* remote side disconnected before local side,
			 	* we need to do it here to force "flushed" error
			 	* on our local postings
				 */
				our_trace_ptr("cm_event_thread",
						"disconnecting cm_id",
						cm_id, options);
				errno = 0;
				ret = rdma_disconnect(cm_id);
				if (ret == 0 || errno == EINVAL) {
					our_report_ptr("cm_event_thread",
							"disconnected cm_id",
							cm_id, options);
				} else {
					our_report_error(ret, "rdma_disconnect",
								options);
				}
			} /* else already flushed by remote */
			break;
		}
	} while (1);
	our_trace_ulong("cm_event_thread", "exited cm_event_thread_id",
			conn->cm_event_thread_id, options);
	return NULL;
}	/* our_cm_event_thread */


/* returns == 0 if our_cm_event_thread spawned ok
 *	   != 0 on any error
 */
int
our_create_cm_event_thread(struct our_control *conn,
				struct our_options *options)
{
	struct cm_event_thread_params	*params;
	struct timeval			cur_time;
	struct timespec			deadline;
	int				ret;

	/* build short-lived structure to pass parameters to new thread */
	params = our_calloc(sizeof(*params), options->message);
	if (params == NULL) {
		ret = ENOMEM;
		goto out0;
	}
	params->conn = conn;
	params->cm_event_channel = conn->cm_id->channel;
	params->options = options;
	errno = 0;
	ret = sem_init(&params->sem, 0, 0);
	if (ret != 0) {
		our_report_error(ret, "sem_init", options);
		goto out1;
	}

	/* create the new thread to handle cm_events on this cm_event_channel */
	errno = 0;
	ret = pthread_create(&conn->cm_event_thread_id, NULL, 
						our_cm_event_thread, params);
	if (ret != 0) {
		our_report_error(ret, "pthread_create", options);
		goto out2;
	}

	our_trace_ulong("pthread_create", "created cm_event_thread_id",
			conn->cm_event_thread_id, options);

	/* wait at most 3 seconds for thread to get going */
	gettimeofday(&cur_time, NULL);
	deadline.tv_sec = cur_time.tv_sec + 3;
	deadline.tv_nsec = cur_time.tv_usec * 1000;
	
	errno = 0;
	ret = sem_timedwait(&params->sem, &deadline);
	if (ret != 0) {
		our_report_error(ret, "sem_wait", options);
		our_destroy_cm_event_thread(conn, options);
	}

out2:
	if (sem_destroy(&params->sem) != 0)
		our_report_error(-1, "sem_destroy", options);
out1:
	free(params);
out0:
	return ret;
}	/* our_create_cm_event_thread */


int
our_destroy_cm_event_thread(struct our_control *conn,
				struct our_options *options)
{
	void	*result;
	int	ret;

	/* see if the cm_event_thread already disconnected */
	pthread_mutex_lock(&conn->cm_event_info_lock);
	if (conn->disconnected == 0) {
		/* we got here before the cm_event_thread, cancel the thread */
		errno = 0;
		ret = pthread_cancel(conn->cm_event_thread_id);
		if (ret != 0) {
			our_report_error(ret, "pthread_cancel", options);
		} else {/* no need to call rdma_disconnect() */
			conn->disconnected = 4;
			our_trace_ulong("pthread_cancel",
					"canceled cm_event_thread_id",
					conn->cm_event_thread_id, options);
		}
	}
	pthread_mutex_unlock(&conn->cm_event_info_lock);

	errno = 0;
	ret = pthread_join(conn->cm_event_thread_id, &result);
	if (ret != 0) {
		our_report_error(ret, "pthread_join", options);
	} else {
		our_trace_ptr("pthread_join","returned status",result,options);
	}
	return ret;
}	/* our_destroy_cm_event_thread */


/* called by main-line thread to wait for asynchronous thread to
 * relay the latest cm event on this conn's cm_event_channel
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
	enum rdma_cm_event_type	new_event_type;
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

	/* wait for our asynchronous thread to detect a new event type */
	pthread_mutex_lock(&conn->cm_event_info_lock);

	do	{
		new_event_type = conn->latest_cm_event_type;
		if (new_event_type == RDMA_CM_EVENT_DISCONNECTED
			|| new_event_type != conn->current_cm_event_type) {
			break;
		}
		conn->waiting_on_info_notify++;
		pthread_cond_wait(&conn->cm_event_info_notify,
						&conn->cm_event_info_lock);
		conn->waiting_on_info_notify--;
	} while (1);

	pthread_mutex_unlock(&conn->cm_event_info_lock);
	if (options->flags & TRACING) {
		fprintf(stderr, "%s: %s got cm event %d (%s) cm_id %p "
				"our_control %p status %d\n",
				options->message, name,
				new_event_type, rdma_event_str(new_event_type),
				conn->latest_cm_id, conn, conn->latest_status);
	}

	if ((ret = conn->latest_status) != 0) {
		errno = 0;
		our_report_error(ret, rdma_event_str(this_event_type), options);
	} else if (new_event_type != this_event_type) {
		fprintf(stderr, "%s: %s expected cm event %d (%s)\n",
			options->message, name,
			this_event_type, rdma_event_str(this_event_type));
		ret = -1;
	} else {
		if (options->flags & VERBOSE_TRACING) {
			our_report_ulong("rdma_get_cm_event","private_data_len",
				conn->latest_private_data_len, options);
		}
		conn->current_cm_event_type = new_event_type;
		if (cm_id != NULL && connect_info != NULL) {
			/* called by listener, pass back client's private data*/
			*cm_id = conn->latest_cm_id;
			if (conn->latest_private_data_len >=
					sizeof(**connect_info)) {
				*connect_info
					= our_calloc(sizeof(**connect_info),
								"connect_info");
				if (*connect_info != NULL) {
					memcpy(*connect_info,
						&conn->latest_private_data,
						sizeof(**connect_info));
				}
			} else {
				our_report_ulong("our_await_cm_event",
					"latest_private_data_len too small",
					conn->latest_private_data_len, options);
				ret = EPROTO;	/* Protocol error */
				our_report_error(ret, "our_await_cm_event",
								options);
			}
		}
	}
	return ret;
}	/* our_await_cm_event */
