#
# httpd-stat
# 
# Accesses Apache 1/2 shared memory segment directly to display server 
# status information.
# 
# Copyright (c) 2010 Gossamer Threads Inc.
# 
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#

# APACHE_VERSION should be set to 1 or 2
# HARD_SERVER should be set to the hard server limit your apache is built with.

APACHE_VERSION = $(shell httpd -v | awk '{print ; exit}' | sed 's/Server version: Apache\///g'| sed 's/\..*//g' )
HARD_SERVER = $(shell httpd -V | grep HARD_SERVER_LIMIT |sed 's/.*=//g' )

all: 
	gcc httpd-stat.c -o httpd-stat -I/usr/include/apr-1/ -D__APACHE_$(APACHE_VERSION)  -D_GNU_SOURCE -DHARD_SERVER_LIMIT=$(HARD_SERVER)
	@ rm -f modperl-stat
	@ ln -s httpd-stat modperl-stat
	@ echo httpd-stat  modperl-stat
install:
	install httpd-stat /usr/bin/httpd-stat 
	install modperl-stat /usr/bin/modperl-stat 
clean:
	@ rm -f httpd-stat modperl-stat


