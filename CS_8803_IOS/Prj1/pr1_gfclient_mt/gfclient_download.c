/* GT-ID : KKIM651 */
/* gfclient_download.c  */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>
#include <getopt.h>
#include <pthread.h>

#include "steque.h"
#include "gfclient.h"
#include "workload.h"

#define USAGE                                                                 \
"usage:\n"                                                                    \
"  webclient [options]\n"                                                     \
"options:\n"                                                                  \
"  -h                  Show this help message\n"                              \
"  -n [num_requests]   Requests download per thread (Default: 2)\n"           \
"  -p [server_port]    Server port (Default: 8140)\n"                         \
"  -s [server_addr]    Server address (Default: 0.0.0.0)\n"                   \
"  -t [nthreads]       Number of threads (Default 2)\n"                       \
"  -w [workload_path]  Path to workload file (Default: workload.txt)\n"       \

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
  {"help",          no_argument,            NULL,           'h'},
  {"nthreads",      required_argument,      NULL,           't'},
  {"nrequests",     required_argument,      NULL,           'n'},
  {"server",        required_argument,      NULL,           's'},
  {"port",          required_argument,      NULL,           'p'},
  {"workload-path", required_argument,      NULL,           'w'},
  {NULL,            0,                      NULL,             0}
};

/* struct for pthread_create argument */
typedef struct wthread_func_struct {
    int nreq;
    int wthnum;
} wthread_func_struct;

/* Global mutex */
pthread_mutex_t mutex;
pthread_cond_t cond;

static steque_t *client_work_q;

static void Usage() {
  fprintf(stdout, "%s", USAGE);
}

static void localPath(char *req_path, char *local_path){
  static int counter = 0;

  sprintf(local_path, "%s-%06d", &req_path[1], counter++);
}

static FILE* openFile(char *path){
  char *cur, *prev;
  FILE *ans;

  /* Make the directory if it isn't there */
  prev = path;
  while(NULL != (cur = strchr(prev+1, '/'))){
    *cur = '\0';

    if (0 > mkdir(&path[0], S_IRWXU)){
      if (errno != EEXIST){
        perror("Unable to create directory");
        exit(EXIT_FAILURE);
      }
    }

    *cur = '/';
    prev = cur;
  }

  if( NULL == (ans = fopen(&path[0], "w"))){
    perror("Unable to open file");
    exit(EXIT_FAILURE);
  }

  return ans;
}

/* Callbacks ========================================================= */
static void writecb(void* data, size_t data_len, void *arg){
  FILE *file = (FILE*) arg;

  fwrite(data, 1, data_len, file);
}

char *server = "localhost";
unsigned short port = 8140;

void *client_mt(void *args)
{

  wthread_func_struct *wargs = (wthread_func_struct*) args;

  int nr = wargs->nreq;
  int wn = wargs->wthnum;
  char *req_path = NULL;
  char local_path[512];
  FILE *file = NULL;
  gfcrequest_t *gfr = NULL;
  int returncode = 0;

  fprintf(stdout, "Worker Thread #: %d\n", wn);
  fflush(stdout);
  fprintf(stdout, "Number of Requests: %d\n", nr);
  fflush(stdout);

  /* repeat forever */
  for (; ;)
  {
        /* lock the mutex to assure exclusive access */
        pthread_mutex_lock(&mutex);
        /* Wait until there is an item in the queue */
        while (steque_isempty(client_work_q)) {
            pthread_cond_wait(&cond, &mutex);
        }
        
        /* Dequeue an item */
        req_path = (char *)steque_pop(client_work_q);
        
        --nr;

        /* Create a request */
        if (NULL != req_path)
        {
          localPath(req_path, local_path);

          file = openFile(local_path);

          gfr = gfc_create();
          gfc_set_server(gfr, server);
          gfc_set_path(gfr, req_path);
          gfc_set_port(gfr, port);
          gfc_set_writefunc(gfr, writecb);
          gfc_set_writearg(gfr, file);

          fprintf(stdout, "Requesting %s%s\n", server, req_path);
          fflush(stdout);

          pthread_mutex_unlock(&mutex);

          /* Send the request and receive the response */
          /* according to the GetFile protocol         */
          if ( 0 > (returncode = gfc_perform(gfr))){
            fprintf(stdout, "gfc_perform returned an error %d\n", returncode);
            fclose(file);
            if ( 0 > unlink(local_path))
              fprintf(stderr, "unlink failed on %s\n", local_path);
          }
          else {
              fclose(file);
          }

          /* Check the status of the request */
          if ( gfc_get_status(gfr) != GF_OK){
            if ( 0 > unlink(local_path))
              fprintf(stderr, "unlink failed on %s\n", local_path);
          }

          fprintf(stdout, "Status: %s\n", gfc_strstatus(gfc_get_status(gfr)));
          fflush(stdout);
          fprintf(stdout, "Received %zu of %zu bytes\n", gfc_get_bytesreceived(gfr), gfc_get_filelen(gfr));
          fflush(stdout);

          gfc_cleanup(gfr);

        } // if req_path loop
        else
        {
          pthread_mutex_unlock(&mutex);
        }

        /* If all requests are processed, exit the loop */
        if (0 >= nr)
          break;

  } // for loop

  return 0;
}

/* Main ========================================================= */
int main(int argc, char **argv) {
/* COMMAND LINE OPTIONS ============================================= */
  char *workload_path = "workload.txt";

  int i = 0;
  int option_char = 0;
  int nrequests = 2;
  int nthreads = 2;
  char *req_path = NULL;

  // Parse and set command line arguments
  while ((option_char = getopt_long(argc, argv, "hn:p:s:t:w:", gLongOptions, NULL)) != -1) {
    switch (option_char) {
      case 'h': // help
        Usage();
        exit(0);
        break;                      
      case 'n': // nrequests
        nrequests = atoi(optarg);
        break;
      case 'p': // port
        port = atoi(optarg);
        break;
      case 's': // server
        server = optarg;
        break;
      case 't': // nthreads
        nthreads = atoi(optarg);
        break;
      case 'w': // workload-path
        workload_path = optarg;
        break;
      default:
        Usage();
        exit(1);
    }
  }

  /* Load workload file */
  if( EXIT_SUCCESS != workload_init(workload_path)){
    fprintf(stderr, "Unable to load workload file %s.\n", workload_path);
    exit(EXIT_FAILURE);
  }

  gfc_global_init();

  /* Initialize mutex and conditional variable */
  pthread_mutex_init (&mutex, NULL);
  pthread_cond_init (&cond, NULL);

  fprintf(stdout, "Num. of Requests: %d\n", nrequests);
  fflush(stdout);

  /* Pool of worker threads are initialized based on number of client worker *
     threads specified with a command line required_argument                 */
  pthread_t thread_id[nthreads];
  client_work_q = malloc(sizeof(steque_t));
  steque_init(client_work_q);
  wthread_func_struct *wth_args;
  wth_args = malloc(nthreads * sizeof(wthread_func_struct));  
  int tcnt = 0;

  /* Create and run workder threads */
  for (int tc=0; tc < nthreads; tc++)
  {
    (wth_args+tcnt)->nreq = nrequests;
    (wth_args+tcnt)->wthnum = tcnt;
    if (pthread_create(&thread_id[tc], NULL, client_mt, (void *) (wth_args+tcnt)) != 0)
      continue;
    else 
      tcnt++;
  }

  /*Making the requests...*/
  for(i = 0; i < nrequests * nthreads; i++){
    req_path = workload_get_path();

    if(strlen(req_path) > 256){
      fprintf(stderr, "Request path exceeded maximum of 256 characters\n.");
      free(client_work_q);
      exit(EXIT_FAILURE);
    }

    pthread_mutex_lock(&mutex);

    /* enqueue the file path to the queue */
    steque_enqueue(client_work_q, req_path);

    pthread_cond_signal(&cond);

    pthread_mutex_unlock(&mutex);
  }

  /* Wait for all requests to be served */
  for (i = 0; i < tcnt; i++) {
      if (pthread_join(thread_id[i], NULL) < 0) {
            fprintf(stderr, "[ERROR]: Failed to join thread id %d\n", i);
      }
  }

  /* Clean up */
  gfc_global_cleanup();

  pthread_mutex_destroy(&mutex);

  pthread_cond_destroy(&cond);

  free(client_work_q);

  free(wth_args);

  return 0;
}  