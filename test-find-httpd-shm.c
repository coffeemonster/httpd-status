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
#include <proc/escape.h>
#include <proc/readproc.h>
#include <pwd.h>


// Includes - apache, apr, apr-util
// --------------------------------------------------------------------------
// #include <httpd/ap_config.h>
// #include <httpd/httpd.h>
// #include <httpd/scoreboard.h>
#include "ap_config.h"
#include "httpd.h"
#include "scoreboard.h"

#ifdef __APACHE_2
#undef HARD_SERVER_LIMIT
// #include <apr_time.h>
// #include <httpd/mpm_common.h>
// #include <httpd/ap_mpm.h>
#include "apr_time.h"
#include "mpm_common.h"
#include "ap_mpm.h"
#endif /*__APACHE_2*/


// Includes - Project
// --------------------------------------------------------------------------
#include "ipcutils.h"


// Constants
// --------------------------------------------------------------------------
#define true            1
#define false           0
#define KBYTE           1024
#define MBYTE           1048576L
#define GBYTE           1073741824L
#define HTTPD_INFO      0
#define PERL_INFO       1
#define MODPERL_STR     "modperl-stat"
#define HTTPD_STR       "httpd-stat"

// -- Begin Configuration --------------------
//
#define HTTPD_BIN               "/var/home/alister_mp/apache24/bin/httpd"
#define HTTPD_CONF              "/var/home/alister_mp/apache24/conf/httpd.conf"
#define HTTPD_CONF_CUSTOM_DIR   "/var/home/alister_mp/apache24/conf/extra/"
//
// -- End Configuration ----------------------

int DEBUG = 0;


// Pre-declared Types + Functions
// --------------------------------------------------------------------------
typedef struct shm_summary {
    int shmid;
    int pid;
    int uid;
    struct shm_summary * next;
} shm_summary;
int shm_get_summary(struct shm_summary **shmds);


/*
 * open a share memory segmemt given in parameter
 * display information about the httpd process
 * set by scorboard
 */
int main(int argc, char *argv[])
{
    printf("main()\n");
    
    struct passwd *pw;
    struct shm_summary *shmds, *shmdsp;

    
    if (shm_get_summary(&shmds) < 1) {
        printf("No shared memory instances found.\n");
        return;
    }

    for (shmdsp = shmds; shmdsp->next != NULL; shmdsp = shmdsp->next) {
        // ipc_print_perms(stdout, &shmdsp->shm_perm);
        pw = getpwuid(shmdsp->uid);
        // creator/current
        printf("shm [%d] %s => %u\n",
            shmdsp->shmid, pw->pw_name, shmdsp->pid
        );
    }

    return 0;
}



/* 
 * shm_get_summary(*data)
 *
 *      populates given shm_summary list
 *      returns number of shm's accessible
 *
 * see also - util-linux-2.23/sys-utils/ipcutils.c
 */
int shm_get_summary(struct shm_summary **shmds)
{
    // allocate memory for linked list
    struct shm_summary *dp;
    dp = *shmds = calloc(1, sizeof(struct shm_summary));
    dp->next = NULL;

    // SHM_INFO - linux specific
    //          - returns max-index of kernel shared memory array
    struct shm_info shm_info;
    int max_index = shmctl(0, SHM_INFO, (struct shmid_ds *) &shm_info);

    int i = 0, count = 0;
    while (i <= max_index  &&  shm_info.used_ids) {

        // lookup kernal shared-mem array
        struct shmid_ds shmseg;
        int shmid = shmctl(i, SHM_STAT, &shmseg);
        if (shmid >= 0) {
            // see man shmctl(2) for definitions and list of data available
            dp->shmid = shmid;
            dp->pid   = shmseg.shm_cpid;
            dp->uid   = shmseg.shm_perm.uid;

            // allocate next
            dp->next = calloc(1, sizeof(struct shm_summary));
            dp = dp->next;
            dp->next = NULL;
            count++;
        }
        i++;
    }
    return count;
}




