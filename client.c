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

/*
 * function main(): entry point for client
 * algorithm: checks whether sufficient command line arguments have
 *   provided to create server connection. Checks login and starts game loop.
 * input:     command line arguments.
 * output:    none.
 */
int main(int argc, char *argv[]) {
    // Check if correct usage of program
    if (argc != 3) {
        fprintf(stderr, "usage: client_hostname port_number\n");
        exit(1);
    }

    // Create socket connection and wait for a free thread on server
    int sockfd = setup_client_connection(argv[1], argv[2]);
    wait_for_thread(sockfd);

    // Send login details to server and exit if not authenticated
    int success = login(sockfd);
    if (!success) {
        printf(
            "You entered either an incorrect username or password. "
            "Disconnecting.\n");
        close(sockfd);
        return 0;
    }

    // Start core loop of getting user input
    core_loop(sockfd);

    // Close connection and exit program once loop is left
    close(sockfd);
    return 0;
}

/*
 * function setup_client_connection(): connect client to server
 * algorithm:
 * input: host name and port number (from command line).
 * output: socket file descriptor.
 */
int setup_client_connection(char *host_arg, char *port_arg) {
    // Variables to store connection information
    int sockfd;
    struct hostent *he;
    struct sockaddr_in their_addr;
    int port_no = atoi(port_arg);

    // Get the host info
    if ((he = gethostbyname(host_arg)) == NULL) {
        herror("gethostbyname");
        exit(1);
    }

    // Create endpoint for communication and refer to it with file descriptor
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    // Set IPv4 addresses as type
    their_addr.sin_family = AF_INET;
    // Set host byte order from host short to network
    their_addr.sin_port = htons(port_no);
    their_addr.sin_addr = *((struct in_addr *)he->h_addr);
    // Zero the rest of the struct
    bzero(&(their_addr.sin_zero), 8);

    // Connect socket file descriptor to address initialised then
    if (connect(sockfd, (struct sockaddr *)&their_addr,
                sizeof(struct sockaddr)) == -1) {
        perror("connect");
        exit(1);
    }

    return sockfd;
}

/*
 * function wait_for_thread(): wait for free thread on server
 * algorithm: Block on recv call till a 'flag' is sent by server to proceed
 * input: socket file descriptor.
 * output: none.
 */
void wait_for_thread(int sockfd) {
    int connection_available;
    printf("Waiting for open connection...\n");
    // Call that blocks processing till trigger sent by server
    recv(sockfd, &connection_available, sizeof(connection_available), 0);
    printf("Received connection.\n");
}

/*
 * function login(): allow client to login with authenticated details
 * algorithm: Print login page on console, and read in user input about details.
 *   Send the details to the server for authentication and receive response.
 * input: socket file descriptor.
 * output: success of authentication.
 */
int login(int sockfd) {
    // Login page to instruct client on how to proceed
    print_login_page();

    // Get username and password from input
    char usr[MAX_READ_LENGTH];
    printf("Username: ");
    read_login_input(usr);

    char pwd[MAX_READ_LENGTH];
    printf("Password: ");
    read_login_input(pwd);

    // Send username and password to server
    if (send(sockfd, usr, sizeof(usr), 0) == -1) {
        perror("Could not send username.");
    }
    if (send(sockfd, pwd, sizeof(pwd), 0) == -1) {
        perror("Could not send password.");
    }

    // Receive authentication response from server
    int val;
    if (recv(sockfd, &val, sizeof(val), 0) == -1) {
        perror("Couldn't receive authentication.");
    }
    return val;
}

/*
 * function print_login_page(): display client facing login page
 * algorithm: Print a number of strings to stdout with login instructions.
 * input: none.
 * output: none.
 */
void print_login_page() {
    char title[] = "Welcome to the online Minesweeper gaming system";

    // Create top border that is the same length as title string
    int title_len = strlen(title);
    char border[title_len];
    for (int i = 0; i < title_len; i++) {
        border[i] = '=';
    }

    char desc[] =
        "You are required to log on with your registered user name and "
        "password.";

    // Print all strings to console
    printf("%s\n%s\n%s\n\n%s\n\n", border, title, border, desc);
}

/*
 * function read_login_input(): read input from stdin and store in buffer
 * algorithm: Read input into buffer, if input length is greater than max,
 *   clear remaining buffer, otherwise set end of string to before buffer end.
 * input: pointer to storage buffer.
 * output: none.
 */
void read_login_input(char *buffer) {
    fgets(buffer, MAX_READ_LENGTH, stdin);
    if (strlen(buffer) >= MAX_READ_LENGTH - 1) {
        clear_buffer();
    } else {
        buffer[strlen(buffer) - 1] = '\0';
    }
}

/*
 * function core_loop(): infinite loop that directs program based on client
 * algorithm: Get clients selection on program option then call the
 *   relevant function. Break out of the loop if client selects to quit.
 * input: socket file descriptor.
 * output: none.
 */
void core_loop(int sockfd) {
    while (1) {
        // User selection in the primary menu
        int selection = select_client_action(sockfd);

        if (selection == 1) {
            play_minesweeper(sockfd);
        } else if (selection == 2) {
            show_leaderboard(sockfd);
        } else if (selection == 3) {
            // Leave the infinite loop and return to main on 'Quit'
            break;
        }
    }
}

/*
 * function select_client_action(): provides client with primary program options
 * algorithm: Get clients selection on program option then call the
 *   relevant function. Break out of the loop if client selects to quit.
 * input: socket file descriptor.
 * output: selection the user makes.
 */
int select_client_action(int sockfd) {
    // Print text on console for client to interpret
    printf("-----------------------------------------\n");
    printf("Welcome to the Minesweeper gaming system.\n");
    printf("\nPlease enter a selection:\n");
    printf("<1> Play Minesweeper\n");
    printf("<2> Show Leaderboard\n");
    printf("<3> Quit\n");

    // Ask client to select one of the provided options until correct input is
    // provided.
    int selection;
    do {
        printf("\nSelection option (1-3): ");
        scanf("%d", &selection);
        // Remove remnants in input buffer to avoid incorrect processing
        clear_buffer();
    } while (selection < 1 || selection > 3);

    // Send selected option to server
    if (send(sockfd, &selection, sizeof(selection), 0) == -1) {
        perror("Could not send selection.");
    }

    return selection;
}

/*
 * function play_minesweeper(): play a game of minesweeper
 * algorithm: Create a game state object and update it based on server response.
 *   Loop forever asking for user game input, unless game is quit/won/lost.
 *   Communicate with server based on user input and get response.
 * input: socket file descriptor.
 * output: none.
 */
void play_minesweeper(int sockfd) {
    // Create initial game and set up
    GameState game;
    update_game_state(&game, sockfd);

    while (1) {
        // Get user selection for game and send to server
        char option = select_game_action();
        send(sockfd, &option, sizeof(option), 0);

        // Leave loop on quit game, returning back to main menu
        if (option == 'Q') {
            break;
        }

        // Get tile coordinates from user on any other option
        get_and_send_tile_coordinates(sockfd);

        // Get server response based on selected option and tile chosen
        int response;
        recv(sockfd, &response, sizeof(response), 0);

        // Update the game board and show any text response provided by server
        update_game_state(&game, sockfd);
        print_response_output(response, sockfd);

        // Leave loop on game end, returning back to main menu
        if (response == GAME_LOST || response == GAME_WON) {
            break;
        }
    };
}

/*
 * function update_game_state(): update the game state
 * algorithm: Receive the game state from server, and print the
 *   new state onto the console.
 * input: pointer to game and socket file descriptor.
 * output: none.
 */
void update_game_state(GameState *game, int sockfd) {
    for (int row = 0; row < NUM_TILES_Y; row++) {
        for (int column = 0; column < NUM_TILES_X; column++) {
            recv(sockfd, &game->tiles[row][column], sizeof(Tile), 0);
        }
    }
    recv(sockfd, &game->mines_left, sizeof(game->mines_left), 0);
    print_game_state(game);
}

/*
 * function select_game_action(): client selects minesweeper game option
 * algorithm: Loop until user selects on of the provided options.
 * input: none.
 * output: selected game option.
 */
char select_game_action() {
    // Print options for client
    printf("Select an option:\n");
    printf("<R> Reveal tile\n");
    printf("<P> Place flag\n");
    printf("<Q> Quit Game\n");

    // Ask client to select one of the provided options until correct input is
    // provided.
    char option;
    do {
        printf("\nOption (R,P,Q): ");
        scanf(" %c", &option);
        // Remove remnants in input buffer to avoid incorrect processing
        clear_buffer();
    } while (option != 'R' && option != 'P' && option != 'Q');
    return option;
}

/*
 * function get_and_send_tile_coordinates(): as name suggests
 * algorithm: Get coordinates from user and send to server.
 * input: socket file descriptor.
 * output: none.
 */
void get_and_send_tile_coordinates(int sockfd) {
    // Get input from client
    char row;
    int column;
    printf("Please input a coordinate: ");
    scanf(" %c%d", &row, &column);
    clear_buffer();

    // Send to server
    send(sockfd, &row, sizeof(row), 0);
    send(sockfd, &column, sizeof(column), 0);
}

/*
 * function print_response_output: print message corresponding to server
 *   response
 * algorithm: Check all possible responses and print a corresponding message.
 * input: response code and socket file descriptor.
 * output: none.
 */
void print_response_output(int response, int sockfd) {
    if (response == NORMAL) {
    } else if (response == GAME_LOST) {
        printf("You lost!\n");
    } else if (response == GAME_WON) {
        // If game is won, the time taken to win is also sent and displayed
        time_t win_time;
        recv(sockfd, &win_time, sizeof(win_time), 0);
        printf(
            "Congratulations! You have located all the mines.\n"
            "You won in %d seconds!\n",
            (int)win_time);
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

/*
 * function show_leaderboard: display border and leaderboard message
 * algorithm: Print a border on top and bottom of core leaderboard data.
 * input: socket file descriptor.
 * output: none.
 */
void show_leaderboard(int sockfd) {
    // Create a 50 character long border
    char border[50];
    for (int i = 0; i < 14; i++) {
        border[i] = '=';
    }
    border[49] = '\0';

    printf("\n%s\n", border);

    // Get response of showing leaderboard from server and print
    int response;
    recv(sockfd, &response, sizeof(response), 0);
    print_leaderboard_contents(response, sockfd);

    printf("\n%s\n", border);
}

/*
 * function print_leaderboard_contents: display actual highscore details
 * algorithm: Loop till server sends end of scores flag. Print name of user,
 *   duration, number of games won, and games played.
 * input: response and socket file descriptor.
 * output: none.
 */
void print_leaderboard_contents(int response, int sockfd) {
    // If no user has won a game yet
    if (response == HIGHSCORES_EMPTY) {
        printf(
            "\nThere is no information currently stored in the leaderboard. "
            "Try again later.\n");
    } else {
        while (1) {
            // Receive required details from server
            char username[MAX_READ_LENGTH];
            long int duration;
            int games_won;
            int games_played;

            recv(sockfd, username, sizeof(username), 0);
            recv(sockfd, &duration, sizeof(duration), 0);
            recv(sockfd, &games_won, sizeof(games_won), 0);
            recv(sockfd, &games_played, sizeof(games_played), 0);

            // Print data in provided format
            printf("%s \t %ld seconds \t %d games won, %d games played\n",
                   username, duration, games_won, games_played);

            // Receive flag on whether more scores are to follow
            int entry_left;
            recv(sockfd, &entry_left, sizeof(entry_left), 0);
            // If no entries remaining, exit loop and return to main menu
            if (entry_left == HIGHSCORES_END) {
                break;
            }
        }
    }
}

/*
 * function clear_buffer: Remove any remaining data on stdin
 * algorithm: Loop and discard each remaining character till a new line or
 *   end of file.
 * input: none.
 * output: none.
 */
void clear_buffer() {
    char c;
    while ((c = getchar()) != '\n' && c != EOF) {
    }
}