/* GT-ID : KKIM651 */
/* echoclient.c    */

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

/* Be prepared accept a response of this length */
#define BUFSIZE 2000

#define USAGE                                                                      \
    "usage:\n"                                                                     \
    "  echoclient [options]\n"                                                     \
    "options:\n"                                                                   \
    "  -s                  Server (Default: localhost)\n"                          \
    "  -p                  Port (Default: 8140)\n"                                 \
    "  -m                  Message to send to server (Default: \"hello world.\"\n" \
    "  -h                  Show this help message\n"

/* OPTIONS DESCRIPTOR ====================================================== */
static struct option gLongOptions[] = {
        {"server", required_argument, NULL, 's'},
        {"port", required_argument, NULL, 'p'},
        {"message", required_argument, NULL, 'm'},
        {"help", no_argument, NULL, 'h'},
        {NULL, 0, NULL, 0}};

/* Main ========================================================= */
int main(int argc, char **argv)
{
    int option_char = 0;
    char *hostname = "localhost";
    unsigned short portno = 8140;
    char *message = "hello world.";
    /* socket connection related */
    int sockfd;
    int ret;
    struct sockaddr_in serveraddr;
    struct hostent *server;
    char buffer[BUFSIZE];

    // Parse and set command line arguments
    while ((option_char = getopt_long(argc, argv, "s:p:m:h", gLongOptions, NULL)) != -1)
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
            case 'm': // server
                message = optarg;
                break;
            case 'h': // help
                fprintf(stdout, "%s", USAGE);
                exit(0);
                break;
        }
    }

    if (NULL == message)
    {
        fprintf(stderr, "%s @ %d: invalid message\n", __FILE__, __LINE__);
        exit(1);
    }

    if ((portno < 1025) || (portno > 65535))
    {
        fprintf(stderr, "%s @ %d: invalid port number (%d)\n", __FILE__, __LINE__, portno);
        exit(1);
    }

    if (NULL == hostname)
    {
        fprintf(stderr, "%s @ %d: invalid host name\n", __FILE__, __LINE__);
        exit(1);
    }

    /* Socket Code Here */

    /* Create Socket Connection */
    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        fprintf(stderr, "[ERROR]: Opening socket connection.\n");
        close(sockfd);
	exit(1);
    }

    /* Get information about given host */
    server = gethostbyname(hostname);
    if (server == NULL)
    {
        fprintf(stderr, "[ERROR]: Getting DNS info. (gethostbyname).\n");
        close(sockfd);
	exit(1);
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
	exit(1);
    }

    /* Send message to the server */
    //ret = write(sockfd, message, strlen(message)+1);
    ret = send(sockfd, message, strlen(message), 0);
    if (ret < 0)
    {
        fprintf(stderr,"[ERROR]: Writing message to sockfd.\n");
	close(sockfd);
	exit(1);
    }

    /* Read response from on sockfd */
    bzero(buffer, BUFSIZE);
    ret = read(sockfd, buffer, BUFSIZE);
    if (ret < 0)
    {
        fprintf(stderr, "[ERROR]: Reading response from server on sockfd.\n");
	close(sockfd);
	exit(1);
    }

    printf("%s", buffer);

    close(sockfd);

    return EXIT_SUCCESS;
}
