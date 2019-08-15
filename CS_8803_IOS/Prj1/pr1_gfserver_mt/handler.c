/* GT-ID : KKIM651 */
/* handler.c       */

#include <stdlib.h>
#include <fcntl.h>
#include <curl/curl.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <pthread.h>

#include "steque.h"
#include "gfserver.h"
#include "content.h"

#define BUFFER_SIZE 4000

/* Global mutex and condition variable */
pthread_mutex_t mutex;
pthread_cond_t cond;

steque_t *handler_work_q;

/* struct for pthread_create argument */
typedef struct wthread_func_struct {
    int ntrds;
    int wthnum;
} wthread_func_struct;

void gfh_init(gfcontext_t *ctx, char *path);

typedef struct gfh_ctx
{
	gfcontext_t *ctx;
	char *path;
} gfh_ctx;

gfh_ctx *hctx;

/* Workder thread function */
ssize_t handler_mt(void *args)
{
	wthread_func_struct *wargs = (wthread_func_struct*) args;
	int nr = wargs->ntrds;
  	int wn = wargs->wthnum;
	int fildes;
	size_t file_len, bytes_transferred;
	ssize_t read_len, write_len;
	char buffer[BUFFER_SIZE];
	gfcontext_t *ctx;
	char *path;
	gfh_ctx *work_req;

	fprintf(stdout, "Total number of Threads: %d\n", nr);
	fflush(stdout);

	pthread_setcanceltype(PTHREAD_CANCEL_ASYNCHRONOUS, 0);
	/* repeat forever */
	for (; ;)
	{
    	pthread_mutex_lock(&mutex);
		if (!steque_isempty(handler_work_q))
		{
			fprintf(stdout, "Worker Thread #: %d\n", wn);
			fflush(stdout);
			/* Dequeue a request from the request queue */
	        work_req = (gfh_ctx *) steque_pop(handler_work_q);
	        ctx = work_req->ctx;
	        path = work_req->path;

	        pthread_mutex_unlock(&mutex);

			if( 0 > (fildes = content_get(path)))
				return gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);

			/* Determine the file size */
			file_len = lseek(fildes, 0, SEEK_END);

			gfs_sendheader(ctx, GF_OK, file_len);

			/* Send the file in chunks */
			bytes_transferred = 0;
			while(bytes_transferred < file_len){
				read_len = pread(fildes, buffer, BUFFER_SIZE, bytes_transferred);
				if (read_len <= 0){
					fprintf(stderr, "handle_with_file read error, %zd, %zu, %zu\n", read_len, bytes_transferred, file_len );
					gfs_abort(ctx);
					return -1;
				}
				write_len = gfs_send(ctx, buffer, read_len);
				if (write_len != read_len){
					fprintf(stderr, "handle_with_file write error, %zd != %zd\n", write_len, read_len);
					gfs_abort(ctx);
					return -1;
				}
				bytes_transferred += write_len;
			}

			free(work_req);

		} // if
		else
		{
			pthread_mutex_unlock(&mutex);
		}

	} // forever for loop

	return bytes_transferred;
}

ssize_t handler_get(gfcontext_t *ctx, char *path, void* arg){

	// Lock mutex
	pthread_mutex_lock(&mutex);

	// Init handler context
	gfh_init(ctx, path);

    // Queue requests
    steque_enqueue(handler_work_q, hctx);

    // unlock mutex
    pthread_mutex_unlock(&mutex);

	return 0;

}

void gfh_init(gfcontext_t *ctx, char *path)
{
	hctx = malloc(sizeof(gfh_ctx));
	hctx->ctx = ctx;
	hctx->path = path;
}