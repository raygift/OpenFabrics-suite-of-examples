/* c_d_control_struct */
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


/* allocate our own control structure to keep track of all new connection info
 *
 * returns != NULL if all ok,
 *	   == NULL on error (and error message has been given)
 */
struct our_control *
our_create_control_struct(struct our_options *options)
{
	struct our_control	*conn;

	/* allocate our own control structure to keep track of new connection */
	conn = our_calloc(sizeof(*conn), "struct our_control");
	if (conn != NULL) {
		/* new control block set up ok */
		our_trace_ptr("our_create_control_struct","created our_control",
								conn, options);
	}
	return conn;
}	/* our_create_control_struct */


void
our_destroy_control_struct(struct our_control *conn,
						struct our_options *options)
{
	our_report_ulong("our_destroy_control_struct", "final disconnected",
						conn->disconnected, options);
	free(conn);
	our_trace_ptr("our_destroy_control_struct", "destroyed our_control",
								conn, options);
}	/* our_destroy_control_struct */
