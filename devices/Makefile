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

LDFLAGS = -lrt -lrdmacm -libverbs

SOURCES = $(shell ls *.c)

EXECUTABLES = devices

all:	devices.o
	$(CC) devices.o -o devices $(LDFLAGS)
tags:	$(SOURCES)
	ctags $(SOURCES)

devices:	devices.o

devices.o:	devices.c

.PHONY: all clean cleanall

clean:
	$(RM) *.o $(EXECUTABLES)

cleanall:
	$(RM) *.o $(EXECUTABLES) tags
