/* utilities.c */
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


/* returns != NULL if all ok (points to newly allocated and zeroed memory)
 *	   == NULL on any error (and error message has been printed)
 */
void *
our_calloc(unsigned long size, const char *message)
{
	void	*ptr;

	ptr = calloc(1, size);
	if (ptr == NULL) {
		perror(message);
	}
	return ptr;
}	/* our_calloc */


/* Scans string pointed to by value and converts it to positive unsigned long.
 * Returns == 0 if successful (and stores the number in result),
 *	   != 0 on error (and error message has been given, result is unchanged)
 */
int
our_scan_switch_ulong(int switch_char, char *value, unsigned long *result,
			const char *message)
{
	long	temp;
	int	retval;
	char	*ptr;

	errno = 0;
	temp = strtol(value, &ptr, 10);
	if (errno != 0  ||  ptr == value
		|| strspn(ptr, WHITE_SPACE) != strlen(ptr)
		|| temp <= 0L) {
		fprintf(stderr,
			"%s: illegal numeric value \"%s\" for option -%c\n",
			message, value, switch_char);
		retval = -1;
	} else {
		*result = temp;
		retval = 0;
	}
	return retval;
}	/* our_scan_switch_ulong */


/* Scans string pointed to by value and converts it to uint64_t.
 * Returns == 0 if successful (and stores the number in result),
 *	   != 0 on error (and error message has been given, result is unchanged)
 */
int
our_scan_switch_uint64(int switch_char, char *value, uint64_t *result,
			const char *message)
{
	int64_t	temp;
	int	retval;
	char	*ptr;

	errno = 0;
	temp = strtoll(value, &ptr, 10);
	if (errno != 0  ||  ptr == value
	   	|| strspn(ptr, WHITE_SPACE) != strlen(ptr)
		|| temp <= (uint64_t)0UL) {
		fprintf(stderr,
			"%s: illegal numeric value \"%s\" for option -%c\n",
			message, value, switch_char);
		retval = -1;
	} else {
		*result = temp;
		retval = 0;
	}
	return retval;
}	/* our_scan_switch_uint64 */


/* on entry, ret is known to be != 0 */
void
our_report_error(int ret, const char *verb_name, struct our_options *options)
{
	int	err;
	char	errbuf[_MAX_STRERROR_MALLOC];

	/* try to find the correct error code */
	if ((err = errno) == 0) {
		if ((err = ret) < 0)
			err = -ret;
	} else if (err < 0) {
		err = -err;
	}

	/* try to convert the error code to a text string */
	if (strerror_r(err, errbuf, _MAX_STRERROR_MALLOC)) {
		snprintf(errbuf, _MAX_STRERROR_MALLOC, "Unknown errno %d", err);
                }

	/* finally print the error message */
	fprintf(stderr, "%s: %s returned %d errno %d %s\n",
		options->message, verb_name, ret, errno, errbuf);
			
}	/* our_report_error */


/* on entry, ret is known to be != 0
 *
 * Returns == 0 if ibv_wc_status message was printed (ret was valid status code)
 *	   != 0 otherwise
 */
int
our_report_wc_status(int ret, const char *verb_name, struct our_options *options)
{
	/* ensure that ret is an enum ibv_wc_status value */
	if (ret < IBV_WC_SUCCESS || ret > IBV_WC_GENERAL_ERR)
		return ret;

	/* print the status error message */
	fprintf(stderr, "%s: %s returned status %d %s\n",
		options->message, verb_name, ret, ibv_wc_status_str(ret));
	return 0;
}	/* our_report_wc_status */


void
our_report_ok(const char *verb_name, struct our_options *options)
{
	fprintf(stderr, "%s: %s ok\n", options->message, verb_name);
}	/* our_report_ok */


void
our_report_ptr(const char *verb_name, const char *ptr_name, void *ptr,
						struct our_options *options)
{
	fprintf(stderr, "%s: %s %s %p\n", options->message, verb_name,
			ptr_name, ptr);
}	/* our_report_ptr */


void
our_report_ulong(const char *verb_name, const char *number_name,
			unsigned long number, struct our_options *options)
{
	fprintf(stderr, "%s: %s %s %lu\n", options->message, verb_name,
			number_name, number);
}	/* our_report_ulong */


void
our_report_uint64(const char *verb_name, const char *number_name,
			uint64_t number, struct our_options *options)
{
	fprintf(stderr, "%s: %s %s %llu\n", options->message, verb_name,
			number_name, (long long unsigned)number);
}	/* our_report_uint64 */


void
our_report_string(const char *verb_name, const char *string_name,
				const char *string, struct our_options *options)
{
	fprintf(stderr, "%s: %s %s \"%s\"\n", options->message, verb_name,
			string_name, string);
}	/* our_report_string */


void
our_trace_error(int ret, const char *verb_name, struct our_options *options)
{
	if (options->flags & TRACING) {
		our_report_error(ret, verb_name, options);
	}
}

void
our_trace_ok(const char *verb_name, struct our_options *options)
{
	if (options->flags & TRACING) {
		our_report_ok(verb_name, options);
	}
}

void
our_trace_ptr(const char *verb_name, const char *ptr_name, void *ptr,
			struct our_options *options)
{
	if (options->flags & TRACING) {
		our_report_ptr(verb_name, ptr_name, ptr, options);
	}
}

void
our_trace_ulong(const char *verb_name, const char *number_name,
			unsigned long number, struct our_options *options)
{
	if (options->flags & TRACING) {
		our_report_ulong(verb_name, number_name, number, options);
	}
}

void
our_trace_uint64(const char *verb_name, const char *number_name,
			uint64_t number, struct our_options *options)
{
	if (options->flags & TRACING) {
		our_report_uint64(verb_name, number_name, number, options);
	}
}

void
our_trace_string(const char *verb_name, const char *string_name,
			const char *string, struct our_options *options)
{
	if (options->flags & TRACING) {
		our_report_string(verb_name, string_name, string, options);
	}
}


void
our_get_current_time(struct timespec *current)
{
	if (clock_gettime(CLOCK_REALTIME, current) < 0) {
		perror("clock_gettime");
		current->tv_sec = 0;
		current->tv_nsec = 0;
	}
}	/* our_get_current_time */


static double
our_get_elapsed_time(struct timespec *start, struct timespec *stop,
		struct timespec *elapsed)
{
	elapsed->tv_sec = stop->tv_sec - start->tv_sec;
	elapsed->tv_nsec = stop->tv_nsec - start->tv_nsec;
	if (elapsed->tv_nsec < 0) {
		elapsed->tv_sec -= 1;
		elapsed->tv_nsec += 1000000000;
	}
	return ((double)elapsed->tv_sec) +
				((double)elapsed->tv_nsec) / 1000000000.0;

}	/* our_get_elapsed_time */


void
our_get_current_usage(struct rusage *current)
{
	if (getrusage(RUSAGE_SELF, current) < 0) {
		perror("getrusage");
		current->ru_utime.tv_sec = 0;
		current->ru_utime.tv_usec = 0;
		current->ru_stime.tv_sec = 0;
		current->ru_stime.tv_usec = 0;
	}
}	/* our_get_current_usage */


static void
our_compute_usage(struct timeval *start, struct timeval *stop,
		  struct timespec *elapsed)
{
	elapsed->tv_sec = stop->tv_sec - start->tv_sec;
	elapsed->tv_nsec = (stop->tv_usec - start->tv_usec) * 1000;
	if (elapsed->tv_nsec < 0) {
		elapsed->tv_sec -= 1;
		elapsed->tv_nsec += 1000000000;
	}
}	/* our_compute_usage */


static void
our_get_elapsed_usage(struct rusage *start, struct rusage *stop,
			struct timespec *elapsed_user,
			struct timespec *elapsed_kernel)
{
	our_compute_usage(&start->ru_utime, &stop->ru_utime, elapsed_user);
	our_compute_usage(&start->ru_stime, &stop->ru_stime, elapsed_kernel);
}	/* our_get_elapsed_usage */


double
our_percentage_time(struct timespec *base, struct timespec *part)
{
	if (base->tv_sec > 0 || base->tv_nsec > 0) {
		return 100.0 * ((double)part->tv_sec +
					(double)(part->tv_nsec)/1000000000.0)
			/ ((double)base->tv_sec +
					(double)(base->tv_nsec)/1000000000.0);
	} else {
		return 0.0;
	}
}	/* our_percentage_time */


void
our_print_statistics(struct our_control *conn, struct our_options *options)
{
	struct timespec		elapsed_time, elapsed_user, elapsed_kernel;
	double			dlapse, duser, dkernel;

	/* mark the time we stop receiving */
	our_get_current_usage(&conn->stop_usage);
	our_get_current_time(&conn->stop_time);

	dlapse = our_get_elapsed_time(&conn->start_time,
					&conn->stop_time, &elapsed_time);
	our_get_elapsed_usage(&conn->start_usage, &conn->stop_usage,
						&elapsed_user, &elapsed_kernel);
	duser = our_percentage_time(&elapsed_time, &elapsed_user);
	dkernel = our_percentage_time(&elapsed_time, &elapsed_kernel);

	fprintf(stderr, "%s: %lu recv completions, %lu send completions\n",
			options->message, conn->wc_recv, conn->wc_send);

	if (options->flags & PRINT_STATS) {
		if (dlapse > 0.0 && (conn->wc_recv > 0)) {
			fprintf(stderr, "%s: %.6f Messages per second\n",
				options->message,
				((double)(conn->wc_recv))/dlapse);
			fprintf(stderr, "%s: %.6f Megabits per second\n",
				options->message,
				8.0 * ((double)((conn->wc_recv)
				* options->data_size)) / (dlapse * 1000000.0));
		}
		if (conn->wc_recv > 0) {
			fprintf(stderr,"%s: %.3f microseconds per buffer\n",
				options->message,
				1000000.0 * dlapse/((double)conn->wc_recv));
		}
	}

	fprintf(stderr, "%s: %ld.%09ld seconds elapsed time\n",
			options->message,
			elapsed_time.tv_sec, elapsed_time.tv_nsec);
	if (options->flags & PRINT_STATS) {
		fprintf(stderr, "%s: %ld.%09ld seconds user time (%.4f%%)\n",
				options->message, 
				elapsed_user.tv_sec, elapsed_user.tv_nsec,
				duser);
		fprintf(stderr, "%s: %ld.%09ld seconds kernel time (%.4f%%)\n",
				options->message, 
				elapsed_kernel.tv_sec, elapsed_kernel.tv_nsec,
				dkernel);
	}

	print_ibv_poll_cq_stats(conn, options);
}	/* our_print_statistics */
