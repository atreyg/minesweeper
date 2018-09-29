#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include "common_constants.h"
#include "server.h"

#include "highscore_logic.c"
#include "minesweeper_logic.h"

int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: server port_number\n");
        exit(1);
    }

    int new_fd;
    struct sockaddr_in their_addr; /* connector's address information */
    socklen_t sin_size;
    int sockfd = setup_server_connection(argv[1]);

    logins *head = NULL;
    setup_login_information(&head);

    Score *best = NULL;

    while (1) { /* main accept() loop */
        sin_size = sizeof(struct sockaddr_in);
        if ((new_fd = accept(sockfd, (struct sockaddr *)&their_addr,
                             &sin_size)) == -1) {
            perror("accept");
            continue;
        }

        if (!fork()) {
            printf("created child for new game\n");

            logins *current_login = authenticate_access(new_fd, head);

            if (current_login != NULL) {
                int selection;
                if (recv(new_fd, &selection, sizeof(selection), 0) == -1) {
                    perror("Couldn't receive client selection.");
                }

                if (selection == 1) {
                    current_login->games_played++;
                    Score *score = malloc(sizeof(Score));
                    score->user = current_login;
                    long int start, end;

                    time(&start);
                    play_minesweeper(new_fd, current_login);
                    time(&end);

                    score->duration = end - start;
                    insert_score(&best, score);
                } else if (selection == 2) {
                    // return highscore data
                    // probably a struct like the game state that gets passed
                    // back to client to render
                }
            }

            // Quitting game
            printf("Closing connection and exiting child process\n");
            close(new_fd);
            exit(0);
        }
        close(new_fd); /* parent doesn't need this */

        /* clean up child processes */
        while (waitpid(-1, NULL, WNOHANG) > 0)
            ;
    }
}

void insert_score(Score **head, Score *new) {
    Score *node = *head;
    Score *prev;

    if (node == NULL) {
        node = new;
        return;
    }

    while (1) {
        if (node->duration > new->duration ||
            (node->duration == new->duration &&
             node->user->games_won < new->user->games_won)) {
            prev = node;
            node = node->next;
        } else {
            prev->next = new;
            new->next = node;
            return;
        }
    }
}

void setup_login_information(logins **head_address) {
    FILE *login_file = fopen("Authentication.txt", "r");
    if (!login_file) {
        exit(1);
    }

    logins *prev = *head_address;
    while (1) {
        logins *curr_node = malloc(sizeof(logins));

        if (fscanf(login_file, "%s %s", curr_node->username,
                   curr_node->password) != 2) {
            free(curr_node);
            break;
        };

        curr_node->games_played = 0;
        curr_node->games_won = 0;

        if (*head_address == NULL) {
            *head_address = curr_node;
        } else {
            prev->next = curr_node;
        }
        prev = curr_node;
    }
    fclose(login_file);
    // Throwing away the first entry that contains headers from file
    *head_address = (*head_address)->next;
}

void play_minesweeper(int new_fd, logins *current_login) {
    GameState *game = malloc(sizeof(GameState));
    initialise_game(game);
    send(new_fd, game, sizeof(GameState), 0);

    char row, option;
    int column;
    do {
        recv(new_fd, &option, sizeof(option), 0);
        if (option == 'Q') {
            break;
        }

        recv(new_fd, &row, sizeof(row), 0);
        recv(new_fd, &column, sizeof(column), 0);

        int response;
        if (option == 'R') {
            response = search_tiles(game, row - 'A', column - 1);
        } else if (option == 'P') {
            response = place_flag(game, row - 'A', column - 1);
        }

        if (response == GAME_WON) {
            current_login->games_won++;
        }

        send(new_fd, &response, sizeof(response), 0);
        send(new_fd, game, sizeof(GameState), 0);

    } while (!game->gameOver);
    free(game);
}

logins *check_details(logins *head, char *usr, char *pwd) {
    logins *curr_node = head;
    while (curr_node->next != NULL) {
        if (strcmp(curr_node->username, usr) == 0 &&
            strcmp(curr_node->password, pwd) == 0) {
            return curr_node;
        }
        curr_node = curr_node->next;
    }
    return NULL;
}

logins *authenticate_access(int new_fd, logins *access_list) {
    char usr[MAX_READ_LENGTH];
    char pwd[MAX_READ_LENGTH];
    if (recv(new_fd, usr, MAX_READ_LENGTH, 0) == -1) {
        perror("Couldn't receive username.");
    };
    if (recv(new_fd, pwd, MAX_READ_LENGTH, 0) == -1) {
        perror("Couldn't receive password.");
    };

    logins *auth_login = check_details(access_list, usr, pwd);
    int auth_val;
    if (auth_login == NULL) {
        auth_val = 0;
    } else {
        auth_val = 1;
    }
    if (send(new_fd, &auth_val, sizeof(auth_val), 0) == -1) {
        perror("Couldn't send authorisation.");
    }
    return auth_login;
}

int setup_server_connection(char *port_arg) {
    int sockfd; /* listen on sock_fd, new connection on new_fd */
    struct sockaddr_in my_addr; /* my address information */

    int port_no = atoi(port_arg);

    /* generate the socket */
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    /* generate the end point */
    my_addr.sin_family = AF_INET;         /* host byte order */
    my_addr.sin_port = htons(port_no);    /* short, network byte order */
    my_addr.sin_addr.s_addr = INADDR_ANY; /* auto-fill with my IP */

    if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) ==
        -1) {
        perror("bind");
        exit(1);
    }

    /* start listnening */
    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    printf("server starts listnening ...\n");
    return sockfd;
}