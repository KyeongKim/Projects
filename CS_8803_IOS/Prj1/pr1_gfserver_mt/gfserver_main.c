/* GT-ID : KKIM651 */
/* gfserver_main.c    */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <string.h>
#include <sys/signal.h>
#include <stdlib.h>
#include <netdb.h>
#include <getopt.h>
#include <pthread.h>

#include "steque.h"
#include "gfserver.h"
#include "content.h"

#define USAGE                                                                 \
"usage:\n"                                                                    \
"  gfserver_main [options]\n"                                                 \
"options:\n"                                                                  \
"  -t [nthreads]       Number of threads (Default: 2)\n"                      \
"  -p [listen_port]    Listen port (Default: 8140)\n"                         \
"  -c [content_file]   Content file mapping keys to content files\n"          \
"  -h                  Show this help message.\n"                             \

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
  {"port",          required_argument,      NULL,           'p'},
  {"nthreads",      required_argument,      NULL,           't'},
  {"content",       required_argument,      NULL,           'c'},
  {"help",          no_argument,            NULL,           'h'},
  {NULL,            0,                      NULL,             0}
};

/* struct for pthread_create argument */
typedef struct wthread_func_struct {
    int ntrds;
    int wthnum;
} wthread_func_struct;

extern ssize_t handler_get(gfcontext_t *ctx, char *path, void* arg);
extern ssize_t handler_mt(void *args);
extern steque_t *handler_work_q;
extern pthread_mutex_t mutex;

wthread_func_struct *wth_args;
pthread_t *thread_id;
int nthreads = 2;

void gfs_init_work_threads();
void gfs_create_run_work_threads(pthread_t *wthread, int nthr);

void gfs_cancel_threads_on_sigint();
void gfs_cleanup();

static void _sig_handler(int signo){
  if (signo == SIGINT || signo == SIGTERM){
    gfs_cancel_threads_on_sigint();
    gfs_cleanup();
    exit(signo);
  }
}

void gfs_cleanup()
{
  free(handler_work_q);
  free(wth_args);
  free(thread_id);
}

void gfs_cancel_threads_on_sigint()
{
  for (int t=0; t < nthreads; t++)
    {
      pthread_cancel(thread_id[t]);
      fprintf(stdout, "Sent cancel signal to thread: %lu \n", thread_id[t]);
      fflush(stdout);
    }
    for (int t=0; t < nthreads; t++)
    {
      pthread_join(thread_id[t], NULL);
      fprintf(stdout, "Joined thread: %lu \n", thread_id[t]);
      fflush(stdout);
    }
}

/* Main ========================================================= */
int main(int argc, char **argv) {
  int option_char = 0;
  unsigned short port = 8140;
  char *content = "content.txt";
  gfserver_t *gfs = NULL;
  //int nthreads = 2;

  if (signal(SIGINT, _sig_handler) == SIG_ERR){
    fprintf(stderr,"Can't catch SIGINT...exiting.\n");
    gfs_cleanup();
    exit(EXIT_FAILURE);
  }

  if (signal(SIGTERM, _sig_handler) == SIG_ERR){
    fprintf(stderr,"Can't catch SIGTERM...exiting.\n");
    gfs_cleanup();
    exit(EXIT_FAILURE);
  }

  // Parse and set command line arguments
  while ((option_char = getopt_long(argc, argv, "p:t:hc:", gLongOptions, NULL)) != -1) {
    switch (option_char) {
      default:
        fprintf(stderr, "%s", USAGE);
        exit(1);
      case 'p': // listen-port
        port = atoi(optarg);
        break;
      case 't': // nthreads
        nthreads = atoi(optarg);
        break;
      case 'h': // help
        fprintf(stdout, "%s", USAGE);
        exit(0);
        break;       
      case 'c': // file-path
        content = optarg;
        break;                                          
    }
  }

  /* not useful, but it ensures the initial code builds without warnings */
  if (nthreads < 1) {
    nthreads = 1;
  }

  thread_id = malloc(nthreads * sizeof(pthread_t));
  
  content_init(content);

  /*Initializing server*/
  gfs = gfserver_create();

  /*Setting options*/
  gfserver_set_port(gfs, port);
  gfserver_set_maxpending(gfs, 64);
  gfserver_set_handler(gfs, handler_get);
  gfserver_set_handlerarg(gfs, NULL);

  /* Initialize request queue, mutex and conditional variables */
  gfs_init_work_threads(thread_id, nthreads);

  /* Create and start workder threads */
  gfs_create_run_work_threads(thread_id, nthreads);

  /*Loops forever*/
  gfserver_serve(gfs);

  /* Clean up */
  for (int t=0; t < nthreads; t++)
  {
    pthread_join(thread_id[t], NULL);
  }

  pthread_mutex_destroy(&mutex);

  steque_destroy(handler_work_q);

  gfs_cleanup();

  return 0;
  
} // main

void gfs_init_work_threads()
{
  pthread_mutex_init(&mutex, NULL);
  handler_work_q = malloc(sizeof(steque_t));
  steque_init(handler_work_q);
}

void gfs_create_run_work_threads(pthread_t *wthread, int nthr)
{
  wth_args = malloc(nthr * sizeof(wthread_func_struct)); 
  int tcnt = 0;
  for (int tc=0; tc < nthr; tc++)
  {
    (wth_args+tcnt)->ntrds = nthr;
    (wth_args+tcnt)->wthnum = tcnt;
    if (pthread_create(&wthread[tc], NULL, (void *) handler_mt, (void *) (wth_args+tcnt)) != 0)
      continue;
    else 
      tcnt++;
  }
}