/* GT-ID : KKIM651 */
/* transferserver.c    */

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
#include <arpa/inet.h>
#include <fcntl.h>

#if 0
/* 
 * Structs exported from netinet/in.h (for easy reference)
 */

/* Internet address */
struct in_addr {
  unsigned int s_addr; 
};

/* Internet style socket address */
struct sockaddr_in  {
  unsigned short int sin_family; /* Address family */
  unsigned short int sin_port;   /* Port number */
  struct in_addr sin_addr;	 /* IP address */
  unsigned char sin_zero[...];   /* Pad to size of 'struct sockaddr' */
};

/*
 * Struct exported from netdb.h
 */

/* Domain name service (DNS) host entry */
struct hostent {
  char    *h_name;        /* official name of host */
  char    **h_aliases;    /* alias list */
  int     h_addrtype;     /* host address type */
  int     h_length;       /* length of address */
  char    **h_addr_list;  /* list of addresses */
}
#endif

#define BUFSIZE 4000
#define MAX_SEND_BUFF_SIZE 256

#define USAGE                                                \
    "usage:\n"                                               \
    "  transferserver [options]\n"                           \
    "options:\n"                                             \
    "  -f                  Filename (Default: cs8803.txt)\n" \
    "  -h                  Show this help message\n"         \
    "  -p                  Port (Default: 8140)\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"filename", required_argument, NULL, 'f'},
    {"help", no_argument, NULL, 'h'},
    {"port", required_argument, NULL, 'p'},
    {NULL, 0, NULL, 0}};

int transfer_file(int sockfd, char *filename);

int main(int argc, char **argv)
{
    int option_char;
    int portno = 8140;             /* port to listen on */
    char *filename = "cs8803.txt"; /* file to transfer */
    /* socket connection related */
    int sockfd;
    int maxnpending = 7;
    int ret;
    int optval;
    struct sockaddr_in serveraddr;
    struct sockaddr_in clientaddr;
    int conn_socket_fd;
    char *client_addr_str;
    socklen_t client_addr_len;
    struct hostent *client;

    // Parse and set command line arguments
    while ((option_char = getopt_long(argc, argv, "p:hf:", gLongOptions, NULL)) != -1)
    {
        switch (option_char)
        {
        default:
            fprintf(stderr, "%s", USAGE);
            exit(1);
        case 'p': // listen-port
            portno = atoi(optarg);
            break;
        case 'h': // help
            fprintf(stdout, "%s", USAGE);
            exit(0);
            break;
        case 'f': // listen-port
            filename = optarg;
            break;
        }
    }

    if (NULL == filename)
    {
        fprintf(stderr, "%s @ %d: invalid filename\n", __FILE__, __LINE__);
        exit(1);
    }

    if ((portno < 1025) || (portno > 65535))
    {
        fprintf(stderr, "%s @ %d: invalid port number (%d)\n", __FILE__, __LINE__, portno);
        exit(1);
    }

    /* Socket Code Here */

    /* Create Socket Connection with AF_NET and of type SOCK_STREAM, which
     * corresponds to TCP connection */
    sockfd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (sockfd < 0)
    {
        fprintf(stderr, "[ERROR]: Server opening socket connection.\n");
        exit(EXIT_FAILURE);
    }

    /* For mostly debugging purposes to get around "Address already in use" error */
    optval = 1;
    setsockopt(sockfd, SOL_SOCKET,
               SO_REUSEADDR, (const void *) &optval , sizeof(int));

    /* Clear server and client address structure */
    memset(&serveraddr, 0, sizeof(serveraddr));
    memset(&clientaddr, 0, sizeof(clientaddr));

    /* Build IP Socket Address Scheme */
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(portno);

    /* Prepare connection in sockaddr */
    ret = bind(sockfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr));
    if (ret < 0)
    {
        fprintf(stderr, "[ERROR]: Binding address/port on sockaddr.\n");
        exit(EXIT_FAILURE);
    }

    /* Start listening for connections */
    if (listen(sockfd, maxnpending) < 0)
    {
        fprintf(stderr, "[ERROR]: Server failed on Listening connections.\n");
        exit(EXIT_FAILURE);
    }
    fprintf(stdout, "Server listening on port: %d ...\n", ntohs(serveraddr.sin_port));
    fflush(stdout);
    
    /* Main loop */
    for ( ; ; )
    {
        client_addr_len = sizeof(clientaddr);
        /* Wait for connection from any device */
        /* blocks until that happens           */
        printf("Waiting for client connection...\n");
        conn_socket_fd = accept(sockfd, (struct sockaddr *) &clientaddr,
                                &client_addr_len);
        if (conn_socket_fd < 0)
        {
            fprintf(stderr, "{ERROR]: Server failed to accept connection request.\n");
            break; /* exit from the infinite for loop */
        }
        
        /* Determine client device */
        client = gethostbyaddr((const char *) &clientaddr.sin_addr.s_addr,
                               sizeof(clientaddr.sin_addr.s_addr), AF_INET);
        if (client == NULL)
        {
            fprintf(stderr, "[ERROR]: Server failed to determine client device info.\n");
        }

        client_addr_str = inet_ntoa(clientaddr.sin_addr);
        if (client_addr_str == NULL)
        {
            fprintf(stderr, "[ERROR]: Server failed to convert client address (inet_ntoa).\n");
        }
        fprintf(stdout, "[INFO]: Client connected: %s: %d\n", 
            client_addr_str, ntohs(clientaddr.sin_port));
        fflush(stdout);

        transfer_file(conn_socket_fd, filename);

        fprintf(stdout, "[INFO]: Closing client connection.\n");
        fflush(stdout);

        close(conn_socket_fd);
    }

    close(sockfd);

    return EXIT_SUCCESS;
}

int transfer_file(int sockfd, char *filename)
{

    int f; /* file handler */
    ssize_t bytes_read, bytes_sent, size_sent;
    char send_str[MAX_SEND_BUFF_SIZE];
    int cnt_sent; /* number of times send() is called to transfer bytes */

    memset(send_str, '0', sizeof(send_str));
    
    /* Attempt to open file to send */
    if ((f = open(filename, O_RDONLY)) < 0)
    {
        /* Unable to open the file */
        fprintf(stderr, "[ERROR]: Unable to open file for reading...\n");
        return EXIT_FAILURE;
    }
    else
    {
        fprintf(stdout, "[INFO]: Server sending %s ...\n", filename);
        fflush(stdout);
        while ((bytes_read = read(f, send_str, MAX_SEND_BUFF_SIZE)) > 0)
        {
            if ((bytes_sent = send(sockfd, send_str, bytes_read, 0)) < bytes_read)
            {
                fprintf(stderr, "[ERROR]: Server failed to send.\n");
                return EXIT_FAILURE;
            }
            cnt_sent ++;
            size_sent += bytes_sent;
        }
        close(f);
    }
    fprintf(stdout, "[INFO]: Server successfully sent.");
    fflush(stdout);

    return size_sent;
}
