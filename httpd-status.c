/*
 * httpd-stat.c
 *
 * Accesses Apache 1/2 shared memory segment directly to display server
 * status information.
 *
 * Copyright (c) 2010 Gossamer Threads Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <asm/param.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <pwd.h>
#include <httpd/ap_config.h>
#include <httpd/httpd.h>    /* for HARD_SERVER_LIMIT */
#include <httpd/scoreboard.h>

#ifdef __APACHE_2
#undef HARD_SERVER_LIMIT
#include <apr_time.h>
#include <httpd/mpm_common.h>
#include <httpd/ap_mpm.h>
#endif /*__APACHE_2*/

#define KBYTE            1024
#define MBYTE            1048576L
#define GBYTE            1073741824L
#define HTTPD_INFO       0
#define PERL_INFO        1
#define true             1
#define false            0
#define MODPERL_STR      "modperl-stat"
#define HTTPD_STR        "httpd-stat"

typedef struct info_shm{
    int shmid;
    int pid;
    int uid;
    struct info_shm * next;
}info_shm;
static char status_flags[SERVER_NUM_STATUS];

#ifdef __APACHE_2
static int HARD_SERVER_LIMIT;
struct serverlimit{
    int MaxClients;
    int ServerLimit;
};

void readlimits(struct serverlimit * limit,char * filename)
{
    FILE *fd;
    char * buffer = NULL;
    char tmp[256];
    size_t max = 0;
    fd= fopen(filename, "r");
    if(fd == NULL){
        printf("Cannot open %s. You must be root to run this utility.\n",filename);
        perror("cannot open file");
        exit(1);
    }
    while(getline (&buffer, &max, fd)>0){
        if (strncmp("MaxClients",buffer,strlen("MaxClients"))==0)
            sscanf(buffer,"%s %d ",tmp,&(limit->MaxClients));

        if (strncmp("ServerLimit",buffer,strlen("MaxClients"))==0)
            sscanf(buffer,"%s %d ",tmp,&(limit->ServerLimit));
    }

    free(buffer);
}


int get_server_limit(void)
{
    struct serverlimit limit ={256,256};
    DIR *dp;
    struct dirent *ep;
    char filename[512];

    /*read the default configuration file*/
    readlimits(&limit,"/etc/httpd/httpd.conf");

    /*read the custom configuration files*/
    dp = opendir ("/etc/httpd/custom");
    if (dp != NULL){
        while (ep = readdir (dp)){
            size_t len = strlen(ep->d_name);
            if(len > 5 &&  ep->d_name[len-5] == '.'&& \
                           ep->d_name[len-4] == 'c'&& \
                           ep->d_name[len-3] == 'o'&& \
                           ep->d_name[len-2] == 'n'&& \
                           ep->d_name[len-1] == 'f'){
                strcpy(filename,"/etc/httpd/custom/");
                strcat(filename,ep->d_name );
                readlimits(&limit,filename);
            }
        }
        (void) closedir (dp);
    }
    else {
        perror ("Couldn't open the directory");
    }
    return limit.ServerLimit;
}
#endif /*__APACHE_2*/


static void status_init(void)
{
    status_flags[SERVER_DEAD] = '.';     /* We don't want to assume these are in */
    status_flags[SERVER_READY] = '_';    /* any particular order in scoreboard.h */
    status_flags[SERVER_STARTING] = 'S';
    status_flags[SERVER_BUSY_READ] = 'R';
    status_flags[SERVER_BUSY_WRITE] = 'W';
    status_flags[SERVER_BUSY_KEEPALIVE] = 'K';
    status_flags[SERVER_BUSY_LOG] = 'L';
    status_flags[SERVER_BUSY_DNS] = 'D';
    status_flags[SERVER_GRACEFUL] = 'G';
#ifdef __APACHE_2
    status_flags[SERVER_CLOSING] = 'C';
    status_flags[SERVER_IDLE_KILL] = 'I';
#endif /*__APACHE_2*/
}

/* Format the number of bytes nicely */
static void format_byte_out(unsigned long bytes)
{
    if (bytes < (5 * KBYTE)) {
        printf("%d B", (int) bytes);
    }
    else if (bytes < (MBYTE / 2)) {
        printf("%.1f kB", (float) bytes / KBYTE);
    }
    else if (bytes < (GBYTE / 2)) {
        printf("%.1f MB", (float) bytes / MBYTE);
    }
    else {
        printf("%.1f GB", (float) bytes / GBYTE);
    }
}

static void format_kbyte_out(unsigned long kbytes)
{
    if (kbytes < KBYTE) {
        printf("%d kB", (int) kbytes);
    }
    else if (kbytes < MBYTE) {
        printf("%.1f MB", (float) kbytes / KBYTE);
    }
    else {
        printf("%.1f GB", (float) kbytes / MBYTE);
    }
}

void printStatusOverview(scoreboard * sb)
{
}

static char gt_flags[SERVER_NUM_STATUS];

/* return when the apache server has been restarted */
time_t getRestartTime(int httpdPid)
{
    double up=0, idle=0;
    unsigned long long Hertz;
    unsigned long btime, time_offset;
    time_t start_time;
    unsigned int i;
    char *stat_fn, *strhold, *buf;
    FILE *fp, *sfp;

    buf=(char *)malloc(sizeof(char)*8192);
    if (buf == NULL) {
        fputs ("Memory error",stderr); exit (2);
    }

    fp = fopen("/proc/uptime", "r");
    fread(buf,1024,1,fp);
    fclose(fp);
    if (sscanf(buf, "%lf %lf", &up, &idle) < 2) {
        fputs("bad data in /proc/uptime\n", stderr);
        return 0;
    }

    Hertz = (unsigned long long)HZ;

    stat_fn = (char *)malloc(sizeof(char)*20);
    sprintf(stat_fn, "/proc/%d/stat", httpdPid);
    sfp = fopen(stat_fn, "r");
    if (sfp == NULL) {
        exit (2);
    }
    fread(buf,1024,1,sfp);
    fclose(sfp);
    strhold = strtok(buf, " ");
    for (i = 0; i < 21; i++) {
        strhold = strtok(NULL, " ");
    }
    time_offset = atoi(strhold);

    btime = time(NULL) - up;

    start_time = btime + time_offset / Hertz;
    return start_time;
}

static void show_time(char *r, time_t tsecs)
{
    long days, hrs, mins, secs;
    struct tm *gmtime(const time_t * timep);

    secs = tsecs % 60;
    tsecs /= 60;
    mins = tsecs % 60;
    tsecs /= 60;
    hrs = tsecs % 24;
    days = tsecs / 24;
    sprintf(r, " %ld day%s  %ld hour%s %ld minute%s %ld second%s",
        days, days == 1 ? "" : "s", hrs, hrs == 1 ? "" : "s", mins,
        mins == 1 ? "" : "s", secs, secs == 1 ? "" : "s");

}

#ifdef __APACHE_2
void displayApacheShm(scoreboard * param_scoreboard_image, int httpdPid, int mode, int snmp)
#else   /*__APACHE_2*/
void displayApacheShm(scoreboard * ap_scoreboard_image, int httpdPid, int mode, int snmp)
#endif     /*__APACHE_2*/
{
    time_t nowtime = time(NULL);
    time_t up_time;
    time_t ap_restart_time;
    int i, res;
    int ready = 0;
    int busy = 0;
    unsigned long count = 0;
    unsigned long lres, bytes;
    unsigned long my_lres, my_bytes, conn_bytes;
    unsigned short conn_lres;
    unsigned long bcount = 0;
    unsigned long kbcount = 0;
    long req_time;
#ifdef _SC_CLK_TCK
    float tick = sysconf(_SC_CLK_TCK);
#else  /*_SC_CLK_TCK */
    float tick = HZ;
#endif /*_SC_CLK_TCK */
    int short_report = 0;
#ifdef __APACHE_2
    worker_score * score_record;
    process_score ps_record;
    char *vhost;
#else   /*__APACHE_2*/
    short_score * score_record;
    parent_score ps_record;
    server_rec *vhost;
#endif     /*__APACHE_2*/
    char stat_buffer[HARD_SERVER_LIMIT];
    int pid_buffer[HARD_SERVER_LIMIT];
    clock_t tu, ts, tcu, tcs;
    char ShowUptime[256];
#ifdef __APACHE_2
    ap_generation_t ap_my_generation;
    scoreboard scoreboard_image;
    scoreboard * ap_scoreboard_image;

    scoreboard_image.global   = (global_score*)param_scoreboard_image;
    scoreboard_image.parent   = (process_score*)((char*) param_scoreboard_image + sizeof(global_score));

    scoreboard_image.servers  = (worker_score**)malloc(HARD_SERVER_LIMIT*sizeof(worker_score*)) ;
    for(i = 0; i < HARD_SERVER_LIMIT; i++){
        scoreboard_image.servers[i] = (worker_score*)((char*) scoreboard_image.parent + sizeof(process_score) * HARD_SERVER_LIMIT + sizeof(worker_score) * i );
    }

    scoreboard_image.balancers = NULL;
    ap_scoreboard_image = &scoreboard_image;

    if ( (size_t) param_scoreboard_image == -1) {
        exit (2);
    }

    ap_my_generation = ap_scoreboard_image->parent[0].generation;
#else   /*__APACHE_2*/
    if ( (size_t) ap_scoreboard_image == -1) {
        exit (2);
    }

    ap_generation_t ap_my_generation = ap_scoreboard_image->parent[0].generation;
#endif     /*__APACHE_2*/

    tu = ts = tcu = tcs = 0;
    ap_restart_time = getRestartTime(httpdPid);

    for (i = 0; i < HARD_SERVER_LIMIT; ++i) {
#ifdef __APACHE_2
        score_record = ap_scoreboard_image->servers[i];
#else   /*__APACHE_2*/
        score_record = &ap_scoreboard_image->servers[i];
#endif     /*__APACHE_2*/
        ps_record = ap_scoreboard_image->parent[i];
        res = score_record->status;
        stat_buffer[i] = gt_flags[res];
        pid_buffer[i] = (int) ps_record.pid;
        if (res == SERVER_READY) {
            ready++;
        }
        else if (res != SERVER_DEAD) {
            busy++;
        }

        lres = score_record->access_count;
        bytes = score_record->bytes_served;
        if (lres != 0 || (res != SERVER_READY && res != SERVER_DEAD)) {
            tu += score_record->times.tms_utime;
            ts += score_record->times.tms_stime;
            tcu += score_record->times.tms_cutime;
            tcs += score_record->times.tms_cstime;
            count += lres;
            bcount += bytes;
            if (bcount >= KBYTE) {
                kbcount += (bcount >> 10);
                bcount = bcount & 0x3ff;
            }
        }
    }

    up_time = nowtime - ap_restart_time;
    show_time(ShowUptime, up_time);

    if (snmp == 1) {
        int statusresults[SERVER_NUM_STATUS];
        for ( i = 0; i < SERVER_NUM_STATUS-1; i++) {
            statusresults[i] = 0;
        }
        for (i = 0; i < HARD_SERVER_LIMIT; i++) {
#ifdef __APACHE_2
            statusresults[ap_scoreboard_image->servers[i]->status]++;
#else  /*__APACHE_2*/
            statusresults[ap_scoreboard_image->servers[i].status]++;
#endif /*__APACHE_2*/
        }

        for ( i = 0; i < SERVER_NUM_STATUS-1; i++) {
            if (status_flags[i] != 0)
                printf("%c:%d ", status_flags[i], statusresults[i]);
        }
        printf("acc:%lu ", count);
        printf("kb:%lu ", kbcount);
        return;
    }

    printf("  Restart time: %s  Parent server generation: %d\n  Server uptime:%s\n", ctime(&ap_restart_time) ,ap_my_generation, ShowUptime);
    printf("  Total accesses: %lu - Total traffic: ", count);
    format_kbyte_out(kbcount);

    printf("\n  CPU usage: u%g s%g cu%g cs%g", tu / tick, ts / tick, tcu / tick, tcs / tick);
    if (ts || tu || tcu || tcs) {
        printf(" - %.3g%% CPU load\n", (tu + ts + tcu + tcs) / tick / up_time * 100.);
    }
    if (up_time > 0) {
        printf("  %.3g requests/sec - ", (float) count / (float) up_time);
    }

    if (up_time > 0) {
        format_byte_out((unsigned long) (KBYTE * (float) kbcount / (float) up_time));
        printf("/second - ");
    }

    if (count > 0) {
        format_byte_out((unsigned long) (KBYTE * (float) kbcount / (float) count));
        printf("/request\n");
    }

    printf("\n  %d requests currently being processed, %d idle servers\n", busy, ready);

    for (i = 0; i < HARD_SERVER_LIMIT; i++) {
        if (i % 64 == 0) {
            printf("\n");
        }
#ifdef __APACHE_2
        printf("%c", status_flags[ap_scoreboard_image->servers[i]->status]);
#else  /*__APACHE_2*/
        printf("%c", status_flags[ap_scoreboard_image->servers[i].status]);
#endif /*__APACHE_2*/
    }
    printf("\n");
    printf("\n");
    printf("Scoreboard key:\n");
    printf("\"_\" Waiting for Connection, \"S\" Starting up, \"R\" Reading Request,\n");
    printf("\"W\" Sending Reply, \"K\" Keepalive (read), \"D\" DNS Lookup,\n");
    printf("\"C\" Closing connection, \"L\" Logging, \"G\" Gracefully finishing,\n");
    printf("\"I\" Idle cleanup of worker, \".\" Open slot with no current process\n");
    printf("\n");

    if(mode == HTTPD_INFO  || mode == PERL_INFO){
        printf ("Srv\tPID\tAcc\tM\tCPU\tSS\tReq\tConn\tChild\tSlot\tHost\t\t VHost\t\tRequest\n");
        for (i = 0; i < HARD_SERVER_LIMIT; ++i) {
            ps_record = ap_scoreboard_image->parent[i];
#ifdef __APACHE_2
            score_record = ap_scoreboard_image->servers[i];
            vhost = score_record->vhost;
#else   /*__APACHE_2*/
            score_record = &ap_scoreboard_image->servers[i];
            vhost = score_record->vhostrec;
#endif     /*__APACHE_2*/
            if (ps_record.generation != ap_my_generation) {
                vhost = NULL;
            }

#ifdef __APACHE_2
            if (score_record->start_time == (clock_t) 0) {
                req_time = 0L;
            }
            else {
                req_time = (long)((score_record->stop_time - score_record->start_time) / 1000);
                req_time = score_record->stop_time - score_record->start_time;
                req_time = (req_time * 1000) / (int) tick;
            }

#else   /*__APACHE_2*/
        if (score_record->start_time.tv_sec == 0L &&
            score_record->start_time.tv_usec == 0L) {
            req_time = 0L;
        }
        else {
            req_time = ((score_record->stop_time.tv_sec - score_record->start_time.tv_sec) * 1000)
                       +
                       ((score_record->stop_time.tv_usec - score_record->start_time.tv_usec) / 1000);
        }
#endif     /*__APACHE_2*/
        if (req_time < 0L) {
            req_time = 0L;
        }

        lres = score_record->access_count;
        my_lres = score_record->my_access_count;
        conn_lres = score_record->conn_count;
        bytes = score_record->bytes_served;
        my_bytes = score_record->my_bytes_served;
        conn_bytes = score_record->conn_bytes;

        if (lres != 0 || (score_record->status != SERVER_READY && score_record->status != SERVER_DEAD)) {
            if (!short_report) {
                if (score_record->status == SERVER_DEAD) {
                    printf("%d-%d\t-\t%d/%lu/%lu\t", i, (int) ps_record.generation, (int) conn_lres, my_lres, lres);
                }
                else {
                    printf("%d-%d\t%d\t%d/%lu/%lu\t", i, (int) ps_record.generation, (int) ps_record.pid, (int) conn_lres, my_lres, lres);
                }
                switch (score_record->status) {
                    case SERVER_READY:
                        printf("_\t");
                        break;
                    case SERVER_STARTING:
                        printf("S\t");
                        break;
                    case SERVER_BUSY_READ:
                        printf("R\t");
                        break;
                    case SERVER_BUSY_WRITE:
                        printf("W\t");
                        break;
                    case SERVER_BUSY_KEEPALIVE:
                        printf("K\t");
                        break;
                    case SERVER_BUSY_LOG:
                        printf("L\t");
                        break;
                    case SERVER_BUSY_DNS:
                        printf("D\t");
                        break;
                    case SERVER_DEAD:
                        printf(".\t");
                        break;
#ifdef __APACHE_2
                    case SERVER_GRACEFUL:
                        printf("G\t");
                        break;
                    case SERVER_CLOSING:
                        printf("C\t");
                        break;
                    case SERVER_IDLE_KILL:
                        printf("I\t");
                        break;
#endif /*__APACHE_2*/
                    default:
                        printf("?\t");
                        break;
                }
                printf(" %.2f\t%.0f\t%ld\t",
                    (  score_record->times.tms_utime
                     + score_record->times.tms_stime
                     + score_record->times.tms_cutime
                     + score_record->times.tms_cstime) / tick,
#ifdef OPTIMIZE_TIMEOUTS
                     difftime(nowtime, ps_record.last_rtime),
#else
                     difftime(nowtime, apr_time_sec(score_record->last_used)),
#endif
                     (long) req_time);

                printf("%-1.1f\t%-2.2f\t%-2.2f\t", (float) conn_bytes / KBYTE, (float) my_bytes / MBYTE, (float) bytes / MBYTE);
                if (score_record->status == SERVER_BUSY_READ) {
                    printf("?\t\t ?\t\t..reading.. \n");
                }
                else {
                    char client[32];
                    int i;
                    for (i = 0; i < 32; i++)
                    client[i] = score_record->client[i];
                    client[31] = '\0';
#ifdef __APACHE_2
                    printf("%s\t %s \t%s\n", client, vhost, score_record->request);
#else /*__APACHE_2*/
                    printf("%s\t %s \t%s\n", client, "(unavailable)", score_record->request);
#endif /*__APACHE_2*/
                }

            }            /* !short_report */
        }            /* if (<active child>) */
    }                /* for () */

    printf
    ("\n   ---------------------------------------------------------------------------\n\
\n\
   Srv   Child Server number - generation\n\
   PID   OS process ID\n\
   Acc   Number of accesses this connection / this child / this slot\n\
   M     Mode of operation\n\
   CPU   CPU usage, number of seconds\n\
   SS    Seconds since beginning of most recent request\n\
   Req   Milliseconds required to process most recent request\n\
   Conn  Kilobytes transferred this connection\n\
   Child Megabytes transferred this child\n\
   Slot  Total megabytes transferred this slot\n\
\n\
   ---------------------------------------------------------------------------\n");
   }
#ifdef __APACHE_2
   free(scoreboard_image.servers);
#endif /* __APACHE_2*/
}

int get_perl_instance(int pid,char* field)
{
    int i,j,nbread;
    char cmdargs[3][512]={"","",""};
    char cmdfilename[50];
    char data;
    FILE * desc;

    /* onpen the /prov/pid/cmdline*/
    sprintf(cmdfilename,"/proc/%d/cmdline",pid);
    desc = fopen(cmdfilename, "r");
    if (desc == NULL) {
        printf("Unable to open the file %s\n",cmdfilename);
        return false;
    }

    /*parse the line : arg1\0arg2\0arg3\0*/
    for (i=0; i<3; i++){
        j=0;
        do{
            nbread=fread(&data,1,1, desc);
            if(nbread) {
                cmdargs[i][j]=data;
            }
            else {
                break;
            }
            j++;
        } while ( data != '\0' );
    }
    fclose(desc);
    /*if cmdline is : /usr/sbin/httpd -f /var/home/...*/
    if ( strcmp(cmdargs[0], "/usr/sbin/httpd") == 0 && strcmp(cmdargs[1], "-f") == 0 && strncmp(cmdargs[2],"/var/home/",10) == 0 ) {

        /* extract the domain name from /var/home/username/domainename/modperl.conf in field */
        int end,start;
        for(i=strlen(cmdargs[2]); cmdargs[2][i]!='/'; i--);
            end=i;
        for(i=end-1; cmdargs[2][i]!='/'; i--);
            start=i;
        for(i=0; i<(end -start-1); i++) {
            field[i]=cmdargs[2][start+i+1];
        }
        field[i]='\0';
        // the name of the instance is in an expected format
        return true;
    }
    else {
        strcpy(field, cmdargs[2]);
    }
    // the name of the instance is not regular
    return false ;
}

int check_perl_instance(char * instance,int pid)
{
    char field[256];
    if( get_perl_instance( pid,field) == false ) {
        return false;
    }
    if(strcmp(field,instance)==0){
        return true;
    }
    return false;
}

/*
*  list all the shm segment from the system
*  return shmid and pid of the first segnemt which belong to root
*  and which the size is sizeof(scoreboard)
*/
info_shm * get_scoreboard_shm (int mode,char * instance)
{
    int maxid, shmid, id;
    struct shmid_ds shmseg;
    unsigned long minim_shm_size;
    struct shm_info shm_info;
    struct ipc_perm *ipcp = &shmseg.shm_perm;
    info_shm * curinfo;
    info_shm * info = (struct info_shm*)malloc(sizeof(struct info_shm));
    curinfo = info;
    curinfo->next = NULL;
    int nbres=0;
    maxid = shmctl (0, SHM_INFO, (struct shmid_ds *) (void *) &shm_info);
    if (maxid < 0) {
        printf ("kernel not configured for shared memory\n");
        return NULL;
    }

    for (id = 0; id <= maxid; id++) {
        shmid = shmctl (id, SHM_STAT, &shmseg);
        if (shmid < 0) {
            continue;
        }

#ifdef __APACHE_2
        minim_shm_size = (sizeof(global_score)+sizeof(process_score)*HARD_SERVER_LIMIT+sizeof(worker_score)*HARD_SERVER_LIMIT);
        if((unsigned long) shmseg.shm_segsz >= minim_shm_size && ((shmseg.shm_segsz - minim_shm_size)%1024 ==0) ) {
#else /* __APACHE_2*/
        if(  (unsigned long) shmseg.shm_segsz == sizeof(scoreboard)) {
#endif /* __APACHE_2*/
            if ((mode == HTTPD_INFO && ipcp->uid == 0)  /* mode root */
               || (ipcp->uid != 0 &&  mode == PERL_INFO && /* mod perl */
               (instance == NULL || check_perl_instance(instance,shmseg.shm_cpid)))) /*with or without instance specified*/
            {
                if (nbres!=0) {
                    curinfo->next = (struct info_shm*)malloc(sizeof(struct info_shm));
                    curinfo = curinfo->next;
                }
                curinfo->shmid = shmid;
                curinfo->pid = shmseg.shm_cpid;
                curinfo->uid = ipcp->uid;
                curinfo->next = NULL;
                nbres++;
            }
        }
    }
    if (nbres == 0) {
        return NULL;
    }
    return info;
}

/**/
void free_info(info_shm * info)
{
    int i = 0 ;
    int j = 0 ;
    info_shm * cur = info;
    if(info!=NULL) {
        while(cur->next!=NULL) {
            cur=cur->next;
            i++;
        }
        for(;i>0;i--) {
            cur = info;
            for(j=0;j<i;j++)cur=cur->next;
            free(cur);
        }
    }
}

int matchname(char* path,char* name)
{
    int last_slash =0;
    int i;
    for (i=strlen(path); i>0;i--){
        if(path[i]=='/') {
            last_slash--;
            break;
        }
    last_slash++;
    }
    if(last_slash == strlen(name)){
        for(i=0; i<last_slash;i++) {
            if(path[strlen(path)-last_slash+i] != name[i]) {
                return false;
            }
        }
        return true;
    }
    return false;
}

/*
 * open a share memory segmemt given in parameter
 * display information about the httpd processus
 * set by scorboard
 */
int main(int argc, char *argv[])
{
    int *shared;        /* pointer to the shm */
    char * instance = NULL;
    char hostname[128];
    info_shm *info_shared_memory;
    pid_t forkid;
    int mode;
    int snmp;
    char instancename[256];

    /* on apache 2 the server limit is a parameter: not hard coded */
    #ifdef __APACHE_2
    HARD_SERVER_LIMIT  = get_server_limit();
    #endif /*__APACHE_2*/

    if (matchname(argv[0],HTTPD_STR) ){
        mode=HTTPD_INFO;
        if (( argc == 2) && (strcmp(argv[1], "-r") == 0)) {
            snmp=1;
        } else if (argc !=1 ){
            printf ("Usage: \n-r  Dump raw stats\n");
            return -1;
        }
    }
    else {
        if (matchname(argv[0],MODPERL_STR)) {
            mode = PERL_INFO;
            #ifdef __APACHE_2
            HARD_SERVER_LIMIT  = 256;/*here I don't know how to get this number*/
            #endif /*__APACHE_2*/
            if( argc ==3 && strcmp(argv[1],"-d") == 0 ) {
                instance = argv[2];
            }
            else if ( argc == 2 && strcmp(argv[1],"-r") == 0) {
                snmp=1;
            }
            else {
                if(argc !=1){
                    printf ("Usage : \n%s\n%s -d domain \n",argv[0],argv[0]);
                    return -1;
                }
            }
        }
        else {
            printf("executable launch with the wrong name : %s or %s \n",HTTPD_STR,MODPERL_STR);
            return -1;
        }
    }


    // Get shared memory information from the system
    info_shared_memory = get_scoreboard_shm(mode,instance);
    if(info_shared_memory == NULL && instance!=NULL){
        printf("no instance of %s found\n",instance);
    }
    while(info_shared_memory !=NULL){
        if(info_shared_memory == NULL){
            perror("unable to find the right shared memory segmemt :\n");
            exit(0);
        }

        gethostname(hostname, 128);
        if (mode == PERL_INFO) {
           if (get_perl_instance( info_shared_memory->pid,instancename) == false) {
                perror("Unable to instantiate ModPerl shared memory");
                exit(0);
            }
            fflush(NULL);
        }

        if (snmp == 0) {
            /*display the begining of the message*/
            printf("  Apache Server Status for %s\n",hostname);
            printf("  Process belongs to  %s \n  ",((struct passwd *)getpwuid(info_shared_memory->uid))->pw_name);
            if(mode == PERL_INFO) { /*display the name of the modperl instance */
                printf("Modperl : %s \n  ",instancename);
            }
        }

        // call httpd -v to get server information
        forkid = fork();
        if (forkid == 0) {                // child
            // Code only executed by child process
            if (snmp == 0) {
                execl("/usr/sbin/httpd","/usr/sbin/httpd", "-v", (char *) 0);
            }
            else {
                freopen("/dev/null","w",stdout);
                execl("/usr/sbin/httpd","/usr/sbin/httpd", "-v", (char *) 0);
            }
        }
        else {
            // wait until the child quit
            int childExitStatus;
            wait(&childExitStatus);

            // init assciation between state and displayed state
            status_init();
            // open shared memory
            shared = shmat(info_shared_memory->shmid, (void *) 0, 0);

            if (shared == NULL) {
                printf("error shmat\n");
                exit(0);
            }
            if (snmp == 1 && mode == PERL_INFO)
                printf("%s=(",instancename);

            // display information from the shared memory
            displayApacheShm((scoreboard *) shared, info_shared_memory->pid, mode, snmp);
            shmdt(shared);

            if (snmp == 1 && mode == PERL_INFO)
                printf(")  ");
        }

        info_shared_memory = info_shared_memory->next;
    }
    free_info(info_shared_memory);
    return 0;
}
