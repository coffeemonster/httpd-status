#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <proc/readproc.h>

/** 
 * find-httpd-process.c
 *
 * iterate over process table looking for a certain httpd process
 */

int DEBUG = 0;

int main(int argc, char** argv) {

    int i = 0, pid = 0;
    char find_conf[512] = "";
    char find_user[512] = "";

    //
    // process commandline args
    //
    for ( i=1; i < argc; i++) {
        // -d turns on debugging
        if (strcmp(argv[i], "-d") == 0) {
            DEBUG++;
        }
        // if we provide a -f arg it must match the wanted processes -f arg.
        if (strcmp(argv[i], "-f") == 0 && argc > i + 1 ) {
            strcpy(find_conf, argv[i+1]);
            if (DEBUG) { printf( "[httpd-stat] find procs matching '-f %s'\n", find_conf); }
        }
    }

    pid = find_httpd_pid(&find_conf, &find_user);
    return;
}


int find_httpd_pid ( char * find_conf ) {

    PROCTAB* proc = openproc(PROC_FILLMEM | PROC_FILLSTAT | PROC_FILLSTATUS | PROC_FILLCOM );
    proc_t proc_info;
    memset(&proc_info, '0', sizeof(proc_info));

    char httpd_conf[512] = "";
    int i;
    int pid = 0;


    //
    // find httpd process
    //
    // - PROC_FILLCOM fills cmdline with char** (NULL terminated)
    //
    while (readproc(proc, &proc_info) != NULL) {
        // reset
        strcpy(httpd_conf,"");

        // pretty-print the command
        if (DEBUG > 1) {
            printf("[httpd-stat] (%d) CMD: ", proc_info.tid);
            i = 0;
            while (proc_info.cmdline[i] != NULL) { printf("%s ", proc_info.cmdline[i++]); }
            printf("\n");
        }

        // Apache FTW!
        if (strcmp(proc_info.cmd, "httpd") != 0) {
            if (DEBUG > 1) { printf("[httpd-stat] (%d) .. proc not httpd - skip\n", proc_info.tid); }
            continue;
        }

        // Apache's parent process gets detached and will always belong to init (1)
        // Will skip non-detached apache processes here
        if (proc_info.ppid != 1) {
            if (DEBUG > 1) { printf("[httpd-stat] (%d) .. httpd not parent - skip\n", proc_info.tid); }
            continue;
        }

        // find a config file
        i = 0;
        while (proc_info.cmdline[i] != NULL) {
            if (strcmp(proc_info.cmdline[i], "-f") == 0) {
                // httpd forces a valid arg after -f
                strcpy(httpd_conf, proc_info.cmdline[i+1]);
            }
            i++;
        }

        // check configs searched/provided match
        if (strcmp(httpd_conf, find_conf) != 0) {
            if (DEBUG > 1) {
                printf("[httpd-stat] (%d) .. configs don't match - skip ('%s' != '%s')\n",
                    proc_info.tid, find_conf, httpd_conf);
            }
            continue;
        }

        // found!
        pid = proc_info.tid;

        if (DEBUG && pid) {
            printf("[httpd-stat] (%d) .. found - %s: ppid: %d => pid: %d, user: %d, resident: %ld\n",
                proc_info.tid, proc_info.cmd, proc_info.ppid, proc_info.tid, proc_info.euid,
                proc_info.rss);
        }

    }
    closeproc(proc);

    if (DEBUG && pid)  { printf("[httpd-stat] Found process: %d\n", pid); }
    if (DEBUG && !pid) { printf("[httpd-stat] no httpd process found\n"); }

    return pid;
}

