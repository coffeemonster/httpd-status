/*
 * httpd-stat.c
 *
 * Accesses Apache 2 shared memory segment directly to display server
 * status information.
 *
 * This software is based off httpd-stat by Gossamer Threads Inc.
 * Copyright (c) 2010 Gossamer Threads Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * CHANGES
 *  2013 (alister.west)
 *      - version httpd-status-v2.4
 *      - added support for Apache-2.4
 *      - remove support for Apache-1.3
 *      - add support for flags [SERVER_CLOSING, SERVER_IDLE_KILL]
 *      - added tests
 *      - code refactor
 *  2010 (gossamer-threads)
 *      - released httpd-stat v0.11
 *
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/shm.h>
#include <proc/readproc.h>
#include "ap_config.h"
#include "ap_config_layout.h"
#include "httpd.h"
#include "scoreboard.h"

#define HTTPD_STATUS_VERSION 2.4

// --------------------------------------------------------------------------
int  SNMP_STATS = 0;                                 // set with -r
char HTTPD_DOMAIN[1024] = "";                        // set with -d <domain>
int  MODPERL_MODE = 0;                               // set with -m
int  DEBUG = 0;                                      // set with --debug | -D (dev)
char HTTPD_BIN[1024] = DEFAULT_EXP_SBINDIR "/httpd"; // set with -b <binpath> (dev)
// --------------------------------------------------------------------------


int usage( char* basename ) {
// --------------------------------------------------------------------------
// print usage and exit
//
    printf("\nUsage: %s  [ ARGS ]\n", basename);
    printf(" ARGS: -r             snmp 'raw' output\n");
    printf("       -d <domain>    matches against httpd's -f <config>\n");
    printf("       -b <binfile>   httpd binpath [%s]\n", HTTPD_BIN);
    printf("       -m             modperl mode\n\n");
    exit(1);
}


char status_as_char( int status ) {
// ---------------------------------------------------------------------------
// return a printable status for statuses in scoreboard.h
//
    switch ( status ) {
        case SERVER_DEAD:           return '.';
        case SERVER_READY:          return '_';
        case SERVER_STARTING:       return 'S';
        case SERVER_BUSY_READ:      return 'R';
        case SERVER_BUSY_WRITE:     return 'W';
        case SERVER_BUSY_KEEPALIVE: return 'K';
        case SERVER_BUSY_LOG:       return 'L';
        case SERVER_BUSY_DNS:       return 'D';
        case SERVER_GRACEFUL:       return 'G';
        case SERVER_CLOSING:        return 'C';
        case SERVER_IDLE_KILL:      return 'I';
    }
    return '?';
}


void human_duration(char * str, time_t tsecs) { // from mod_status.c:show_time
// ---------------------------------------------------------------------------
// convert time in seconds to human readable output
//
    int secs = tsecs % 60; tsecs /= 60;
    int mins = tsecs % 60; tsecs /= 60;
    int hrs  = tsecs % 24;
    int days = tsecs / 24;
    sprintf(str, " %d day%s  %d hour%s %d minute%s %d second%s",
        days, days == 1 ? "" : "s", hrs,  hrs  == 1 ? "" : "s",
        mins, mins  == 1 ? "" : "s", secs, secs == 1 ? "" : "s");
}


char * human_bytes(char * str, unsigned long bytes) {
// ---------------------------------------------------------------------------
// convert bytes into human readable output
//
    unsigned long kbyte = 1024, mbyte = 1024 * 1024, gbyte = 1024 * 1024 * 1024;
    if      (bytes < (5 * kbyte)) { sprintf(str, "%d B", (int) bytes); }
    else if (bytes < (mbyte / 2)) { sprintf(str, "%.1f kB", (float) bytes / kbyte); }
    else if (bytes < (gbyte / 2)) { sprintf(str, "%.1f MB", (float) bytes / mbyte); }
    else                          { sprintf(str, "%.1f GB", (float) bytes / gbyte); }
    return str;
}


void print_general_info( int pid, char * domain ) {
// ---------------------------------------------------------------------------
// output general information about the httpd process
//
    // hostname
    char hostname[1024] = "";
    gethostname(hostname, 128);
    printf("  Apache Server Status for %s\n", hostname);


    // user-name
    PROCTAB* proc = openproc(PROC_FILLMEM | PROC_FILLUSR);
    proc_t proc_info;
    while (readproc(proc, &proc_info) != NULL) {
        if (proc_info.tid != pid)  { continue; }
        printf("  Process belongs to  %s\n", proc_info.euser);
    }
    closeproc(proc);


    // mod_perl
    if (strlen(domain)) {
        printf("  Modperl : %s\n", domain);
    }


    // httpd -v
    int forkid = fork();
    if (forkid == 0) { // child
        DEBUG && printf("[debug] cmd: %s -v\n", HTTPD_BIN);
        execl(HTTPD_BIN, HTTPD_BIN, "-v", (char *) 0);
        exit;
    } else if (forkid > 0) {
        int child_exit_status;
        wait(&child_exit_status);
    }
}


int print_scoreboard_snmp ( scoreboard * sb_ptr ) {
// ---------------------------------------------------------------------------
// Summary info for snmp
//
    global_score * sb_global = (global_score*) sb_ptr;

    int status[SERVER_NUM_STATUS] = {0}, i;
    unsigned long total_accesses = 0, total_traffic = 0;
    for (i = 0; i < sb_global->server_limit * sb_global->thread_limit; i++) {
        worker_score * ws = (worker_score*)(
                     (char*) sb_ptr + sizeof(global_score)
                                    + sizeof(process_score) * sb_global->server_limit
                                    + sizeof(worker_score)  * i );
        status[ ws->status ]++;
        total_accesses += ws->access_count;
        total_traffic  += ws->bytes_served;
    }

    for ( i = 0; i < (SERVER_NUM_STATUS -1); i++) {
        printf("%c:%d ", status_as_char(i), status[i]);
    }
    printf("acc:%lu kb:%lu ",   total_accesses, (total_traffic >> 10) );

}


int print_scoreboard_info ( scoreboard * sb_ptr ) {
// ---------------------------------------------------------------------------
// Prints a nice text-format of the information available in the scoreboard.
// Note: Uses the Apache2 scoreboard structure.
//

    DEBUG && printf("[debug] print_scoreboard_info ...\n");

    double        TICK = sysconf(2); // _SC_CLK_TCK is 2 - clock ticks per second.
    int           status[SERVER_NUM_STATUS] = {0}, i, j;
    unsigned long total_accesses = 0, total_traffic = 0;
    scoreboard    sb;

    sb.global   = (global_score*) sb_ptr;
    sb.parent   = (process_score*)((char*) sb_ptr + sizeof(global_score)); // sb.parent[i].pid
    sb.servers  = (worker_score**)((char*) sb.parent + sizeof(process_score) * sb.global->server_limit);


    //
    // Restart-Time, Server-Generation, Server-Uptime
    //
    char restart_str[256] = "";
    char uptime_str[256]  = "";
    time_t uptime = time(NULL) - apr_time_sec(sb.global->restart_time); // apr_time_t is microsec's
    apr_ctime( restart_str, sb.global->restart_time) ;
    human_duration( uptime_str, uptime );
    printf("  Restart Time: %s\n", restart_str);
    printf("  Parent Server Generation: %d\n", sb.global->running_generation + 1);
    printf("  Server Uptime: %s\n", uptime_str);


    //
    // Generate Worker Summary and CPU Stats
    //
    double  cpu_utime, cpu_stime, cpu_cutime, cpu_cstime;
    for (i = 0; i < sb.global->server_limit * sb.global->thread_limit; i++) {
        worker_score * ws = (worker_score*)( (char*) sb.servers + sizeof(worker_score)  * i );
        status[ ws->status ]++;
        total_accesses += ws->access_count;
        total_traffic  += ws->bytes_served;
        // <sys/times.h>
        cpu_utime   += (ws->times.tms_utime || 0);
        cpu_stime   += (ws->times.tms_stime || 0);
        cpu_cutime  += (ws->times.tms_cutime || 0);
        cpu_cstime  += (ws->times.tms_cstime || 0);
    }
    cpu_utime /= TICK;  cpu_stime /= TICK;
    cpu_cutime /= TICK; cpu_cstime /= TICK;


    //
    // Print CPU and Traffic Stats
    //
    char kb_str[256]  = "";
    printf("  Total accesses: %lu - Total traffic: %s kB\n", total_accesses, human_bytes(kb_str, total_traffic) );
    printf("  CPU usage: u%g s%g cu%g cs%g - %.3g%% CPU load\n",
                cpu_utime, cpu_stime, cpu_cutime, cpu_cstime,
                (cpu_utime + cpu_stime + cpu_cutime + cpu_cstime) / uptime * 100.0 );
    printf("  %.3g requests/sec - %.3g b/second - %.3g b/request\n",
                (float) total_accesses / (float) uptime,
                (float) total_traffic  / (float) uptime,
                (float) total_traffic  / (float) total_accesses );


    //
    // Worker Stats
    //
    int busy = 0;
    for (i = 0; i < SERVER_NUM_STATUS; i++) {
        if (i != SERVER_READY && i != SERVER_DEAD && status[i] > 0) { busy += status[i]; }
    }
    printf("  %d requests currently being processed, %d idle servers\n", busy, status[SERVER_READY] );

    for (i = 0; i < sb.global->server_limit * sb.global->thread_limit; i++) {
        if (i % 64 == 0) { printf("\n"); }
        worker_score * ws = (worker_score*)( (char*) sb.servers + sizeof(worker_score)  * i );
        printf("%c", status_as_char(ws->status) );
    }
    printf("\n\nScoreboard key:\n");
    printf("\"_\" Waiting for Connection, \"S\" Starting up, \"R\" Reading Request,\n");
    printf("\"W\" Sending Reply, \"K\" Keepalive (read), \"D\" DNS Lookup,\n");
    printf("\"C\" Closing connection, \"L\" Logging, \"G\" Gracefully finishing,\n");
    printf("\"I\" Idle cleanup of worker, \".\" Open slot with no current process\n\n");


    //
    // Worker Current Requests
    //
    printf("  Srv\t\tPID\tAcc\t\tM\tCPU\tSS\tReq\tConn\tChild\tSlot\tHost\t\tVHost\t\t\tRequest\n");
    i = 0;
    while( i++ < sb.global->server_limit * sb.global->thread_limit) {

        int j = (int)( i / sb.global->server_limit);
        worker_score * ws = (worker_score*)( (char*) sb.servers + sizeof(worker_score) * i );
        process_score * ps = &sb.parent[j];

        // cleanup some numbers
        if (ws->status == SERVER_READY || ws->status == SERVER_DEAD) { continue; }
        int pid = (ps->pid > 32768 || ps->pid < 32768) ? 0 : ps->pid;                   // /proc/sys/kernel/pid_max
        int generation = ps->generation > 100000 ? 0 : ps->generation;
        long int req = (ws->stop_time > ws->start_time) ? apr_time_sec(ws->stop_time - ws->start_time) : -1;


        printf("%4d-%-4d\t%d\t", i, generation, pid );                                  // Srv, PID
        printf("%hu/%lu/%-4lu\t", ws->conn_count,ws->my_access_count,ws->access_count); // Acc
        printf("%c\t", status_as_char(ws->status) );                                    // Mode
        time_t cpu_t = ws->times.tms_utime + ws->times.tms_stime + ws->times.tms_cutime + ws->times.tms_cstime;
        printf("%.2f\t", cpu_t / TICK );                                                // CPU
        printf("%.0f\t", difftime(time(NULL), apr_time_sec(ws->last_used)) );           // SS
        printf("%ld\t",  req );                                                         // Req
        printf("%-1.1f\t", (float) ws->conn_bytes / 1024 );                             // Conn  KB
        printf("%-2.2f\t", (float) ws->my_bytes_served / (1024 * 1024) );               // Child MB
        printf("%-2.2f\t", (float) ws->bytes_served / (1024 * 1024) );                  // Slot  MB

        if (ws->status == SERVER_BUSY_READ) {
            printf("?\t\t ?\t\t..reading.. \n");
        }
        else {
            char client[32];
            printf("%s\t%s\t%s\n", strncpy(client, ws->client, 31), ws->vhost, ws->request);
        }
    }

    printf("\n -----------------------------------------------------------------------------\n");
    printf("   Srv   Child Server number - generation\n");
    printf("   PID   OS process ID\n");
    printf("   Acc   Number of accesses this connection / this child / this slot\n");
    printf("   M     Mode of operation\n");
    printf("   CPU   CPU usage, number of seconds\n");
    printf("   SS    Seconds since beginning of most recent request\n");
    printf("   Req   Milliseconds required to process most recent request\n");
    printf("   Conn  Kilobytes transferred this connection\n");
    printf("   Child Megabytes transferred this child\n");
    printf("   Slot  Total megabytes transferred this slot\n");
    printf(" -----------------------------------------------------------------------------\n");

}


int find_shm_with_pid(int pid) {
// ---------------------------------------------------------------------------
// given a PID find a matching SHM_ID
// as 0 can be a valid SHM_ID we return -1 for SHM_NOT_FOUND
//
    if (pid < 1) { return -1; }

    struct shm_info shm_info;
    int max_index = shmctl(0, SHM_INFO, (struct shmid_ds *) &shm_info);
    int i;
    for( i = 0; i <= max_index && shm_info.used_ids; i++) {
        struct shmid_ds shmseg;
        int shmid = shmctl(i, SHM_STAT, &shmseg);
        if (shmid >= 0) {
            if (shmseg.shm_cpid == pid) {
                return shmid;
            }
        }
    }
    return -1;
}


int process_cmdline(int argc, char** argv) {
// ---------------------------------------------------------------------------
// process commandline args - configuring the script globals.
//
    int i;
    for ( i=0; i < argc; i++) {

        // -h prints help (and exits)
        if (strcmp(argv[i], "-h") == 0) {
            usage(argv[0]);
        }

        // -b : binary path for httpd
        //    : useful when testing new apache builds
        if (strcmp(argv[i], "-b") == 0 && argc > i + 1 ) {
            strcpy(HTTPD_BIN, argv[i+1]);
        }

        // --debug|-D turns on debugging
        if (strcmp(argv[i], "--debug") == 0 || strcmp(argv[i], "-D") == 0) {
            DEBUG++;
        }

        // -m : modperl-mode
        if (strcmp(argv[i], "-m") == 0) {
            MODPERL_MODE++;
        }

        // -d <domain> : will find a httpd process with a matching conf
        if (strcmp(argv[i], "-d") == 0) {
            if ( i+1 >= argc ) {
                usage(argv[0]);
            }
            strcpy(HTTPD_DOMAIN, argv[i+1]);
        }

        // -r raw-stats == reduced output for SNMP service
        if (strcmp(argv[i], "-r") == 0) {
            SNMP_STATS++;
        }
    }
    if (strstr(argv[0], "modperl-stat") != NULL) {
        MODPERL_MODE++;
    }

    DEBUG &&                             printf("[debug] debug-mode [On]\n");
    DEBUG && SNMP_STATS &&               printf("[debug] snmp_stats [On]\n");
    DEBUG && MODPERL_MODE &&             printf("[debug] modperl_mode [On]\n");
    DEBUG && strlen(HTTPD_BIN) > 0 &&    printf("[debug] with httpd '%s'\n", HTTPD_BIN);
    DEBUG && strlen(HTTPD_DOMAIN) > 0 && printf("[debug] using domain '%s'\n", HTTPD_DOMAIN);
}


/*
 *
 * MAIN
 *
 */
int main(int argc, char** argv) {
// ---------------------------------------------------------------------------

    // set global script flags
    process_cmdline(argc, argv);

    // ---------------------------------------------------------------------------
    // Loop through httpd processes
    //  -- find parent httpds
    //  -- match with a shared-mem-instance
    //  -- print info from proc
    //  -- print info from shared-mem
    //
    PROCTAB* proc = openproc(PROC_FILLMEM | PROC_FILLSTAT | PROC_FILLSTATUS | PROC_FILLCOM );
    proc_t proc_info;
    memset(&proc_info, '0', sizeof(proc_info));

    READPROC_LOOP:
    while (readproc(proc, &proc_info) != NULL) {

        if (strcmp(proc_info.cmd, "httpd") != 0) { continue; }  // apache FTW!
        if (proc_info.ppid != 1) { continue; }                  // apache detaches and will belong to init(1)

        DEBUG && printf("[debug] proc - have httpd: %d\n", proc_info.tid);

        // check for a domain config
        char domain[1024] = "";
        int i = -1;
        while (proc_info.cmdline[++i] != NULL) {
            if (strcmp(proc_info.cmdline[i], "-f") == 0) { // httpd forces a valid arg after -f
                if ( strncmp( proc_info.cmdline[i+1], "/var/home/", 10 ) == 0 ) {
                    DEBUG && printf("[debug] .. cmdline -f: %s\n", proc_info.cmdline[i+1]);
                    char * ch_p = strtok( proc_info.cmdline[i+1], "/" ); // var
                    ch_p = strtok( NULL, "/");                          // home
                    ch_p = strtok( NULL, "/");                          // username
                    ch_p = strtok( NULL, "/");                          // domain
                    strcpy(domain, ch_p);
                    DEBUG && printf("[debug] .. domain: %s\n", domain);
                }
                if (strlen(HTTPD_DOMAIN) && strcmp(HTTPD_DOMAIN, domain) != 0) {
                DEBUG && printf("[debug] .. skipping - domain mismatch (%s, %s)\n", HTTPD_DOMAIN, domain);
                    goto READPROC_LOOP; // not a match for this domain - break out of multiple while loops
                }
            }
        }
        DEBUG && printf("[debug] .. processed /proc/%d/cmdline\n", proc_info.tid);

        // we either want info about domains or we don't
        if (MODPERL_MODE && !strlen(domain)) { DEBUG && printf("[debug] .. skipping - domain missing\n");      continue; }
        if (!MODPERL_MODE && strlen(domain)) { DEBUG && printf("[debug] .. skipping - domain '%s'\n", domain); continue; }


        // find shared memory id and attatch to it.
        int shmid = find_shm_with_pid( proc_info.tid );
        scoreboard * shared_mem = shmat(shmid, (void *) 0, 0);
        if (shared_mem == NULL) { printf("Error shmat for pid:%d,shmid:%d\n", proc_info.tid, shmid); continue; }

        DEBUG && printf("[debug] .. found httpd process (%d) with SHM (%d)\n", proc_info.tid, shmid);


        // Print Scoreboard Info
        if (SNMP_STATS) {
            // wrap domain stats
            MODPERL_MODE && printf("%s=(", domain);
            print_scoreboard_snmp( shared_mem );
            MODPERL_MODE && printf(")  %s", DEBUG ? "\n" : "");
        } else {
            print_general_info( proc_info.tid, domain);
            print_scoreboard_info( shared_mem );
        }


        // cleanup
        shmdt((void*) shared_mem);
    }

    // Finish our snmp status-line with a newline.
    if (SNMP_STATS) { printf("\n"); }

    DEBUG && printf("[debug] Done!\n");
    return;
}
// vim: ts=4 sw=4
