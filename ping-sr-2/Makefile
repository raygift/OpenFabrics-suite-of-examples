# Makefile
#
# The OpenFabrics suite of examples is code developed for the Programming
# with OpenFabrics Software Training Course.
#
# Copyright (c) 2011 OpenFabrics Alliance, Inc.  All rights reserved.
#
# This software is available to you under a choice of one of two
# licenses.  You may choose to be licensed under the terms of the GNU
# General Public License (GPL) Version 2, available from the file
# GNU_GPL_OFA.txt in the directory containing this source file, or the
# OpenIB.org BSD license, available from the file BSD_for_OFA.txt in the
# directory containing this source file.
#

CC = gcc

CFLAGS = -Wall -O -g

LDFLAGS = -lrt -lrdmacm

SOURCES = $(shell ls *.{c,h})

EXECUTABLES = client server

all:	$(EXECUTABLES) tags

tags:	$(SOURCES)
	ctags $(SOURCES)

client:	client.o utilities.o process_options.o \
		c_d_control_struct.o c_d_id.o bind_client.o \
		c_d_queue_pair.o c_d_connect.o \
		c_d_buffers.o send_recv.o \
		process_cm_events.o process_completions.o

client.o:	client.c prototypes.h

server:	server.o utilities.o process_options.o \
		c_d_control_struct.o c_d_id.o bind_listener.o \
		c_d_connect.o c_d_queue_pair.o \
		c_d_buffers.o send_recv.o agent.o \
		process_cm_events.o process_completions.o

server.o:	server.c prototypes.h

utilities.o:	utilities.c prototypes.h

process_options.o:	process_options.c prototypes.h

process_cm_events.o:	process_cm_events.c prototypes.h

process_completions.o:	process_completions.c prototypes.h

c_d_control_struct.o:	c_d_control_struct.c prototypes.h

c_d_id.o:	c_d_id.c prototypes.h

bind_client.o:	bind_client.c prototypes.h

bind_listener.o:	bind_listener.c prototypes.h

c_d_connect.o:	c_d_connect.c prototypes.h

c_d_queue_pair.o:	c_d_queue_pair.c prototypes.h

c_d_buffers.o:	c_d_buffers.c prototypes.h

send_recv.o:	send_recv.c prototypes.h

agent.o:	agent.c prototypes.h

.PHONY: all clean cleanall

clean:
	$(RM) *.o $(EXECUTABLES)

cleanall:
	$(RM) *.o $(EXECUTABLES) tags
