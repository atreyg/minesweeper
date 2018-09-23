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

#include "minesweeper_logic.h"

#define MAX_READ_LENGTH 20
#define BACKLOG 10

typedef struct logins
{
    char username[MAX_READ_LENGTH];
    char password[MAX_READ_LENGTH];
    struct logins *next;
} logins;

int setup_server_connection(char *port_no);
void authenticate_access(int new_fd, logins *access_list);
int check_details(logins *head, char *usr, char *pwd);
void play_minesweeper(int new_fd);

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
    logins *prev = head;
    while (1)
    {
        logins *curr_node = malloc(sizeof(logins));

        if (fscanf(login_file, "%s %s", curr_node->username, curr_node->password) != 2)
        {
            free(curr_node);
            break;
        };

        if (head == NULL)
        {
            head = curr_node;
        }
        else
        {
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
            printf("created child for new game\n");
            authenticate_access(new_fd, head);
            int selection;
            if (recv(new_fd, &selection, sizeof(selection), 0) == -1)
            {
                perror("Couldn't receive username.");
            }

            if (selection == 1)
            {
                play_minesweeper(new_fd);
            }
            else if (selection == 2)
            {
                //return highscore data
                //probably a struct like the game state that gets passed back to client to render
            }

            printf("Closing connection and exiting child process\n");
            close(new_fd);
            exit(0);
        }
        close(new_fd); /* parent doesn't need this */

        while (waitpid(-1, NULL, WNOHANG) > 0)
            ; /* clean up child processes */
    }
}

void play_minesweeper(int new_fd)
{
    GameState *game = malloc(sizeof(GameState));
    initialise_game(game);
    char row, option;
    int column;
    do
    {
        recv(new_fd, &option, sizeof(option), 0);
        printf("got option %c from client\n", option);
        if (option == 'Q')
        {
            break;
        }

        recv(new_fd, &row, sizeof(row), 0);
        recv(new_fd, &column, sizeof(column), 0);
        printf("Client send coordinates %c%d to %c\n", row, column, option);
        if (option == 'R')
        {
            search_tiles(game, row - 'A', column - 1);
        }
        else if (option == 'P')
        {
            place_flag(game, row - 'A', column - 1);
        }

        printf("sending game state to client\n");
        send(new_fd, game, sizeof(GameState), 0);

    } while (!game->gameOver);
    free(game);
}

int check_details(logins *head, char *usr, char *pwd)
{
    logins *curr_node = head;
    while (curr_node->next != NULL)
    {
        if (strcmp(curr_node->username, usr) == 0 && strcmp(curr_node->password, pwd) == 0)
        {
            return 1;
        }
        curr_node = curr_node->next;
    }
    return 0;
}

void authenticate_access(int new_fd, logins *access_list)
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

    int auth_val = check_details(access_list, usr, pwd);
    if (send(new_fd, &auth_val, sizeof(auth_val), 0) == -1)
    {
        perror("Couldn't send authorisation.");
    }
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