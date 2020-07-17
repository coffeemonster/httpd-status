#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/shm.h>
#include <proc/readproc.h>
#include "ap_config.h"
#include "ap_config_layout.h"
#include "httpd.h"
#include "scoreboard.h"

// Run:
//      gcc test-shmat.c -o go.bin -I/home/alister_mp/src/httpd-2.4.4/include -D_GNU_SOURCE 
//      ./go.bin



#ifdef AP_SERVER_MAJORVERSION_NUMBER
#define __APACHE_2
#else
#define __APACHE_1
typedef short_score worker_score;
#endif

// How to get access to scoreboard of shared-memory segment ?
int main(int argc, char *argv[])
{
    // SHM_INFO - linux specific, returns max-index of kernel shared memory array
    struct shm_info shm_info;
    int max_index = shmctl(0, SHM_INFO, (struct shmid_ds *) &shm_info);
    if (!shm_info.used_ids) { return; }

    int i = 0, shmid = -1, pid = 0;
    for (i = 0; i <= max_index; i++) {
        struct shmid_ds shmseg;
        shmid = shmctl(i, SHM_STAT, &shmseg);
        if (shmid >= 0) {
            printf ("shared-memory %d -> shmid: %d, pid: %d\n", i, shmid, shmseg.shm_cpid );
            // check is actually httpd `cat /proc/12345/cmdline`
            break;
        }
    }

    if (shmid >=0) {
        // we are given back a ptr to the shared-memory-blob.
        scoreboard * sb_ptr = shmat(shmid, (void *) 0, 0);
        if (sb_ptr == NULL) { printf("Error shmat\n"); return; }

        ap_version_t version = { .major = AP_SERVER_MAJORVERSION_NUMBER, .minor = AP_SERVER_MINORVERSION_NUMBER };
        printf("version: %d.%d\n", version.major, version.minor);

        int i;
        scoreboard sb;

#if __APACHE_1

        // apache-1.3 is a struct of arrays.
        sb = *sb_ptr;
        int server_limit = HARD_SERVER_LIMIT;

#else /* __APACHE_1 */

        // apache-2.X is a struct of pointers
        //
        // hack the fact we know where the memory lines up with the scoreboard datastructure
        //
        // memory allocation as shown by "scoreboard.c" : ap_calc_scoreboard_size() and ap_init_scoreboard()"
        //      global_score    
        //      process_score[x children]
        //      worker_score [x children * threads]
        //      
        
        sb.global   = (global_score*) sb_ptr;
        printf("scoreboard.global.server_limit = %d\n", sb.global->server_limit); // 24 == ServerLimit directive
        printf("scoreboard.global.thread_limit = %d\n", sb.global->thread_limit); // 64 == event.c:DEFAULT_THREAD_LIMIT
       
        sb.parent   = (process_score*)((char*) sb_ptr + sizeof(global_score));
        printf("scoreboard.parent (%p) = %d\n", sb.parent, sb.parent->pid);
        for (i = 0; i < sb.global->server_limit; i++) {
            printf("scoreboard.parent[%d] = pid(%d) => gen(%d)\n", i, sb.parent[i].pid, (int)sb.parent[i].generation);
            if (sb.parent[i].pid == 0) { break; }
        }
return;
        // workers
        // need to allocate space for array of pointers for our local scoreboard struct
        sb.servers  = (worker_score**) malloc( sb.global->server_limit * sizeof(worker_score*) );
        worker_score * ws;
        for (i = 0; i < sb.global->server_limit * sb.global->thread_limit; i++) {
            ws = (worker_score*)( (char*) sb_ptr
                                  + sizeof(global_score)
                                  + sizeof(process_score) * sb.global->server_limit
                                  + sizeof(worker_score)  * i
                                );
            sb.servers[i] = ws;
            if (ws->pid > 0) {
                printf("scoreboard.servers[%d] pid(%d), status(%d)\n", i, ws->pid, ws->status);
            }
        }

#endif /* __APACHE_1 */

        shmdt((void*) sb_ptr);
    }
}




