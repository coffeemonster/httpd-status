# httpd-status

Accesses Apache 2 shared memory segment directly to display server
status information.

This software is based off httpd-stat by Gossamer Threads Inc.

Copyright (c) 2010 Gossamer Threads Inc. distributed under the GPLv2

See httpd-status.c for a list of changes

## Examples:

```
  httpd-status              # Long output for server running on default config (normally :80)
  httpd-status -r           # short output for SNMP for default config
  httpd-status -r -m        # short output for SNMP for httpds running with -f <config>
  httpd-status -d foo.com   # show info for httpd procs with -f <config> matching */foo.com/*
  modperl-status            # 'httpd-status -m'
  modperl-status -r         # 'httpd-status -m -r'
```
