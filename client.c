#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>

#include "minesweeper_logic.h"

#include "common_constants.h"

#include "client.h"

// Win string:
// PA1PA2PA3PA4PA5PA6PA7PA8PA9PB1PB2PB3PB4PB5PB6PB7PB8PB9PC1PC2PC3PC4PC5PC6PC7PC8PC9PD1PD2PD3PD4PD5PD6PD7PD8PD9PE1PE2PE3PE4PE5PE6PE7PE8PE9PF1PF2PF3PF4PF5PF6PF7PF8PF9PG1PG2PG3PG4PG5PG6PG7PG8PG9PH1PH2PH3PH4PH5PH6PH7PH8PH9PI1PI2PI3PI4PI5PI6PI7PI8PI9

int main(int argc, char *argv[]) {
    if (argc != 3) {
        fprintf(stderr, "usage: client_hostname port_number\n");
        exit(1);
    }

    int sockfd = setup_client_connection(argv[1], argv[2]);

    int success = login(sockfd);
    if (!success) {
        printf(
            "You entered either an incorrect username or password. "
            "Disconnecting.\n");
        close(sockfd);
        return 0;
    }

    core_loop(sockfd);

    close(sockfd);
    return 0;
}

void core_loop(int sockfd) {
    while (1) {
        int selection = select_client_action(sockfd);

        if (selection == 1) {
            play_minesweeper(sockfd);
        } else if (selection == 2) {
            show_leaderboard(sockfd);
        } else if (selection == 3) {
            break;
        }
    }
}

void play_minesweeper(int sockfd) {
    GameState *game = malloc(sizeof(GameState));
    update_game_state(game, sockfd);
    while (1) {
        char option = select_game_action();
        send(sockfd, &option, sizeof(option), 0);

        if (option == 'Q') {
            break;
        }

        get_and_send_tile_coordinates(sockfd);

        int response;
        recv(sockfd, &response, sizeof(response), 0);

        update_game_state(game, sockfd);
        print_response_output(response);

        if (response == GAME_LOST || response == GAME_WON) {
            break;
        }
    };
    free(game);
}

void show_leaderboard(int sockfd) {
    int response;
    recv(sockfd, &response, sizeof(response), 0);

    char border[50];
    for (int i = 0; i < 14; i++) {
        border[i] = '=';
    }
    border[49] = '\0';
    printf("\n%s\n", border);
    if (response == HIGHSCORES_EMPTY) {
        printf(
            "\nThere is no information currently stored in the leaderboard. "
            "Try again later.\n");
    } else {
        while (1) {
            char username[MAX_READ_LENGTH];
            long int duration;
            int games_won;
            int games_played;

            recv(sockfd, username, sizeof(username), 0);
            recv(sockfd, &duration, sizeof(duration), 0);
            recv(sockfd, &games_won, sizeof(games_won), 0);
            recv(sockfd, &games_played, sizeof(games_played), 0);

            printf("%s \t %ld seconds \t %d games won, %d games played\n",
                   username, duration, games_won, games_played);

            int entry_left;
            recv(sockfd, &entry_left, sizeof(entry_left), 0);

            if (entry_left == HIGHSCORES_END) {
                break;
            }
        }
    }
    printf("\n%s\n", border);
}

void print_response_output(int response) {
    if (response == NORMAL) {
    } else if (response == GAME_LOST) {
        printf("You lost!\n");
    } else if (response == GAME_WON) {
        printf("You won!\n");
    } else if (response == NO_MINE_AT_FLAG) {
        printf("There was no mine at flag!\n");
    } else if (response == TILE_ALREADY_REVEALED) {
        printf("The tile was already revealed!\n");
    } else if (response == INVALID_COORDINATES) {
        printf(
            "The coordinates entered are invalid. Ensure they are "
            "within the game bounds!\n");
    }
}

void update_game_state(GameState *game, int sockfd) {
    recv(sockfd, game, sizeof(GameState), 0);
    print_game_state(game);
}

void get_and_send_tile_coordinates(int sockfd) {
    char row;
    int column;
    printf("Please input a coordinate: ");
    scanf(" %c%d", &row, &column);

    send(sockfd, &row, sizeof(row), 0);
    send(sockfd, &column, sizeof(column), 0);
}

char select_game_action() {
    char option;
    printf("Select an option:\n");
    printf("<R> Reveal tile\n");
    printf("<P> Place flag\n");
    printf("<Q> Quit Game\n");
    do {
        printf("\nOption (R,P,Q): ");
        scanf(" %c", &option);
    } while (option != 'R' && option != 'P' && option != 'Q');
    return option;
}

int select_client_action(int sockfd) {
    printf("Welcome to the Minesweeper gaming system.\n");
    printf("\nPlease enter a selection:\n");
    printf("<1> Play Minesweeper\n");
    printf("<2> Show Leaderboard\n");
    printf("<3> Quit\n");

    int selection;
    do {
        printf("\nSelection option (1-3): ");
        scanf("%d", &selection);
    } while (selection < 1 || selection > 3);

    if (send(sockfd, &selection, sizeof(selection), 0) == -1) {
        perror("Could not send selection.");
    }

    return selection;
}

int setup_client_connection(char *host_arg, char *port_arg) {
    int sockfd;
    struct hostent *he;
    struct sockaddr_in their_addr; /* connector's address information */
    int port_no = atoi(port_arg);

    if ((he = gethostbyname(host_arg)) == NULL) { /* get the host info */
        herror("gethostbyname");
        exit(1);
    }

    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    their_addr.sin_family = AF_INET;      /* host byte order */
    their_addr.sin_port = htons(port_no); /* short, network byte order */
    their_addr.sin_addr = *((struct in_addr *)he->h_addr);
    bzero(&(their_addr.sin_zero), 8); /* zero the rest of the struct */

    if (connect(sockfd, (struct sockaddr *)&their_addr,
                sizeof(struct sockaddr)) == -1) {
        perror("connect");
        exit(1);
    }

    return sockfd;
}

int login(int sockfd) {
    char title[] = "Welcome to the online Minesweeper gaming system";
    int title_len = strlen(title);
    char border[title_len];

    for (int i = 0; i < title_len; i++) {
        border[i] = '=';
    }

    char desc[] =
        "You are required to log on with your registered user name and "
        "password.";
    printf("%s\n%s\n%s\n\n%s\n\n", border, title, border, desc);

    char usr[MAX_READ_LENGTH];
    printf("Username: ");
    read_line(usr);

    char pwd[MAX_READ_LENGTH];
    printf("Password: ");
    read_line(pwd);

    if (send(sockfd, usr, sizeof(usr), 0) == -1) {
        perror("Could not send username.");
    }
    if (send(sockfd, pwd, sizeof(pwd), 0) == -1) {
        perror("Could not send password.");
    }

    int val;
    if (recv(sockfd, &val, sizeof(val), 0) == -1) {
        perror("Couldn't receive authentication.");
    }
    return val;
}

void read_line(char *buffer) {
    fgets(buffer, MAX_READ_LENGTH, stdin);
    if (strlen(buffer) >= MAX_READ_LENGTH - 1) {
        int c;
        while ((c = getchar()) != '\n' && c != EOF)
            ;
    } else {
        buffer[strlen(buffer) - 1] = '\0';
    }
}