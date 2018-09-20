#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
#include <errno.h>

int setup_server_connection(char *port_no);
void authenticate_access(int new_fd);

typedef struct logins
{
    char *username;
    char *password;
    struct logins *next;
} logins;

#define BACKLOG 10
#define MAX_READ_LENGTH 20

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: server port_number\n");
        exit(1);
    }

    //Read file
    logins *head = NULL;
    head = malloc(sizeof(logins));
    if (head == NULL)
    {
        exit(1);
    }

    FILE *login_file = fopen("Authentication.txt", "r");
    if (login_file)
    {

        fscanf(login_file, "%s", head->username);
        fscanf(login_file, "%s", head->password);
    }
    printf("%s\n", head->username);
    printf("%s\n", head->password);
    fclose(login_file);

    int sockfd = setup_server_connection(argv[1]);
    int new_fd;
    struct sockaddr_in their_addr; /* connector's address information */
    socklen_t sin_size;

    while (1)
    { /* main accept() loop */
        sin_size = sizeof(struct sockaddr_in);
        if ((new_fd = accept(sockfd, (struct sockaddr *)&their_addr,
                             &sin_size)) == -1)
        {
            perror("accept");
            continue;
        }
        // printf("server: got connection from %s\n",
        //        inet_ntoa(their_addr.sin_addr));
        if (!fork())
        { /* this is the child process */
            authenticate_access(new_fd);

            close(new_fd);
            exit(0);
        }
        close(new_fd); /* parent doesn't need this */

        while (waitpid(-1, NULL, WNOHANG) > 0)
            ; /* clean up child processes */
    }
}

void authenticate_access(int new_fd)
{
    char usr[MAX_READ_LENGTH];
    char pwd[MAX_READ_LENGTH];
    if (recv(new_fd, usr, MAX_READ_LENGTH, 0) == -1)
    {
        perror("Couldn't receive username.");
    };
    if (recv(new_fd, pwd, MAX_READ_LENGTH, 0) == -1)
    {
        perror("Couldn't receive password.");
    };

    printf("Username: %s\nPassword: %s\n", usr, pwd);

    int auth_val = 1;
    if (send(new_fd, &auth_val, sizeof(auth_val), 0) == -1)
    {
        perror("Couldn't send authorisation.");
    }

    printf("Send data\n");
    //Loop to block so connection does not close
    while (1)
    {
    };
}

int setup_server_connection(char *port_arg)
{
    int sockfd;                 /* listen on sock_fd, new connection on new_fd */
    struct sockaddr_in my_addr; /* my address information */

    int port_no = atoi(port_arg);

    /* generate the socket */
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1)
    {
        perror("socket");
        exit(1);
    }

    /* generate the end point */
    my_addr.sin_family = AF_INET;         /* host byte order */
    my_addr.sin_port = htons(port_no);    /* short, network byte order */
    my_addr.sin_addr.s_addr = INADDR_ANY; /* auto-fill with my IP */

    if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) == -1)
    {
        perror("bind");
        exit(1);
    }

    /* start listnening */
    if (listen(sockfd, BACKLOG) == -1)
    {
        perror("listen");
        exit(1);
    }

    printf("server starts listnening ...\n");
    return sockfd;
}