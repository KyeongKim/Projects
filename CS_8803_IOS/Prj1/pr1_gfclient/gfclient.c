/* GT-ID : KKIM651 */
/* gfclient.c    */

#include <unistd.h>
#include <stdio.h>
#include <ctype.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <string.h>
#include <stdlib.h>
#include <netdb.h>
#include <getopt.h>
#include <fcntl.h>

#include "gfclient.h"

#define MAX_HDR_BUFFER 280
#define BUFFER_SIZE 4096

typedef struct gfcrequest_t
{
    char *server;
    char *path;
    unsigned short port;
    void (*headerfunc)(void *, size_t, void *);
    void *headerarg;
    void (*writefunc)(void*, size_t, void *);
    void *writearg;
    size_t bytesReceived;
    size_t fileLen;
    gfstatus_t status;
    int sockfd;
    int gfcReqHdrValid;
    int gfcRcvdCnt;
} gfcrequest_t;

void gfc_cleanup(gfcrequest_t *gfr){
    gfr->headerarg = NULL;
    gfr->headerfunc = NULL;
    gfr->writearg = NULL;
    gfr->writefunc = NULL;
    if (gfr != NULL)
    {
        free(gfr);
        gfr = NULL;
    }
}

gfcrequest_t *gfc_create(){
    gfcrequest_t *gfr = (gfcrequest_t *) malloc(sizeof(gfcrequest_t));

    gfr->sockfd = 0;
    gfr->bytesReceived = 0;
    gfr->fileLen = 0;
    gfr->gfcReqHdrValid = 0;
    gfr->gfcRcvdCnt = 0;

    return gfr;
}

size_t gfc_get_bytesreceived(gfcrequest_t *gfr){
    return gfr->bytesReceived;
}

size_t gfc_get_filelen(gfcrequest_t *gfr){
    return gfr->fileLen;
}

gfstatus_t gfc_get_status(gfcrequest_t *gfr){
    return gfr->status;
}

void gfc_global_init(){
}

void gfc_global_cleanup(){

}

int gfc_trans_hdr_data(gfcrequest_t *gfr)
{
    // Variables
    ssize_t bytes_sent = 0;
    ssize_t size_sent = 0;
    size_t length = 0;
    char gfcReqHdr[MAX_HDR_BUFFER];

    memset(gfcReqHdr, '\0', sizeof(char)* MAX_HDR_BUFFER);
    strcpy(gfcReqHdr, "GETFILE GET ");
    strcat(gfcReqHdr, gfr->path);
    strcat(gfcReqHdr, "\r\n\r\n");

    fprintf(stdout, "[INFO]: GFC sending request header %s ...\n", gfcReqHdr);
    fflush(stdout);

    length = strlen(gfcReqHdr);
    while (length > 0)
    {
        if ((bytes_sent = send(gfr->sockfd, gfcReqHdr, strlen(gfcReqHdr), 0)) < 0)
        {
            return -1;
        }
        length -= bytes_sent;
        size_sent += bytes_sent;
    }

    return 0;
}

int gfc_service_create(gfcrequest_t *gfr){

    if (gfc_trans_hdr_data(gfr) < 0)
    {
        fprintf(stderr, "[ERROR]: GFC failed to send request header.\n");
        return -1;
    }

    return 0;
}

int gfc_create_socket_conn(gfcrequest_t *gfr){
    // Variables
    struct hostent *gfc_serverinfo;
    struct sockaddr_in gfc_serveraddr;
    unsigned long serveraddr_nbo;

    int func_ret_status = 1;

    /* Create SOcket Connection */
    gfr->sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (gfr->sockfd < 0)
    {
        fprintf(stderr, "[ERROR]: Opening socket connection.\n");
        gfr->status = GF_ERROR;
        func_ret_status = -1;
    }
    else
    {
        int opt_reuse_val = 1;
        if (setsockopt(gfr->sockfd, SOL_SOCKET, SO_REUSEADDR, &opt_reuse_val, sizeof(int)) < 0)
        {
            fprintf(stderr, "[ERROR]: GFC failed to set reuse socket option.");
        }

        /* Get information about given host */
        gfc_serverinfo = gethostbyname(gfr->server);
        serveraddr_nbo = *(unsigned long *) (gfc_serverinfo->h_addr_list[0]);

        /* Clear Server Address */
        bzero((char *) &gfc_serveraddr, sizeof(gfc_serveraddr));
        /* Build IP Socket Address Scheme */
        gfc_serveraddr.sin_family = AF_INET;
        gfc_serveraddr.sin_port = htons(gfr->port);
        gfc_serveraddr.sin_addr.s_addr = serveraddr_nbo;

        /* Create connection to the server */
        if (connect(gfr->sockfd, (struct sockaddr *) &gfc_serveraddr, sizeof(gfc_serveraddr)) < 0)
        {
            fprintf(stderr, "[ERROR]: Creating connection to the server.\n");
            gfr->status = GF_ERROR;
            func_ret_status = -1;
        }
        else
        {
            func_ret_status = 1;
        }

    }

    gfc_serverinfo = NULL;
    
    return func_ret_status;
}

int gfc_process_hdr_data(gfcrequest_t *gfr, char *scheme, char *status)
{
    char *gfc_ret_status[4] = {"OK", "FILE_NOT_FOUND", "ERROR", "INVALID"};

    char *(*ptr)[4] = &gfc_ret_status;
    int index = -1;
    
    if (strcmp(scheme, "GETFILE") != 0)
    {
        gfr->status = GF_INVALID;
        gfr->bytesReceived = 0;
        return -1;
    }

    for (int i=0; i < 4; i++)
    {
        if (strstr(status, (*ptr)[i]) != NULL)
        {
            index = i;
        }
    }

    if (index != -1)
    {
        switch(index)
        {
            case 0:
                gfr->status = GF_OK;
                break;
            case 1:
                gfr->status = GF_FILE_NOT_FOUND;
                break;
            case 2:
                gfr->status = GF_ERROR;
                break;
            case 3:
                gfr->status = GF_INVALID;
                break;
        }

        return 0;
    }
    else
    {
        return -1;
    }
}


int gfc_recv_data(gfcrequest_t *gfr){

    // Header vriables
    char rcvbuf[MAX_HDR_BUFFER];
    ssize_t hdr_bytes_rcvd = 0;
    ssize_t hdr_bytes_cnt = 0;
    size_t max_hdr_size = MAX_HDR_BUFFER;
    char hdrStr[MAX_HDR_BUFFER];

    // Content varaibles
    ssize_t bytes_rcvd = 0;
    int len_hdr_rcvd_pos = 0;
    char rcvd_str[BUFFER_SIZE];
    gfr->gfcRcvdCnt = 0;
    gfr->gfcReqHdrValid = -1;
    int func_ret_status = -1;

    memset(hdrStr, '\0', sizeof(char)* MAX_HDR_BUFFER);
    memset(rcvbuf, '\0', sizeof(char)* MAX_HDR_BUFFER);
    
    /* Receive data from server in chunk */
    for ( ; ; )
    {
        if (gfr->gfcReqHdrValid < 0)
        {
            hdr_bytes_rcvd = recv(gfr->sockfd, rcvbuf, max_hdr_size, 0);
            if (hdr_bytes_rcvd > 0)
            {
                if (hdr_bytes_cnt + hdr_bytes_rcvd <= max_hdr_size)
                {
                    memcpy(hdrStr + hdr_bytes_cnt, rcvbuf, hdr_bytes_rcvd);
                    hdr_bytes_cnt += hdr_bytes_rcvd;
                }

                char *scheme = strtok(hdrStr, " ");
                char *status = strtok(NULL, " ");
                char *length = strtok(NULL, " \r\n\r\n");

                if (scheme != NULL)
                {
                    if (status != NULL && length != NULL)
                    {
                        len_hdr_rcvd_pos = strlen(scheme) + strlen(status) + strlen(length) + 6;
                    }
                    else
                    {
                        len_hdr_rcvd_pos = 0;
                    }

                    if (gfc_process_hdr_data(gfr, scheme, status) == 0)
                    {
                        if (length != NULL)
                            gfr->fileLen = atol(length);
                        if (gfr->status == GF_OK)
                        {
                            if (hdr_bytes_cnt > len_hdr_rcvd_pos)
                            {
                                size_t offset_buf_hdr = hdr_bytes_cnt - len_hdr_rcvd_pos;
                                char *content_pos = hdrStr + len_hdr_rcvd_pos;
                                gfr->writefunc(content_pos, offset_buf_hdr, gfr->writearg);
                                gfr->bytesReceived += offset_buf_hdr;
                            }
                        }
                        gfr->gfcReqHdrValid = 0;
                    }
                    else
                    {
                        gfr->status = GF_INVALID;
                        func_ret_status = -1;
                        goto final;
                    }
                } // if scheme, status != NULL
                else if (hdr_bytes_cnt <= max_hdr_size)
                {
                    continue;
                }
                else
                {
                    gfr->status = GF_INVALID;
                    func_ret_status = -1;
                    goto final;
                }
            } // if hdr_byte_rcdv > 0
            else if (hdr_bytes_rcvd == 0)
            {
                fprintf(stdout, "[ERROR]: GFC Failed to receive header data from socket.(closed)\n");
                fflush(stdout);
                gfr->status = GF_INVALID;
                func_ret_status = -1;
                goto final;
            }
            else if (hdr_bytes_rcvd == -1)
            {
                fprintf(stderr, "[ERROR]: GFC Failed to receive header data from socket.\n");
                gfr->status = GF_INVALID;
                func_ret_status = -1;
                goto final;
            }
        } // if gfcReqHdrValid
        else
        {
            while (gfr->bytesReceived < gfr->fileLen)
            {
                bytes_rcvd = read(gfr->sockfd, rcvd_str, BUFFER_SIZE);
                if (bytes_rcvd > 0)
                {
                    gfr->bytesReceived += bytes_rcvd;
                    gfr->writefunc(rcvd_str, bytes_rcvd, gfr->writearg);
                } // if
                else if (bytes_rcvd == 0)
                {
                    fprintf(stderr, "[ERROR]: Failed to receive all content data from socket (closed).\n");
                    func_ret_status = -1;
                    goto final;
                }
                else if (bytes_rcvd < 0)
                {
                    fprintf(stderr, "[ERROR]: Failed to receive content data from socket.\n");
                    func_ret_status = -1;
                    goto final;
                }
            } // while loop

            func_ret_status = 0;
            goto final;

        } // else gfcReqHdrValid

    } // main for loop - forever

    final:
        return func_ret_status;
}

int gfc_perform(gfcrequest_t *gfr){

    int gf_return_status = -1;

    /* Create Socket Connection with AF_NET and of type SOCK_STREAM, which
     * corresponds to TCP connection */
    if (gfc_create_socket_conn(gfr) > 0)
    {
        /* Send request header */
        if (gfc_service_create(gfr) == 0)
            gf_return_status = gfc_recv_data(gfr); /* Receive response */
    }

    close(gfr->sockfd);

    return gf_return_status;
}

void gfc_set_headerarg(gfcrequest_t *gfr, void *headerarg){
    gfr->headerarg = headerarg;
}

void gfc_set_headerfunc(gfcrequest_t *gfr, void (*headerfunc)(void*, size_t, void *)){
    gfr->headerfunc = headerfunc;
}

void gfc_set_path(gfcrequest_t *gfr, char* path){
    gfr->path = path;
}

void gfc_set_port(gfcrequest_t *gfr, unsigned short port){
    gfr->port = port;
}

void gfc_set_server(gfcrequest_t *gfr, char* server){
    gfr->server = server;
}

void gfc_set_writearg(gfcrequest_t *gfr, void *writearg){
    gfr->writearg = writearg;
}

void gfc_set_writefunc(gfcrequest_t *gfr, void (*writefunc)(void*, size_t, void *)){
    gfr->writefunc = writefunc;
}

char* gfc_strstatus(gfstatus_t status){

    char * gf_status;

    switch(status){
        case GF_OK: 
            gf_status = "OK";
            break;
        case GF_FILE_NOT_FOUND: 
            gf_status = "FILE_NOT_FOUND" ;
            break;
        case GF_ERROR: 
            gf_status = "ERROR";
            break;
        case GF_INVALID: 
            gf_status = "INVALID";
            break;
    }
    return gf_status;
}
