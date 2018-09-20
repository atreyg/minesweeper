#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netdb.h>

int setup_client_connection(char *host_arg, char *port_arg);
int login();
char *read_line(char *buffer);

#define MAXDATASIZE 100 /* max number of bytes we can get at once */

#define MAX_READ_LENGTH 20

#define ARRAY_SIZE 30

int main(int argc, char *argv[])
{
    if (argc != 3)
    {
        fprintf(stderr, "usage: client_hostname port_number\n");
        exit(1);
    }

    int sockfd = setup_client_connection(argv[1], argv[2]);

    int success = login(sockfd);
    if (!success)
    {
        printf("You entered either an incorrect username or password. Disconnecting.");
        close(sockfd);
        return 0;
    }

    printf("Authenticated.\n");

    while (1)
    {
    }

    close(sockfd);
    return 0;
}

int setup_client_connection(char *host_arg, char *port_arg)
{
    int sockfd, i = 0;
    struct hostent *he;
    struct sockaddr_in their_addr; /* connector's address information */
    int port_no = atoi(port_arg);

    if ((he = gethostbyname(host_arg)) == NULL)
    { /* get the host info */
        herror("gethostbyname");
        exit(1);
    }

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket");
        exit(1);
    }

    their_addr.sin_family = AF_INET;      /* host byte order */
    their_addr.sin_port = htons(port_no); /* short, network byte order */
    their_addr.sin_addr = *((struct in_addr *)he->h_addr);
    bzero(&(their_addr.sin_zero), 8); /* zero the rest of the struct */

    if (connect(sockfd, (struct sockaddr *)&their_addr,
                sizeof(struct sockaddr)) == -1)
    {
        perror("connect");
        exit(1);
    }

    return sockfd;
}

int login(int sockfd)
{

    char title[] = "Welcome to the online Minesweeper gaming system";
    int title_len = strlen(title);
    char border[title_len];

    for (int i = 0; i < title_len; i++)
    {
        border[i] = '=';
    }

    char desc[] = "You are required to log on with your registered user name and password.";
    printf("%s\n%s\n%s\n\n%s\n\n", border, title, border, desc);

    char usr[MAX_READ_LENGTH];
    printf("Username: ");
    read_line(usr);

    char pwd[MAX_READ_LENGTH];
    printf("Password: ");
    read_line(pwd);

    if (send(sockfd, usr, sizeof(usr), 0) == -1)
    {
        perror("Could not send username.");
    }
    if (send(sockfd, pwd, sizeof(pwd), 0) == -1)
    {
        perror("Could not send password.");
    }

    int val;
    if (recv(sockfd, &val, sizeof(val), 0) == -1)
    {
        perror("Couldn't receive authentication.");
    }
    return val;
}

char *read_line(char *buffer)
{
    fgets(buffer, MAX_READ_LENGTH, stdin);
    if (strlen(buffer) >= MAX_READ_LENGTH - 1)
    {
        int c;
        while ((c = getchar()) != '\n' && c != EOF)
            ;
    }
}