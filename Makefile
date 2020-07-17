#
# httpd-status
#
# Access the Apache2 shared memory segment directly to display server
# status information.
#
# This software is based off httpd-stat by Gossamer Threads Inc
# httpd-stat is available at https://www.gossamer-threads.com/developers/tools.html
#
# Copyright (c) 2010 Gossamer Threads Inc.
# Changes by alister.west
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License version 2 as
# published by the Free Software Foundation.
#

all: 
	@# linking to apr-1 to get time functions for scoreboard objects
	@ rm -f httpd-stat modperl-stat
	gcc httpd-status.c -o httpd-stat -lproc -lapr-1 -D_GNU_SOURCE -I/usr/include -I/usr/include/httpd -I/usr/include/apr-1
	@ ln -s httpd-stat modperl-stat
	@ echo httpd-stat  modperl-stat

install:
	install httpd-stat /usr/bin/httpd-stat 
	install modperl-stat /usr/bin/modperl-stat 

clean:
	@ rm -fv httpd-stat modperl-stat findshm findproc test-shms test-procs test-sb

dev:
	@rm -f httpd-stat || true
	gcc httpd-status.c -o httpd-stat -lproc -lapr-1 -D_GNU_SOURCE -I/usr/include -I/usr/include/httpd -I/usr/include/apr-1

## Simple test scripts.
# gcc test-find-httpd-shm.c     -o test-shms  -lproc         -I/home/alister_mp/apache24/include  -D_GNU_SOURCE
# gcc test-find-httpd-process.c -o test-procs -lproc         -I/home/alister_mp/apache24/include  -D_GNU_SOURCE
# gcc test-explore-scoreboard.c -o test-sb    -lproc -lapr-1 -I/home/alister_mp/apache24/include  -D_GNU_SOURCE
