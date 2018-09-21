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


#define MAX_READ_LENGTH 20
#define BACKLOG 10

typedef struct logins
{
    char username[MAX_READ_LENGTH];
    char password[MAX_READ_LENGTH];
    struct logins *next;
} logins;

int setup_server_connection(char *port_no);
void authenticate_access(int new_fd, logins* access_list);
int check_details(logins* head, char* usr, char* pwd);

int main(int argc, char *argv[])
{
    if (argc != 2)
    {
        fprintf(stderr, "usage: server port_number\n");
        exit(1);
    }


    FILE *login_file = fopen("Authentication.txt", "r");
    if (!login_file)
    {
        exit(1);
    }

    logins *head = NULL;
    logins* prev = head;
    while(1){
        logins* curr_node = malloc(sizeof(logins));
        
        if(fscanf(login_file, "%s %s", curr_node->username, curr_node->password) != 2){
            free(curr_node);
            break;
        };
        
        if(head == NULL){
            head = curr_node;
        }else{
            prev->next = curr_node;
        }

        prev = curr_node;
    } 
    //Throwing away the first entry
    head = head->next;

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
            authenticate_access(new_fd, head);

            close(new_fd);
            exit(0);
        }
        close(new_fd); /* parent doesn't need this */

        while (waitpid(-1, NULL, WNOHANG) > 0)
            ; /* clean up child processes */
    }
}

int check_details(logins* head, char* usr, char* pwd){
    logins* curr_node = head;
    while(curr_node->next != NULL){
        if(strcmp(curr_node->username, usr) == 0 && strcmp(curr_node->password, pwd) == 0){
            return 1;
        }
        printf("Username: %s, Password: %s\n", curr_node->username, curr_node->password);
        curr_node = curr_node->next;
    }
    return 0;
}

void authenticate_access(int new_fd, logins* access_list)
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

    int auth_val = check_details(access_list, usr, pwd);
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