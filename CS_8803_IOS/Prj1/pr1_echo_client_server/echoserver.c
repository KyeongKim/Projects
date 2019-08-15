/* GT-ID : KKIM651 */
/* echoserver.c    */

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

#if 0
/* Internet style socket address */
struct sockaddr_in  {
  unsigned short int sin_family; /* Address family */
  unsigned short int sin_port;   /* Port number */
  struct in_addr sin_addr;	 /* IP address */
  unsigned char sin_zero[...];   /* Pad to size of 'struct sockaddr' */
};

/*
 * Structs exported from netinet/in.h (for easy reference)
 */

/* Internet address */
struct in_addr {
  unsigned int s_addr;
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

#define BUFSIZE 2000

#define USAGE                                                                 \
"usage:\n"                                                                    \
"  echoserver [options]\n"                                                    \
"options:\n"                                                                  \
"  -p                  Port (Default: 8140)\n"                                \
"  -m                  Maximum pending connections (default: 8)\n"            \
"  -h                  Show this help message\n"                              \

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
        {"port",          required_argument,      NULL,           'p'},
        {"maxnpending",   required_argument,      NULL,           'm'},
        {"help",          no_argument,            NULL,           'h'},
        {NULL,            0,                      NULL,             0}
};

/* Perror wrapper - error */
void error(char *errMsg);

int main(int argc, char **argv) {
    int option_char;
    int portno = 8140; /* port to listen on */
    int maxnpending = 8;
    /* socket connection related */
    int sockfd;
    int ret;
    int optval;
    socklen_t client_addr_len;
    int conn_socket_fd;
    int num_bytes_read;
    int num_bytes_send;
    char *client_addr_str;
    struct sockaddr_in serveraddr;
    struct sockaddr_in clientaddr;
    struct hostent *client;
    char buffer[BUFSIZE];

    // Parse and set command line arguments
    while ((option_char = getopt_long(argc, argv, "p:m:h", gLongOptions, NULL)) != -1) {
        switch (option_char) {
            default:
                fprintf(stderr, "%s", USAGE);
                exit(1);
            case 'p': // listen-port
                portno = atoi(optarg);
                break;
            case 'm': // server
                maxnpending = atoi(optarg);
                break;
            case 'h': // help
                fprintf(stdout, "%s", USAGE);
                exit(0);
                break;
        }
    }

    if (maxnpending < 1) {
        fprintf(stderr, "%s @ %d: invalid pending count (%d)\n", __FILE__, __LINE__, maxnpending);
        exit(1);
    }

    if ((portno < 1025) || (portno > 65535)) {
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
	exit(1);
    }

    /* For mostly debugging purposes to get around "Address already in use" error */
    optval = 1;
    setsockopt(sockfd, SOL_SOCKET,
               SO_REUSEADDR, (const void *) &optval , sizeof(int));

    /* Clear Server Address */
    bzero((char *) &serveraddr, sizeof(serveraddr));

    /* Build IP Socket Address Scheme */
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serveraddr.sin_port = htons(portno);

    /* Prepare connection in sockaddr */
    ret = bind(sockfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr));
    if (ret < 0)
    {
        fprintf(stderr, "[ERROR]: Binding address/port on sockaddr.\n");
	exit(1);
    }

    /* Start listening for connections */
    if (listen(sockfd, maxnpending) < 0)
    {
        fprintf(stderr, "[ERROR]: Server failed on Listening connections.\n");
	exit(1);
    }

    /* Main loop: wait for connection, send response back and close the connection */
    client_addr_len = sizeof(clientaddr);
    for ( ; ; )
    {
        /* Wait for connection from any device */
        conn_socket_fd = accept(sockfd, (struct sockaddr *) &clientaddr,
                                &client_addr_len);
        if (conn_socket_fd < 0)
        {
            fprintf(stderr, "{ERROR]: Server failed to accept connection request.\n");
            break;
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
            fprintf(stderr, "[ERROR]: Server failed to convert client address to dotted decimal notation.\n");
        }

        /* Read request from Client */
        bzero(buffer, BUFSIZE);
        num_bytes_read = read(conn_socket_fd, buffer, BUFSIZE);
        if (num_bytes_read < 0)
        {
            fprintf(stderr, "[ERROR]: Server failed to read bytes from socket.\n");
        }

        /* Sending response back to the client device */
        num_bytes_send = write(conn_socket_fd, buffer, strlen(buffer));
        if (num_bytes_send < 0)
        {
            fprintf(stderr, "[ERROR]: Server failed to write bytes back to socket.\n");
        }
        close(conn_socket_fd);
    }
    close(sockfd);

    return EXIT_SUCCESS;
}
