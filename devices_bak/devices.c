#include <stdio.h>
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

#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <rdma/rdma_cma.h>

int
main(void)
{
	int			num_devices, count, n;
	struct ibv_context	**list, **lptr;
	struct ibv_device_attr	dev_attrs;
	char			*tptr, *nptr;


	num_devices = 0;
	list = rdma_get_devices(&num_devices);
	if(list == NULL || num_devices == 0) {
		fprintf(stderr, "No RDMA devices found\n");
		exit(EXIT_FAILURE);
	}

	fprintf(stderr, "%d RDMA devices found\n", num_devices);

	for(count = 1, lptr = list; *lptr != NULL; lptr++, count++) {
		switch ((*lptr)->device->transport_type) {
		case IBV_TRANSPORT_IB:
			tptr = "InfiniBand";
			break;
		case IBV_TRANSPORT_IWARP:
			tptr = "iWARP";
			break;
		default:
			tptr = "Unknown transport type";
			break;
		}	/* switch */

		switch ((*lptr)->device->node_type) {
		case IBV_NODE_CA:
			nptr = "CA";
			break;
		case IBV_NODE_SWITCH:
			nptr = "Switch";
			break;
		case IBV_NODE_ROUTER:
			nptr = "Router";
			break;
		case IBV_NODE_RNIC:
			nptr = "RNIC";
			break;
		default:
			nptr = "Unknown node type";
			break;
		}	/* switch */
		fprintf(stderr, "%d: %s, %s, %s, %s\n",
			count, (*lptr)->device->name,
			(*lptr)->device->dev_name, tptr, nptr);

		if((n = ibv_query_device(*lptr, &dev_attrs)) != 0)
		{
			if(n < 0)
				n = -n;
			fprintf(stderr, "ibv_query_device returned error %d: "
				"%s\n", n, strerror(n));
		continue;
		}
		fprintf(stderr, "\tRDMA device vendor id %u, part id %u, "
			"hardware version %u, firmware version %s\n",
			dev_attrs.vendor_id,
			dev_attrs.vendor_part_id, dev_attrs.hw_ver,
			dev_attrs.fw_ver);
		fprintf(stderr, "\tMaximum number of supported queue pairs - "
			"max_qp %d\n", dev_attrs.max_qp);
		fprintf(stderr, "\tMaximum number of outstanding work requests "
			"- max_qp_wr %d\n", dev_attrs.max_qp_wr);
		fprintf(stderr, "\tMaximum number of scatter-gather items per "
			"work request for non-registered queue pairs"
			"- max_sge %d\n", dev_attrs.max_sge);
		fprintf(stderr, "\tMaximum number of scatter-gather items per "
			"work request for registered queue pairs"
			"- max_sge_rd %d\n", dev_attrs.max_sge_rd);
		fprintf(stderr, "\tMaximum number of supported completion "
			"queues - max_cq %d\n", dev_attrs.max_cq);
		fprintf(stderr, "\tMaximum number of elements per completion "
			"queue - max_cqe %d\n", dev_attrs.max_cqe);
		fprintf(stderr, "\tMaximum number of supported memory "
			"regions - max_mr %d\n", dev_attrs.max_mr);
		fprintf(stderr, "\tMaximum number of supported protection "
			"domains - max_pd %d\n", dev_attrs.max_pd);
	}

	rdma_free_devices(list);

	return 0;
}	/* main */
