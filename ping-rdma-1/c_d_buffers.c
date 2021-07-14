/* c_d_buffers.c */
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


static void
our_print_buffer_numbers(struct our_control *conn, const char *name,
						struct our_options *options)
{
	/* print out our buffer allocation numbers */
	our_trace_ulong(name, "n_user_data_bufs",
					conn->n_user_data_bufs, options);
}	/* our_print_buffer_numbers */


/* register a memory addr of length bytes for appropriate access
 *
 * returns != NULL if all ok,
 *	   == NULL on error (and error message has been given)
 */
static struct ibv_mr *
our_setup_mr(struct our_control *conn, void *addr, unsigned int length,
					int access, struct our_options *options)
{
	struct ibv_mr	*mr;

	errno = 0;
	mr = ibv_reg_mr(conn->protection_domain, addr, length, access);
	if (mr == NULL) {
		our_report_error(ENOMEM, "ibv_reg_mr", options);
	}
	return mr;
}	/* our_setup_mr */


/* unregister a memory area that was previously registered */
static void
our_unsetup_mr(struct our_control *conn, struct ibv_mr **mr, int max_slots,
		  				struct our_options *options)
{
	struct ibv_mr	**ptr;
	int		ret, i;

	ptr = mr;
	for (i = 0; i < max_slots; i++) {
		if (*ptr != NULL) {
			errno = 0;
			ret = ibv_dereg_mr(*ptr);
			if (ret != 0) {
				our_report_error(ret, "ibv_dereg_mr", options);
			}
			*ptr = NULL;
		}
		ptr++;
	}
}	/* our_unsetup_mr */


/* fill in scatter-gather element for length bytes at registered addr */
static void
our_setup_sge(void *addr, unsigned int length, unsigned int lkey,
		struct ibv_sge *sge)
{
	/* point at the memory area */
	sge->addr = (uint64_t)(unsigned long)addr;

	/* set the number of bytes in that memory area */
	sge->length = length;

	/* set the registration key for that memory area */
	sge->lkey = lkey;
}	/* our_setup_sge */


/* register a memory addr of length bytes for appropriate access
 * and fill in scatter-gather element for it
 */
static int
our_setup_mr_sge(struct our_control *conn, void *addr, unsigned int length,
		int access, struct ibv_mr **mr, struct ibv_sge *sge,
		struct our_options *options)
{
	/* register the address for appropriate access */
	*mr = our_setup_mr(conn, addr, length, access, options);
	if (*mr == NULL) {
		return -1;
	}

	/* fill in the fields of a single scatter-gather element */
	our_setup_sge(addr, length, (*mr)->lkey, sge);
	return 0;
}	/* our_setup_mr_sge */


/* fill in_send work_request for opcode operation
 * on n_sges scatter-gather elements in sg_list array
 */
static void
our_setup_send_wr(struct our_control *conn, struct ibv_sge *sg_list,
		enum ibv_wr_opcode opcode, int n_sges,
		struct ibv_send_wr *send_work_request)
{
	/* set the user's identification to be our conn pointer */
	send_work_request->wr_id = (uint64_t)(unsigned long)conn;

	/* not chaining this work request to other work requests */
	send_work_request->next = NULL;

	/* point at array of scatter-gather elements for this send */
	send_work_request->sg_list = sg_list;

	/* number of scatter-gather elements in array actually being used */
	send_work_request->num_sge = n_sges;

	/* the type of send */
	send_work_request->opcode = opcode;

	/* set SIGNALED flag so every send generates a completion
	 * (if we don't do this, then the send queue fills up!)
	 */
	send_work_request->send_flags = IBV_SEND_SIGNALED;

	/* not sending any immediate data */
	send_work_request->imm_data = 0;
}	/* our_setup_send_wr */


/* for n_sges
 *	register a memory addr of length bytes for appropriate access
 *	fill in scatter-gather element sge for it
 * then fill in send work_request for opcode operation
 * on n_sges scatter-gather elements in sge array
 */
static int
our_setup_send(struct our_control *conn, void *addr, unsigned int length,
		int access, struct ibv_mr *mr[], struct ibv_sge sge[],
		enum ibv_wr_opcode opcode, int n_sges,
		struct ibv_send_wr *send_work_request,
		struct our_options *options)
{
	int	ret, i;
	char	*ptr;

	/* setup the addresses for appropriate access */
	ptr = addr;
	for (i = 0; i < n_sges; i++) {
		ret = our_setup_mr_sge(conn, ptr, length, access, &mr[i],
							&sge[i], options);
		if (ret != 0) {
			goto err1;
		}
		ptr += length;
	}

	/* fill in the fields of the send work request */
	our_setup_send_wr(conn, sge, opcode, n_sges, send_work_request);
	return 0;
err1:
	our_unsetup_mr(conn, mr, i, options);
	return ret;
}	/* our_setup_send */


/* fill in recv_work_request for recv operation
 * on n_sges scatter-gather elements in sg_list array
 */
static void
our_setup_recv_wr(struct our_control *conn, struct ibv_sge *sg_list,
		int n_sges, struct ibv_recv_wr *recv_work_request)
{
	/* set the user's identification to be our conn pointer */
	recv_work_request->wr_id = (uint64_t)(unsigned long)conn;

	/* not chaining this work request to other work requests */
	recv_work_request->next = NULL;

	/* point at array of scatter-gather elements for this recv */
	recv_work_request->sg_list = sg_list;

	/* number of scatter-gather elements in array actually being used */
	recv_work_request->num_sge = n_sges;

}	/* our_setup_recv_wr */


/* register a memory addr of length bytes for IBV_ACCESS_LOCAL_WRITE
 * fill in scatter-gather element sge for it
 * then fill in recv_work_request for recv operation
 * on n_sges scatter-gather elements in sge array
 */
static int
our_setup_recv(struct our_control *conn, void *addr, unsigned int length,
		struct ibv_mr *mr[], struct ibv_sge sge[], int n_sges,
		struct ibv_recv_wr *recv_work_request,
		struct our_options *options)
{
	int	ret, i;
	char	*ptr;

	/* setup the addresses for appropriate access */
	ptr = addr;
	for (i = 0; i < n_sges; i++) {
		ret = our_setup_mr_sge(conn, ptr, length, IBV_ACCESS_LOCAL_WRITE,
						&mr[i], &sge[i], options);
		if (ret != 0) {
			goto err1;
		}
		ptr += length;
	}

	/* fill in the fields of the recv work request */
	our_setup_recv_wr(conn, sge, n_sges, recv_work_request);
	return 0;
err1:
	our_unsetup_mr(conn, mr, i, options);
	return ret;
}	/* our_setup_recv */


/* deals with "n_user_bufs" user_data buffers
 * for each buffer
 *	allocates space for user_data and registers it
 *	then fills in user_data_mr, user_data_sge
 *
 * when called MUST have n_user_bufs <= OUR_MAX_USER_DATA_BUFFERS
 *
 * returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
 */
static int
our_setup_user_data(struct our_control *conn, int n_user_bufs, int access[],
		  struct our_options *options)
{
	int	ret, i;


	/* count number of user_data_bufs actually allocated and set up */
	conn->n_user_data_bufs = 0;
	for (i = 0; i < n_user_bufs; i++) {

		/* allocate space to hold user data, plus 1 for '\0' */
		conn->user_data[i] = our_calloc(options->data_size + 1,
							options->message);
		if (conn->user_data[i] == NULL) {
			ret = ENOMEM;
			goto out1;
		}

		/* register each user_data buffer for appropriate access */
		ret = our_setup_mr_sge(conn, conn->user_data[i],
					options->data_size, access[i],
					&conn->user_data_mr[i],
					&conn->user_data_sge[i], options);
		if (ret != 0) {
			free(conn->user_data[i]);
			goto out1;
		}

		/* keep count of number of buffers allocated and registered */
		conn->n_user_data_bufs++;
	}
	/* all user_data buffers set up ok */
	ret = 0;
	goto out0;
out1:
	our_unsetup_buffers(conn, options);
out0:
	return ret;
}	/* our_setup_user_data */


static int
our_setup_common_buffers(struct our_control *conn, int n_local_sges,
			int n_remote_sges, struct our_options *options)
{
	int	ret, i;


	/* setup the recv_ack structures */
	ret = our_setup_recv(conn, &conn->recv_ack, OUR_ACK_SIZE,
				&conn->recv_ack_mr,
				&conn->recv_ack_sge[0], 1,
				&conn->recv_ack_work_request, options);
	if (ret != 0) {
		goto out0;
	}

	/* setup the send_ack structures */
	ret = our_setup_send(conn, &conn->send_ack, OUR_ACK_SIZE,
				0, &conn->send_ack_mr,
				&conn->send_ack_sge[0], IBV_WR_SEND, 1,
				&conn->send_ack_work_request, options);
	if (ret != 0) {
		goto out1;
	}

	our_trace_ok("setup_common_buffers acks", options);

	/* now deal with new local buffer info that we send to remote side */

	/* fill in the fields with info about our local rdma buffer */
	for (i = 0; i < n_local_sges; i++) {
		conn->local_buffer_info[i].addr 
			= ((uint64_t)(unsigned long)(conn->user_data[i]));
			// = htonll((uint64_t)(unsigned long)(conn->user_data[i]));
		conn->local_buffer_info[i].len = htonl(options->data_size);
		conn->local_buffer_info[i].rkey
			= (conn->user_data_mr[i]->rkey);
			// = htonl(conn->user_data_mr[i]->rkey);
		if (options->flags & VERBOSE_TRACING) {
			our_report_ptr("local_buffer_info", "addr",
						conn->user_data[i], options);
			our_report_ulong("local_buffer_info", "rkey",
					conn->user_data_mr[i]->rkey, options);
		}
	}

	/* setup the local buffer info for sending */
	ret = our_setup_send(conn, &conn->local_buffer_info,
				sizeof(struct our_buffer_info),
				0,
				conn->local_buffer_info_mr,
				&conn->local_buffer_info_sge[0], IBV_WR_SEND,
				n_local_sges,
				&conn->local_buffer_info_work_request, options);
	if (ret != 0) {
		goto out2;
	}

	our_trace_ok("setup_common_buffers local_info", options);


/* now deal with new remote buffer info that we recv from remote side */
	/* fields about remote's rdma buffer are filled by recv from remote */

	/* setup the remote buffer info for recving */
	ret = our_setup_recv(conn, &conn->remote_buffer_info,
				sizeof(struct our_buffer_info),
				conn->remote_buffer_info_mr,
				conn->remote_buffer_info_sge,
				n_remote_sges,
				&conn->remote_buffer_info_work_request,options);
	if (ret != 0) {
		goto out3;
	}

	our_trace_ok("setup_common_buffers remote_info", options);

	/* all common buffers setup ok */
	ret = 0;
	goto out0;

out3:
	our_unsetup_mr(conn, conn->local_buffer_info_mr, OUR_MAX_SGES, options);
out2:
	our_unsetup_mr(conn, &conn->send_ack_mr, 1, options);
out1:
	our_unsetup_mr(conn, &conn->recv_ack_mr, 1, options);
out0:
	return ret;
}	/* our_setup_common_buffers */


/*
 * returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
*/
int
our_setup_client_buffers(struct our_control *conn, struct our_options *options)
{
	int	ret;
	int	access[2] = {	IBV_ACCESS_REMOTE_READ,

				IBV_ACCESS_LOCAL_WRITE |
				IBV_ACCESS_REMOTE_WRITE};

	/* client needs 2 buffers,
	 * user_data[0] for local client to RDMA_WRITE ping data out of
	 * user_data[1] for local client to RDMA_READ pong data into
	 */
	ret = our_setup_user_data(conn, 2, access, options);
	if (ret != 0) {
		goto out0;
	}
	our_trace_ok("client's setup_user_data", options);

	/* client needs 2 work requests,
	 * one to RDMA_WRITE ping data out of user_data[0]
	 * one to RDMA_READ pong data into user_data[1]
	 */
	our_setup_send_wr(conn, &conn->user_data_sge[0], IBV_WR_RDMA_WRITE, 1,
					&conn->user_data_send_work_request[0]);
	our_setup_send_wr(conn, &conn->user_data_sge[1], IBV_WR_RDMA_READ, 1,
					&conn->user_data_send_work_request[1]);

	/* the remote info fields of the RDMA_WRITE and RDMA_READ work requests
	 * have to be filled in dynamically
	 */

	/* print out our buffer allocation numbers */
	our_print_buffer_numbers(conn, "our_setup_client_buffers", options);

	ret = our_setup_common_buffers(conn, 1, 1, options);
	if (ret != 0) {
		goto out1;
	}

	/* all client buffers setup ok */
	our_trace_ok("client's setup_common_buffers", options);

	goto out0;

out1:
	our_unsetup_buffers(conn, options);
out0:
	return ret;
}	/* our_setup_client_buffers */


/*
 * returns == 0 if all ok,
 *	   != 0 on error (and error message has been given)
*/
int
our_setup_agent_buffers(struct our_control *conn, struct our_options *options)
{
	int	ret;
	int	access[1] = {	IBV_ACCESS_LOCAL_WRITE |
				IBV_ACCESS_REMOTE_READ |
				IBV_ACCESS_REMOTE_WRITE};

	/* agent needs 1 buffer
	 * user_data[0] for remote client to RDMA_WRITE ping data into
	 * user_data[0] for remote client to RDMA_READ pong data out of
	 */
	ret = our_setup_user_data(conn, 1, access, options);
	if (ret != 0) {
		goto out0;
	}
	our_trace_ok("agent's setup_user_data", options);

	/* agent needs no work requests, because client does all the work */

	/* print out our buffer allocation numbers */
	our_print_buffer_numbers(conn, "our_setup_agent_buffers", options);

	ret = our_setup_common_buffers(conn, 1, 1, options);
	if (ret != 0) {
		goto out1;
	}

	/* all agent buffers setup ok */
	our_trace_ok("agent's setup_common_buffers", options);
	goto out0;

out1:
	our_unsetup_buffers(conn, options);

out0:
	return ret;
}	/* our_setup_agent_buffers */


/* undoes the successful allocations and registrations of all buffers */
void
our_unsetup_buffers(struct our_control *conn, struct our_options *options)
{	int	ret, i;

	/* tear-down memory registrations of common buffers */
	our_unsetup_mr(conn, conn->remote_buffer_info_mr, OUR_MAX_SGES, options);
	our_unsetup_mr(conn, conn->local_buffer_info_mr, OUR_MAX_SGES, options);
	our_unsetup_mr(conn, &conn->send_ack_mr, 1, options);
	our_unsetup_mr(conn, &conn->recv_ack_mr, 1, options);

	/* tear-down memory registration and free memory for user_data buffers */
	for (i = 0; i < conn->n_user_data_bufs; i++) {
		errno = 0;
		ret = ibv_dereg_mr(conn->user_data_mr[i]);
		if (ret != 0) {
			our_report_error(ret, "ibv_dereg_mr user_data_mr",
								options);
		}
		free(conn->user_data[i]);
	}
}	/* our_unsetup_buffers */
