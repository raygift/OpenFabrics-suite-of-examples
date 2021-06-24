/* process_options.c */
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


#define OPTIONS		":a:p:c:hvtTs:x"

/* processes command line options (switches)
 * returns != NULL if all ok, (pointer is to newly allocated struct our_options)
 *		and the option fields server_name, server_port, and message
 *		have been filled in.
 *	   == NULL on error (and error message has been given)
 */
struct our_options *
our_process_options(int argc, char *argv[])
{
	struct our_options	*options;
	int			c, len;


	/* set up default option values
	 * some of these can be overridden by command-line options
	 */
	options = our_calloc(sizeof(*options), "main");
	if (options == NULL) {
		goto out0;
	}

	options->server_port_number = OUR_DEFAULT_PORT;
	options->limit = OUR_DEFAULT_LIMIT;
	options->data_size = OUR_DEFAULT_DATA_SIZE;
	options->send_queue_depth = OUR_SQ_DEPTH;
	options->recv_queue_depth = OUR_RQ_DEPTH;
	options->max_send_sge = OUR_MAX_SGES;
	options->max_recv_sge = OUR_MAX_SGES;
	options->flags = 0;

	/* our "message" will be the last component of the executable path */;
	options->message = strrchr(argv[0], '/');
	if (options->message == NULL) {
		options->message = argv[0];
	} else {
		options->message += 1;
	}

	/* These two fields must become non-NULL in order to return ok */
	options->server_name = NULL;
	options->server_port = NULL;

	opterr = 0;	/* prevent getopt() from printing errors itself */
	while ((c = getopt(argc, argv, OPTIONS)) != EOF) {
		switch (c) {
		case 'h':
			fprintf(stderr,"The only required option is -a\n");
			fprintf(stderr,"-h this help message\n");
			fprintf(stderr,"-a DNS_name_or_IP_address of server\n");
			fprintf(stderr,"-p port_number of server "
				"(default %d)\n", OUR_DEFAULT_PORT);
			fprintf(stderr,"-c number_of_iterations "
				"(default %d)\n", OUR_DEFAULT_LIMIT);
			fprintf(stderr,"-s number_of_data_bytes in one message "
				"(default %d)\n", OUR_DEFAULT_DATA_SIZE);
			fprintf(stderr,"-v turn on verification of pong data "
				"(client only)\n");
			fprintf(stderr,"-t turn on major trace printing\n");
			fprintf(stderr,"-T turn on detailed trace printing\n");
			fprintf(stderr,"-x print statistics at end of run\n");
			break;
		case 'a':
			/* IP name or address of server */
			len = strlen(optarg);
			if (len <= 0 || strspn(optarg, WHITE_SPACE) == len) {
				our_report_string("options",
					"invalid server name", optarg, options);
				goto out1;
			}
			options->server_name = optarg;
			break;

		case 'p':
			/* port number */
			if (our_scan_switch_ulong(c, optarg,
						&options->server_port_number,
						options->message) != 0) {
				goto out1;
			}
			break;

		case 'c':
			/* number of messages to send */
			if (our_scan_switch_uint64(c, optarg, &options->limit,
							options->message) != 0){
				goto out1;
			}
			break;

		case 'v':
			options->flags |= VERIFY;
			break;

		case 'T':
			options->flags |= VERBOSE_TRACING;
		case 't':
			options->flags |= TRACING;
			break;

		case 's':
			/* size of data to transfer */
			if (our_scan_switch_uint64(c, optarg,
				&options->data_size, options->message) != 0){
				goto out1;
			}
			break;

		case 'x':
			options->flags |= PRINT_STATS;
			break;

		case ':':
			fprintf(stderr, "%s: missing parameter to '%c'\n",
					options->message, optopt);
			goto out1;
			break;

		case '?':
		default:
			fprintf(stderr, "%s: unknown option '%c'\n",
					options->message, optopt);
			goto out1;
			break;
		}	/* switch */
	}	/* while */

        if (options->server_name == NULL) {
                /* user did not specify a server name, better not go on */
                fprintf(stderr, "%s: no server name/address specified "
				"(use -a option)\n", options->message);
                goto out1;
        }

	if (optind < argc) {
		/* extra unused command-line parameters */
		fprintf(stderr, "%s: extra unused command-line parameters\n",
							options->message);
		for ( ; optind < argc; optind++) {
			fprintf(stderr, "%s: %2d: %s\n", options->message,
							optind, argv[optind]);
		}
		goto out1;
	}

	options->server_port = our_calloc(24, "server_port");
	if (options->server_port == NULL)
		goto out1;
	if (snprintf(options->server_port, 24, "%lu",
					options->server_port_number) < 0) {
		perror("server_port");
		goto out2;
	}
	our_report_string("option", "server_name", options->server_name,
								options);
	our_report_ulong("option", "server_port", options->server_port_number,
								options);
	our_report_uint64("option", "count", options->limit, options);
	our_report_uint64("option", "data_size", options->data_size, options);

        for (len = optind; len < argc; len++) {
		our_report_string("option", "unused command-line parameter",
							argv[len], options);
        }
	return options;

out2:
	free(options->server_port);
out1:
	free(options);
out0:
	return NULL;
}	/* our_process_options */


/* free up storage allocated in processing options */
void
our_unprocess_options(struct our_options *options)
{
	free(options->server_port);
	free(options);
}	/* our_unprocess_options */
