/* prototypes.h */
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


#ifndef _PROTOTYPES_H_
#define _PROTOTYPES_H_

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <ctype.h>
#include <string.h>
#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/param.h>
#include <netdb.h>
#include <rdma/rdma_cma.h>
#include <inttypes.h>
#include <infiniband/arch.h>
#include <pthread.h>
#include <semaphore.h>


#define WHITE_SPACE " \n\r\t\v\f"

#define _MAX_STRERROR_MALLOC	64

#define OUR_SQ_DEPTH	32
#define OUR_RQ_DEPTH	32

/* our default port number if user doesn't supply one on the command line */
#define OUR_DEFAULT_PORT	12321

/* our "listen" backlog if user doesn't supply one on the command line */
#define OUR_BACKLOG		3

/* default number of iterations */
#define OUR_DEFAULT_LIMIT	500000

/* default size of data buffer transfered in one message */
#define OUR_DEFAULT_DATA_SIZE	100000

/* fixed size of ack transfered in one message */
#define OUR_ACK_SIZE		8

/* maximum number of ack messages to allocate */
#define OUR_MAX_ACK_MESSAGES	3

/* default number of data buffers to allocate */
#define OUR_N_DATA_BUFFERS	4

/* maximum number of user data buffers to allocate */
#define OUR_MAX_USER_DATA_BUFFERS	24

/* maximum number of user send/recv work requests to allocate */
#define OUR_MAX_WORK_REQUESTS	OUR_MAX_USER_DATA_BUFFERS

/* bits in our flags word */
#define VERIFY		0x01
#define TRACING		0x02
#define VERBOSE_TRACING	0x04
#define PRINT_STATS	0x08

struct our_options {
	char		*server_name;
	char		*server_port;
	char		*message;
	unsigned long	server_port_number;
	uint64_t	limit;
	uint64_t	data_size;
	uint64_t	n_data_buffers;
	int		send_queue_depth;
	int		recv_queue_depth;
	int		max_send_sge;
	int		max_recv_sge;
	unsigned int	flags;
};	/* struct our_options */


/* structure to hold private data client sends to server on a connect */
struct our_connect_info {
	uint64_t	remote_limit;
	uint64_t	remote_data_size;
	uint64_t	remote_n_data_buffers;
};


/* this union holds info to be exchanged as an ack message */
union our_ack_info {
	uint64_t	ack_count;
	char		ack_data[OUR_ACK_SIZE];
};


/* this structure heads a list of linked send work requests */
struct our_send_work_request_list {
	struct ibv_send_wr		*head;
	struct ibv_send_wr		*tail;
	unsigned int			size;
};


/* this structure holds all info relevant to a single connection,
 * whether client, listener or agent
 */
struct our_control {

/***** this part contains fields for general info *****/

	struct timespec		start_time;
	struct timespec		stop_time;
	struct rusage		start_usage;
	struct rusage		stop_usage;
	uint64_t		wc_send;
	uint64_t		wc_recv;

/***** this part contains fields for CM and completions interaction *****/

	/* connection manager event channel */
	struct rdma_event_channel	*cm_event_channel;

	/* connection manager id */
	struct rdma_cm_id		*cm_id;

	/* set to 1 when rdma_disconnect() has been called */
	volatile int			disconnected;

	/* protection domain */
	struct ibv_pd			*protection_domain;

	/* completion queue */
	struct ibv_cq			*completion_queue;

/* fields for catching cm events using asynchronous thread */

	/* lock to protect asynchronous access to the cm event fields */
	pthread_mutex_t			cm_event_info_lock;

	/* condition variable to wait for cm event fields to change */
	pthread_cond_t			cm_event_info_notify;

	/* non-zero when thread is waiting on cm_event_info_notify */
	unsigned int			waiting_on_info_notify;

	/* holds type of latest asynch cm event reported by cm */
	enum rdma_cm_event_type		latest_cm_event_type;

	/* holds status of latest asynch cm event reported by cm */
	int				latest_status;

	/* holds cm_id from latest asynch cm event reported by cm */
	struct rdma_cm_id		*latest_cm_id;

	/* holds private_data_len from latest asynch cm event reported by cm */
	int				latest_private_data_len;

	/* holds private_data from latest asynch cm event reported by cm */
	struct our_connect_info		latest_private_data;

	/* holds type of current asynch cm event processed by mainline */
	enum rdma_cm_event_type		current_cm_event_type;

	/* asynchronous thread to handle cm_events */
	pthread_t			cm_event_thread_id;

/* fields added to get statistics on completion event handling */

	/* array of work completions */
	struct ibv_wc			*work_completion;

	/* number of elements allocated in work_completion array */
	int				max_n_work_completions;

	/* current number of polled elements in work_completion array */
	int				current_n_work_completions;

	/* index of next polled but not-dealt-with element in work_completion */
	int				index_work_completions;

	/* dynamically allocated array to hold histogram of number of
	 * completions returned on each call to ibv_poll_cq()
	 */
	unsigned long			*completion_stats;

/***** this part contains fields for data setup and transfer *****/

	/* queue pair */
	struct ibv_qp		*queue_pair;

	/* dynamically allocated space for user_data buffers */
	unsigned char		*user_data[OUR_MAX_USER_DATA_BUFFERS];

	/* memory registration pointers for user_data buffers */
	struct ibv_mr		*user_data_mr[OUR_MAX_USER_DATA_BUFFERS];

	/* scatter-gather element array for user_data send/recv operations */
	struct ibv_sge		user_data_sge[OUR_MAX_USER_DATA_BUFFERS];

	/* work requests for user_data send operations */
	struct ibv_send_wr	user_data_send_work_request[OUR_MAX_WORK_REQUESTS];

	/* work requests for user_data recv operations */
	struct ibv_recv_wr	user_data_recv_work_request[OUR_MAX_WORK_REQUESTS];

	/* number of buffers actually allocated and registered in
	 * user_data and user_data_mr arrays
	 * (used only to know how many to deregister and deallocate)
	 */
	int			n_user_data_bufs;


/* this part contains structures needed to receive ACKs */

	/* memory where ACKs are received into */
	union our_ack_info	recv_ack[OUR_MAX_ACK_MESSAGES];

	/* holds memory registration pointers for receive ack areas */
	struct ibv_mr		*recv_ack_mr[OUR_MAX_ACK_MESSAGES];

	/* work requests for receive ack operations */
	struct ibv_recv_wr	recv_ack_work_request[OUR_MAX_ACK_MESSAGES];

	/* scatter-gather element array for recv ack operations */
	struct ibv_sge		recv_ack_sge[OUR_MAX_ACK_MESSAGES];

/* this part contains structures needed to send ACKs */

	/* memory where ACKs are sent from */
	union our_ack_info	send_ack[OUR_MAX_ACK_MESSAGES];

	/* holds memory registration pointers for send ack areas */
	struct ibv_mr		*send_ack_mr[OUR_MAX_ACK_MESSAGES];

	/* work requests for send ack operations */
	struct ibv_send_wr	send_ack_work_request[OUR_MAX_ACK_MESSAGES];

	/* scatter-gather element array for send ack operations */
	struct ibv_sge		send_ack_sge[OUR_MAX_ACK_MESSAGES];

/* this part contains structures needed to keep chained work request stats */

	/* counts number of successful send calls to ibv_post_send() */
	unsigned long		send_stats;

	/* array to hold histogram of number of chained send work requests
	 * posted on each call to ibv_post_send()
	 */
	unsigned long		wr_chain_stats[OUR_MAX_WORK_REQUESTS+1];

};	/* struct our_control */


/***** this part contains miscellaneous functions for general processing *****/

extern void *
our_calloc(unsigned long size, const char *message);


/* Scans string pointed to by value and converts it to positive unsigned long.
 * Returns 0 if successful (and stores the number in result),
 *	  -1 on any error (prints an error message and leaves result unchanged)
 */
extern int
our_scan_switch_ulong(int switch_char, char *value, unsigned long *result,
			const char *message);


/* Scans string pointed to by value and converts it to uint64_t.
 * Returns == 0 if successful (and stores the number in result),
 *	   != 0 on error (and error message has been given, result is unchanged)
 */
extern int
our_scan_switch_uint64(int switch_char, char *value, uint64_t *result,
			const char *message);


extern void
our_report_error(int ret, const char *verb_name, struct our_options *options);

/* on entry, ret is known to be != 0
 *
 * Returns == 0 if ibv_wc_status message was printed (ret was valid status code)
 *	   != 0 otherwise
 */
extern int
our_report_wc_status(int ret, const char *verb_name, struct our_options *options);

extern void
our_report_ok(const char *verb_name, struct our_options *options);

extern void
our_report_ptr(const char *verb_name, const char *ptr_name, void *ptr,
			struct our_options *options);

extern void
our_report_ulong(const char *verb_name, const char *number_name,
			unsigned long number, struct our_options *options);

extern void
our_report_uint64(const char *verb_name, const char *number_name,
			uint64_t number, struct our_options *options);

extern void
our_report_string(const char *verb_name, const char *string_name,
			const char *string, struct our_options *options);

extern void
our_trace_error(int ret, const char *verb_name, struct our_options *options);

extern void
our_trace_ok(const char *verb_name, struct our_options *options);

extern void
our_trace_ptr(const char *verb_name, const char *ptr_name, void *ptr,
			struct our_options *options);

extern void
our_trace_ulong(const char *verb_name, const char *number_name,
			unsigned long number, struct our_options *options);

extern void
our_trace_uint64(const char *verb_name, const char *number_name,
			uint64_t number, struct our_options *options);

extern void
our_trace_string(const char *verb_name, const char *string_name,
			const char *string, struct our_options *options);

extern void
our_get_current_time(struct timespec *current);

extern void
our_get_current_usage(struct rusage *current);

extern void
our_print_statistics(struct our_control *conn, struct our_options *options);

/* processes command line options (switches)
 * returns != NULL if all ok, (pointer is to newly allocated struct our_options)
 *		and the option fields server_name, server_port, and message
 *		have been filled in.
 *	   == NULL on error (and error message has been given)
 */
extern struct our_options *
our_process_options(int argc, char *argv[]);


/* free up storage allocated in processing options */
extern void
our_unprocess_options(struct our_options *options);


/***** this part contains functions for CM and completions interaction *****/

/*
 * create a communication identifier used to identify which
 * RDMA device a cm event is being reported about
 *
 * returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
extern int
our_create_id(struct our_control *conn, struct our_options *options);


/* already have a communication identifier,
 * just copy it and set its context to be this new conn
 *
 * returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
extern int
our_migrate_id(struct our_control *conn, struct rdma_cm_id *new_cm_id,
		struct our_connect_info *connect_info,
		struct our_options *options);


/* release a communication identifier, canceling any outstanding
 * asynchronous operation on it.
 *
 * returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
extern int
our_destroy_id(struct our_control *conn, struct our_options *options);

/* called to wait for next cm event on this conn's cm_id->channel
 * returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
extern int
our_await_cm_event(struct our_control *conn,
		enum rdma_cm_event_type this_event_type,
		char *name,
		struct rdma_cm_id **cm_id,
		struct our_connect_info **connect_info,
		struct our_options *options);


/* new functions added for catching cm events using asynchronous thread */

/* returns == 0 if cm_event_thread_function spawned ok
	   != 0 on any error
*/
extern int
our_create_cm_event_thread(struct our_control *conn,
			   struct our_options *options);


extern int
our_destroy_cm_event_thread(struct our_control *conn,
				struct our_options *options);


/* returns == 0 if work_completion has status == 0 (no error)
 *	   != 0 on error (and error message has been given)
 */
extern int
our_check_completion_status(struct our_control *conn,
			   struct ibv_wc *work_completion,
			   struct our_options *options);


/* returns == 0 if at least 1 wc in conn's work_completion array
 *	   != 0 on error (and error message has been given)
 */
extern int
our_fill_completion_array(struct our_control *conn,
			   struct our_options *options);


/* returns == 0 if filled work_completion with status == 0 (no error)
 *	   != 0 on error (and error message has been given)
 */
extern int
our_await_completion(struct our_control *conn,
		struct ibv_wc **work_completion,
		struct our_options *options);

/* add this send work request to end of linked list */
extern void
our_add_to_work_request_list(struct our_send_work_request_list *list,
		struct ibv_send_wr *work_request, struct our_options *options);


/* remove and return first item from send work request list
 * return NULL if list is empty
 */
extern struct ibv_send_wr *
our_get_from_work_request_list(struct our_send_work_request_list *list,
				struct our_options *options);

extern void
print_ibv_poll_cq_stats(struct our_control *conn, struct our_options *options);

extern int
our_client_bind(struct our_control *client_conn, struct our_options *options);

extern int
our_listener_bind(struct our_control *listen_conn, struct our_options *options);

extern struct our_control *
our_agent_setup(struct our_control *listen_conn, struct our_options *options);

extern void
our_agent_unsetup(struct our_control *agent_conn, struct our_options *options);

extern int
our_client_connect(struct our_control *client_conn, struct our_options *options);

extern int
our_agent_connect(struct our_control *agent_conn, struct our_options *options);

extern int
our_disconnect(struct our_control *conn, struct our_options *options);


/***** this part contains functions needed for data setup and transfer *****/

extern struct our_control *
our_create_control_struct(struct our_options *options);

extern void
our_destroy_control_struct(struct our_control *conn,
			   struct our_options *options);

extern int
our_setup_qp(struct our_control *conn, struct rdma_cm_id *cm_id,
		struct our_options *options);

extern void
our_unsetup_qp(struct our_control *conn, struct our_options *options);

extern int
our_setup_client_buffers(struct our_control *conn, struct our_options *options);

extern int
our_setup_agent_buffers(struct our_control *conn, struct our_options *options);

extern void
our_unsetup_buffers(struct our_control *conn, struct our_options *options);

extern int
our_post_send(struct our_control *conn, struct ibv_send_wr *work_request,
					struct our_options *options);

extern int
our_post_recv(struct our_control *conn, struct ibv_recv_wr *work_request,
					struct our_options *options);

extern int
our_agent_operation(struct our_control *agent_conn,struct our_options *options);

#endif	/* _PROTOTYPES_H_ */
