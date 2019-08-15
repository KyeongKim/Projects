/* GT-ID : KKIM651 */
/* gfserver.c    */

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>
#include <getopt.h>

#include "gfserver.h"

/* 
 * Modify this file to implement the interface specified in
 * gfserver.h.
 */

#define BUFFSIZE 4096

const char *SCHEME = "GETFILE";
const char *GET = "GET";
const char *FILENOTFOUND = "FILE_NOT_FOUND";
const char *ERROR = "ERROR";
const char *OK = "OK";
const char *HDRMKR = "\r\n\r\n";

int startsWith(const char *f, const char *s);

typedef struct gfserver_t
{
    int sockfd;
    void *arg;
    int max_npending;
    unsigned short port;
    ssize_t (*handlerfunc)(gfcontext_t *, char *, void *);
} gfserver_t;

typedef struct gfcontext_t
{
    int sockfd;
    size_t fileLen;
} gfcontext_t;

gfcontext_t *gfc_create();

void gfs_abort(gfcontext_t *ctx){

    if (close(ctx->sockfd) < 0)
    {
        fprintf(stderr, "[ERROR]: GFS failed on closing socket.\n");
    }

    if (ctx != NULL)
    {
        free(ctx);
        ctx = NULL;
    }
}

gfserver_t* gfserver_create(){
    gfserver_t *gfs = (gfserver_t *) malloc(sizeof(gfserver_t));
    gfs->sockfd = 0;
    gfs->max_npending = 0;

    return gfs;
}

gfcontext_t* gfclient_create(){
    gfcontext_t *gfc = (gfcontext_t *) malloc(sizeof(gfcontext_t));
    gfc->sockfd = 0;
    gfc->fileLen = 0;

    return gfc;
}

ssize_t gfs_send(gfcontext_t *ctx, void *data, size_t len){
    ssize_t bytes_sent;
    bytes_sent = write(ctx->sockfd, data, len);

    return bytes_sent;
}

ssize_t gfs_sendheader(gfcontext_t *ctx, gfstatus_t status, size_t file_siz_len){

    ssize_t bytes_sent = 0;
    char *rspHdr;
    char file_siz_length[320];
    int func_ret_status = 0;

    ctx->fileLen = 0;

    if (status == GF_ERROR)
    {
        rspHdr = (char *) malloc(strlen(SCHEME) + strlen(ERROR) + strlen(HDRMKR) + 3);
        strcpy(rspHdr, "GETFILE ");
        strcat(rspHdr, ERROR);
        strcat(rspHdr, " \r\n\r\n");
    } 
    else if (status == GF_FILE_NOT_FOUND)
    {
        rspHdr = (char *) malloc(strlen(SCHEME) + strlen(FILENOTFOUND) + strlen(HDRMKR) + 3);
        strcpy(rspHdr, "GETFILE ");
        strcat(rspHdr, FILENOTFOUND);
        strcat(rspHdr, " \r\n\r\n");
    }
    else // GF_OK
    {
        sprintf(file_siz_length, "%zu", file_siz_len);
        ctx->fileLen = file_siz_len;
        rspHdr = (char *) malloc(strlen(SCHEME) + strlen(OK) + strlen(file_siz_length) + strlen(HDRMKR) + 4);
        strcpy(rspHdr, "GETFILE ");
        strcat(rspHdr, "OK ");
        strcat(rspHdr, file_siz_length);
        strcat(rspHdr, " \r\n\r\n");
    }

    ssize_t size_sent = 0;
    ssize_t length = 0;
    char *ptr;

    length = strlen(rspHdr);
    //fprintf(stdout, "[INFO]: GFS sending response header - %s, %d ...\n", rspHdr, (int) length);
    //fflush(stdout);
    while (length > 0)
    {
        if ((bytes_sent = send(ctx->sockfd, rspHdr+size_sent, length, 0)) < 0)
        {
            fprintf(stderr, "[ERROR]: GFS failed on sending response header(<0).\n");
            func_ret_status = -1;
            goto final;
        }
        ptr += bytes_sent;
        length -= bytes_sent;
        size_sent += bytes_sent;
    }

    /* case to handle the abort */
    if (size_sent < sizeof(rspHdr))
    {
            fprintf(stderr, "[ERROR]: GFS failed on sending response header.\n");
            func_ret_status = -1;
            goto final;
    }

    final:
        if (rspHdr != NULL)
        {
            free(rspHdr);
        }
        if (func_ret_status == 0)
            return size_sent;
        else
            return func_ret_status;
}

void gfserver_serve(gfserver_t *gfs){

    int optval;
    struct sockaddr_in serveraddr;
    struct sockaddr_in clientaddr;
    socklen_t client_addr_len;
    int conn_socket_fd;
    int ret;

    ssize_t hdr_bytes_rcvd = 0;
    ssize_t hdr_bytes_cnt = 0;
    char hdrStr[BUFFSIZE];
    char rcvbuf[BUFFSIZE];
    size_t max_hdr_size = BUFFSIZE;

    /* Create Socket Connection with AF_NET and of type SOCK_STREAM, which
     * corresponds to TCP connection */
    gfs->sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (gfs->sockfd < 0)
    {
        fprintf(stderr, "[ERROR]: Server opening socket connection.\n");
    }

    /* For mostly debugging purposes to get around "Address already in use" error */
    optval = 1;
    setsockopt(gfs->sockfd, SOL_SOCKET,
               SO_REUSEADDR, (const void *) &optval , sizeof(int));

    /* Clear server and client address structure */
    memset(&serveraddr, 0, sizeof(serveraddr));
    memset(&clientaddr, 0, sizeof(clientaddr));

    /* Build IP Socket Address Scheme */
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(gfs->port);

    /* Prepare connection in sockaddr */
    ret = bind(gfs->sockfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr));
    if (ret < 0)
    {
        fprintf(stderr, "[ERROR]: Binding address/port on sockaddr.\n");
    }

    /* Start listening for connections */
    if (listen(gfs->sockfd, gfs->max_npending) < 0)
    {
        fprintf(stderr, "[ERROR]: Server failed on Listening connections.\n");
    }
    //printf("Server listening on port: %d ...\n", ntohs(serveraddr.sin_port));

    /* Main loop */
    for ( ; ; )
    {
        conn_socket_fd = 0;
        client_addr_len = 0;
        
        /* Wait for connection from any device */
        /* blocks until that happens           */
        printf("Waiting for client connection...\n");
        conn_socket_fd = accept(gfs->sockfd, (struct sockaddr *) &clientaddr,
                                &client_addr_len);
        if (conn_socket_fd < 0)
        {
            fprintf(stderr, "{ERROR]: Server failed to accept connection request.\n");
            continue;
        }

        gfcontext_t *ctx = (gfcontext_t *) malloc(sizeof(gfcontext_t));
        ctx->sockfd = conn_socket_fd;

        while (1)
        {
            hdr_bytes_rcvd = recv(conn_socket_fd, rcvbuf, max_hdr_size, 0);

            if (hdr_bytes_rcvd > 0)
            {
                if (hdr_bytes_cnt + hdr_bytes_rcvd <= max_hdr_size)
                {
                    memcpy(hdrStr + hdr_bytes_cnt, rcvbuf, hdr_bytes_rcvd);
                    hdr_bytes_cnt += hdr_bytes_rcvd;
                }

                char *scheme = strtok(hdrStr, " ");
                char *method = strtok(NULL, " ");
                char *path = strtok(NULL, " \r\n\r\n");

                //fprintf(stdout, "SCHEME: %s, METHOD: %s, PATH: %s\n.", scheme, method, path);
                //fflush(stdout);

                if (NULL == scheme)
                {
                    fprintf(stderr, "[ERROR]: Invalid request header(NULL) - %s\n", scheme);
                    gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);
                    break;
                }
                else if (NULL == method)
                {
                    fprintf(stderr, "[ERROR]: Invalid request method(NULL) - %s\n", scheme);
                    gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0); 
                    break;
                }
                else if ((strcmp(scheme, SCHEME) != 0)) 
                {
                    fprintf(stderr, "[ERROR]: Invalid request scheme - %s\n", scheme);
                    gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);  
                    break;
                }
                else if (strcmp(method, GET) !=0)
                {
                    fprintf(stderr, "[ERROR]: Invalid request method - %s\n", method);
                    gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);  
                    break;
                }
                else if (startsWith(path, (char*)"/") != 0)
                {
                    fprintf(stderr, "[ERROR]: Invalid request path - %s\n", path);
                    gfs_sendheader(ctx, GF_FILE_NOT_FOUND, 0);  
                    break;
                }
                else
                {
                    /* Request header valid */
                    ssize_t bytes_sent;

                    bytes_sent = gfs->handlerfunc(ctx, path, gfs->arg);
                    if (bytes_sent < 0)
                    {
                        gfs_sendheader(ctx, GF_ERROR, 0);
                    }
                    hdr_bytes_cnt = 0;
                    break;
                }
            }
            else if (hdr_bytes_rcvd == 0)
            {
                fprintf(stderr, "[ERROR]: Failed to receive header data from socket(Closed).\n");
                break;
            }
            else if (hdr_bytes_rcvd == -1)
            {
                fprintf(stderr, "[ERROR]: Failed to receive header data from socket.\n");
                break;
            }

        } // while loop
        
    } // main for loop

}

void gfserver_set_handlerarg(gfserver_t *gfs, void* arg){
    gfs->arg = arg;
}

void gfserver_set_handler(gfserver_t *gfs, ssize_t (*handler)(gfcontext_t *, char *, void*)){
    gfs->handlerfunc = handler;
}

void gfserver_set_maxpending(gfserver_t *gfs, int max_npending){
    max_npending > 0 ? gfs->max_npending = max_npending : exit(0);
}

void gfserver_set_port(gfserver_t *gfs, unsigned short port){
    gfs->port = port;
}

int startsWith(const char *f, const char *s)
{
    if (strncmp(f, s, strlen(s)) == 0)
        return 0;
    else
        return -1;
}
