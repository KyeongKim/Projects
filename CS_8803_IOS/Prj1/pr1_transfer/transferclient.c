/* GT-ID : KKIM651 */
/* transferclient.c    */

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
#include <fcntl.h>

#define BUFSIZE 2000
#define MAX_RCV_BUFF_SIZE 256

#define USAGE                                                \
    "usage:\n"                                               \
    "  transferclient [options]\n"                           \
    "options:\n"                                             \
    "  -s                  Server (Default: localhost)\n"    \
    "  -p                  Port (Default: 8140)\n"           \
    "  -o                  Output file (Default gios.txt)\n" \
    "  -h                  Show this help message\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
    {"server", required_argument, NULL, 's'},
    {"port", required_argument, NULL, 'p'},
    {"output", required_argument, NULL, 'o'},
    {"help", no_argument, NULL, 'h'},
    {NULL, 0, NULL, 0}};


int recv_file_from_server(int sock, char* filename);

/* Main ========================================================= */
int main(int argc, char **argv)
{
    int option_char = 0;
    char *hostname = "localhost";
    unsigned short portno = 8140;
    char *filename = "gios.txt";
    /* socket connection related */
    int sockfd;
    struct sockaddr_in serveraddr;
    struct hostent *server;

    // Parse and set command line arguments
    while ((option_char = getopt_long(argc, argv, "s:p:o:h", gLongOptions, NULL)) != -1)
    {
        switch (option_char)
        {
        default:
            fprintf(stderr, "%s", USAGE);
            exit(1);
        case 's': // server
            hostname = optarg;
            break;
        case 'p': // listen-port
            portno = atoi(optarg);
            break;
        case 'o': // filename
            filename = optarg;
            break;
        case 'h': // help
            fprintf(stdout, "%s", USAGE);
            exit(0);
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
        exit(EXIT_FAILURE);
    }

    if (NULL == hostname)
    {
        fprintf(stderr, "%s @ %d: invalid host name\n", __FILE__, __LINE__);
        exit(EXIT_FAILURE);
    }

    /* Socket Code Here */

    /* Create SOcket Connection */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
    	fprintf(stderr, "[ERROR]: Opening socket connection.\n");
    	close(sockfd);
    	exit(EXIT_FAILURE);
    }

    /* Get information about given host */
    server = gethostbyname(hostname);
    if (server == NULL)
    {
    	fprintf(stderr, "[ERROR]: Getting DNS info. (gethostbyname).\n");
    	close(sockfd);
    	exit(EXIT_FAILURE);
    }

    /* Clear Server Address */
    bzero((char *) &serveraddr, sizeof(serveraddr));

    /* Build IP Socket Address Scheme */
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_port = htons(portno);
    bcopy(server->h_addr, (char *) &serveraddr.sin_addr.s_addr, server->h_length);

    /* Create connection to the server */
    if (connect(sockfd, (struct sockaddr *) &serveraddr, sizeof(serveraddr)) < 0)
    {
        fprintf(stderr, "[ERROR]: Creating connection to the server.\n");
        close(sockfd);
        exit(EXIT_FAILURE);
    }
    printf("connected to: %s: %d ..\n", hostname, portno);

    recv_file_from_server(sockfd, filename);

    /* close socket */
    if (close(sockfd) < 0)
    {
        fprintf(stderr, "[ERROR]: Closing socket.\n");
        exit(EXIT_FAILURE);
    }

    return EXIT_SUCCESS;
}

int recv_file_from_server(int sockfd, char* filename)
{

    int f; /* file handler */
    ssize_t bytes_rcvd, size_rcvd;
    char rcvd_str[MAX_RCV_BUFF_SIZE];
    int cnt_rcvd; /* number of times recv() is called during transfer */

    /* Create a file to save received data from server with 0644 permission */
    if ((f = open(filename, O_WRONLY | O_CREAT | O_EXCL, S_IRUSR | S_IWUSR)) == -1)
    {
        fprintf(stderr, "[ERROR]: Creating file.\n");
        return EXIT_FAILURE;
    }

    /* Receive data from server in chunk of 256 bytes */
    while ((bytes_rcvd = recv(sockfd, rcvd_str, MAX_RCV_BUFF_SIZE, 0 )) > 0)    
    {
        cnt_rcvd++;
        size_rcvd += bytes_rcvd;

        if (write(f, rcvd_str, bytes_rcvd) < 0)
        {
            fprintf(stderr, "[ERROR]: Failed to write file.\n");
            return EXIT_FAILURE;
        }
    }

    close(f);  /* close file handler */

    return size_rcvd;
}
