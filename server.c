#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>

#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "common_constants.h"
#include "minesweeper_logic.h"
#include "server.h"

#define RANDOM_NUMBER_SEED 42

// Allocate memory for threads
#define NUM_HANDLER_THREADS 10
int thr_id[NUM_HANDLER_THREADS];
pthread_t p_threads[NUM_HANDLER_THREADS];

// Synchronisation for client requests
pthread_mutex_t request_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
pthread_cond_t got_request = PTHREAD_COND_INITIALIZER;

// Synchronisation for scoreboard
pthread_mutex_t read_mutex, write_mutex;
int reader_count = 0;

// Head to linked lists of structs: Score, Login, and Request respectively
Score *score_head = NULL;
Login *login_head = NULL;
Request *request_head = NULL;

// Time struct for polling select function
struct timeval tv;

// Flag to start program cleanup
volatile int shutdown_active = 0;

/*
 * function main(): entry point for server
 * algorithm: checks whether sufficient command line arguments have
 *   provided to create connection. Checks login and starts game loop.
 * input:     command line arguments.
 * output:    none.
 */
int main(int argc, char *argv[]) {
    // Check if correct usage of program
    if (argc > 2) {
        fprintf(stderr, "usage: server port_number\n");
        exit(1);
    }

    // If port number is not provided use a default value
    int port_no;
    if (argc == 2) {
        port_no = atoi(argv[1]);
    } else {
        port_no = 12345;
    }

    // Seed the random number generator with set value
    srand(RANDOM_NUMBER_SEED);
    // Set handler for interrupt signal (Ctrl + C)
    signal(SIGINT, initiate_shutdown);
    // Create socket connection
    int sockfd = setup_server_connection(port_no);
    // Set up details from .txt file into linked list for login
    setup_login_information();
    // Execute threads in thread pool
    initialise_thread_pool();

    // Set timeval struct values to 0 for select to be polling continuously
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    // Loop continously while flag to shutdown hasnt been set
    while (!shutdown_active) {
        // Create file descriptor set with sockfd
        fd_set master;
        FD_ZERO(&master);
        FD_SET(sockfd, &master);

        if (select(sockfd + 1, &master, NULL, NULL, &tv) <= 0) {
            continue;
        };

        // If connection is ready to accept on sockfd
        if (FD_ISSET(sockfd, &master)) {
            // Variables to store new connection information
            int new_fd;
            struct sockaddr_in their_addr;
            socklen_t sin_size = sizeof(struct sockaddr_in);

            // Accept new connection and store details in new_fd
            if ((new_fd = accept(sockfd, (struct sockaddr *)&their_addr,
                                 &sin_size)) == -1) {
                perror("accept");
                continue;
            }

            // Add the new connection to the request_head linked list
            add_request(new_fd, &request_mutex, &got_request);
        }
    }

    // Once loop is exited (i.e. shutdown_active) clear stored data
    printf("Main thread: Clearing shared data.\n");
    clear_allocated_memory();

    // Signal all threads waiting on 'got_request' cond variable to unblock
    printf("Main thread: Unblocking all threads waiting on request.\n");
    pthread_cond_broadcast(&got_request);

    printf("Main thread: Cleared data, exiting.\n");
    pthread_exit(0);

    // Execution will not reach this point as main thread is exited
    return 0;
}

/*
 * function initiate_shutdown(): function handling interrupt signal
 * algorithm: set shutdown_active to true, allowing threads to exit gracefully.
 * input:     none.
 * output:    none.
 */
void initiate_shutdown() {
    printf("Ctrl+C pressed, initiating clean shutdown.\n");
    shutdown_active = 1;
}

/*
 * function setup_server_connection(): create listening socket to connect on
 * algorithm: create the socket, bind it to an address based on port input,
 *   and start listening on it.
 * input:     port number.
 * output:    socket file descriptor.
 */
int setup_server_connection(int port_no) {
    // Variables to store connection information
    int sockfd;
    struct sockaddr_in my_addr;

    // Generate the socket
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    // Allow port to be reused
    int yes = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEPORT, &yes, sizeof(int)) == -1) {
        perror("reuse addr");
        exit(1);
    }

    // Set IPv4 addresses as type
    my_addr.sin_family = AF_INET;
    // Set host byte order from host short to network
    my_addr.sin_port = htons(port_no);
    // Autofill with IP
    my_addr.sin_addr.s_addr = INADDR_ANY;

    // Bind socket to the connection information set up
    if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(struct sockaddr)) ==
        -1) {
        perror("bind");
        exit(1);
    }

    // Start listnening on the socket
    if (listen(sockfd, BACKLOG) == -1) {
        perror("listen");
        exit(1);
    }

    printf("Server starts listening ...\n");
    return sockfd;
}

/*
 * function setup_login_information(): read txt file into global linked list
 * algorithm: Open the Authentication.txt file for reading. Create a login node
 *   for each entry in the file, and add it to the linked list.
 * input:     pointer to login_head of login linked list.
 * output:    none.
 */
void setup_login_information() {
    // Open file for reading
    FILE *login_file = fopen("Authentication.txt", "r");
    // Exit on file read error (file not found, etc)
    if (!login_file) {
        exit(1);
    }

    // Track the previous linked list entry
    Login *prev = login_head;
    while (1) {
        // Create login node
        Login *curr_node = malloc(sizeof(Login));
        // Read username and password from file to node
        if (fscanf(login_file, "%s %s", curr_node->username,
                   curr_node->password) != 2) {
            // If it was the last entry, free node and leave loop
            free(curr_node);
            break;
        };

        // Initialise each login with 0 games played or won
        curr_node->games_played = 0;
        curr_node->games_won = 0;

        // Add node to linked list
        if (login_head == NULL) {
            login_head = curr_node;
        } else {
            prev->next = curr_node;
        }
        // Set previous to node, as next loop this will be the prev node
        prev = curr_node;
    }
    // Close file
    fclose(login_file);
    // Throwing away the first entry that contains column headers from file
    login_head = login_head->next;
}

/*
 * function initialise_thread_pool(): execute all handler threads for
 * request_head algorithm: loop through number of handler threads and execute
 * them. Initialise the read and write mutexes as well. input:     none. output:
 * none.
 */
void initialise_thread_pool() {
    // Loop through number of threads, and execute them in start routine
    for (int i = 0; i < NUM_HANDLER_THREADS; i++) {
        thr_id[i] = i;
        pthread_create(&p_threads[i], NULL, handle_requests_loop,
                       (void *)&thr_id[i]);
    }

    // Initialise mutexes
    pthread_mutex_init(&read_mutex, NULL);
    pthread_mutex_init(&write_mutex, NULL);
}

/*
 * function add_request(): add a request to the request_head linked list
 * algorithm: creates a Request, adds to the list, and increases number
 *   of pending request_head by one, and signal that there is a new request.
 * input:     request file descriptor, linked list mutex and cond variable.
 * output:    none.
 */
void add_request(int new_fd, pthread_mutex_t *p_mutex,
                 pthread_cond_t *p_cond_var) {
    // Initialise new request
    Request *a_request = malloc(sizeof(Request));
    a_request->new_fd = new_fd;
    a_request->next = NULL;

    // Lock the mutex, to assure exclusive access to the list
    pthread_mutex_lock(p_mutex);

    // Add Request to the end of the linked list
    if (request_head == NULL) {
        // Set new request as login_head if list is empty
        request_head = a_request;
    } else {
        Request *last_request = request_head;
        while (last_request->next != NULL) {
            last_request = last_request->next;
        }
        last_request->next = a_request;
    }

    // Unlock mutex
    pthread_mutex_unlock(p_mutex);

    // Signal the condition variable that a new request is available
    pthread_cond_signal(p_cond_var);
}

/*
 * function handle_request_loop(): infinite loop of request_head handling
 * algorithm: forever, if there are request_head to handle, take the first
 *            and handle it. Then wait on the given condition variable,
 *            and when it is signaled, re-do the loop.
 * input:     id of thread, for printing purposes.
 * output:    none.
 */
void *handle_requests_loop(void *data) {
    Request *a_request;
    int thread_id = *((int *)data);

    // Lock the mutex, to access the request_head list exclusively.
    pthread_mutex_lock(&request_mutex);

    // Loop forever as long as shutdown hasnt been activated
    while (!shutdown_active) {
        // Get a request from the list
        a_request = get_request();

        // If a request was pending
        if (a_request) {
            // Unlock mutex so other threads can handle other request_head
            pthread_mutex_unlock(&request_mutex);

            // Handle the request, and free once handled
            handle_request(a_request, thread_id);
            free(a_request);

            // Lock the mutex again as it will try to get a request again
            pthread_mutex_lock(&request_mutex);
        } else {
            printf("Thread %d: Waiting for request...\n", thread_id);
            // Block on the condition variable, unlocking the mutex.
            pthread_cond_wait(&got_request, &request_mutex);
            // Will unblock on signal/broadcast and lock the mutex.
        }
    }

    // Leaves loop on shutdown, unlocks mutex allowing other blocked threads to
    // continue. Exits after.
    printf("Thread %d: Exiting\n", thread_id);
    pthread_mutex_unlock(&request_mutex);
    pthread_exit(0);
}

/*
 * function get_request(): gets the first pending request from the list
 * algorithm: creates a Request pointer and points it at the login_head of the
 *   request_head list.
 * input: none.
 * output: pointer to the request, or NULL if none.
 */
Request *get_request() {
    Request *a_request;

    // Get the top value of the request_head list
    a_request = request_head;
    // If the list was not empty, progress the list down one link
    if (request_head != NULL) {
        request_head = a_request->next;
    }

    return a_request;
}

/*
 * function handle_request(): use thread to process a client connection
 * algorithm: get the file descriptor for client connection, authenticate user,
 *   and begin selection loop
 * input: pointer to Request, and thread id for logging.
 * output: none.
 */
void handle_request(Request *a_request, int thread_id) {
    // File descriptor for the request
    int new_fd = a_request->new_fd;

    // Unblock client that is waiting to be handled
    int connected = 1;
    send(new_fd, &connected, sizeof(connected), 0);
    printf("Thread %d: Handling new game.\n", thread_id);

    // Get user login information or NULL if not authenticated,
    Login *curr_login = auth_access(new_fd, thread_id, &connected);

    if (curr_login != NULL) {
        int selection;

        // Loop until shutdown or client disconnect
        while (!shutdown_active && connected) {
            if (read_helper(new_fd, &selection, sizeof(selection),
                            &connected)) {
                // Call appropriate function from client selection
                if (selection == 1) {
                    minesweeper_selection(new_fd, thread_id, &connected,
                                          curr_login);
                } else if (selection == 2) {
                    score_selection(new_fd);
                } else if (selection == 3) {
                    // Leave loop on client quit
                    break;
                }
            }
        }
    }

    // Quitting game: failed login, shutdown or client disconnect
    close(new_fd);
    printf("Thread %d: Closing client connection and returning back to pool.\n",
           thread_id);
}

/*
 * function auth_access(): authenticate the client
 * algorithm: get the username and password strings from client,
 *   and compare the values to the verified Login list
 * input: socked file descriptor, thread id for logging, and connected flag
 * output: pointer to client's Login or NULL if not authenticated.
 */
Login *auth_access(int new_fd, int thread_id, int *connected) {
    char usr[MAX_READ_LENGTH];
    char pwd[MAX_READ_LENGTH];

    // Get user input for username and password without blocking
    if (read_helper(new_fd, &usr, MAX_READ_LENGTH, connected)) {
        if (read_helper(new_fd, &pwd, MAX_READ_LENGTH, connected)) {
            // Default the authentication variables to 'unauthenticated'
            int auth_val = 0;
            Login *auth_login = NULL;

            // Loop through the linked list
            Login *curr_node = login_head;
            while (curr_node->next != NULL) {
                if (strcmp(curr_node->username, usr) == 0 &&
                    strcmp(curr_node->password, pwd) == 0) {
                    // If correct details, set variables to login values
                    auth_val = 1;
                    auth_login = curr_node;
                    break;
                }
                curr_node = curr_node->next;
            }
            // Send whether authentication was successful to client
            if (send(new_fd, &auth_val, sizeof(auth_val), 0) == -1) {
                perror("Couldn't send authorisation.");
            }

            return auth_login;
        }
    }

    // Control only reaches here if unable to read values or server shutdown
    printf("Thread %d: Left login due to shutdown or disconnect.\n", thread_id);
    return NULL;
}

/*
 * function minesweeper_selection(): process a minesweeper game selection
 * algorithm: store start time for a game, play the game, compute the play time
 *   , and if the game was won add the score to the scoreboard.
 * input: socked file descriptor, thread id for logging, connected flag, and
 *   login of current user
 * output: none.
 */
void minesweeper_selection(int new_fd, int thread_id, int *connected,
                           Login *login) {
    // Track the time before, and after the game to compute duration
    long int start, end;
    time(&start);
    int game_result = play_minesweeper(new_fd, thread_id, connected);
    time(&end);

    // Update user details about games played/won
    login->games_played++;
    if (game_result == GAME_WON) {
        login->games_won++;

        // Create a score struct with user and duration data
        Score *score = malloc(sizeof(Score));
        score->user = login;
        score->duration = end - start;

        // Send duration to client so player can view
        send(new_fd, &(score->duration), sizeof(score->duration), 0);

        // Mutexes to exclusively add a score to the list
        pthread_mutex_lock(&write_mutex);
        insert_score(score);
        pthread_mutex_unlock(&write_mutex);
    }
}

/*
 * function play_minesweeper(): communicate with client to play game
 * algorithm: Loop and get client input for game option. If game isnt quit,
 *   get coordinates and place or reveal tile. Send server response code for
 *   the processing, and send updated game state. Leave loop on game end or quit
 * input: socked file descriptor, thread id for logging, connected flag.
 * output: exit code of game (GAME_WON or GAME_LOST or -1).
 */
int play_minesweeper(int new_fd, int thread_id, int *connected) {
    // Setup intial game state
    GameState game;
    initialise_game(&game);
    send_revealed_game(&game, new_fd);

    char row, option;
    int column;
    // Loop till shutdown or disconnect
    while (!shutdown_active && connected) {
        // Reads are nested to ensure data is received in order
        if (read_helper(new_fd, &option, sizeof(option), connected)) {
            // Leave loop on quit
            if (option == 'Q') {
                break;
            }

            if (read_helper(new_fd, &row, sizeof(row), connected)) {
                if (read_helper(new_fd, &column, sizeof(column), connected)) {
                    int response;
                    if (option == 'R') {
                        response = search_tiles(&game, row - 'A', column - 1);
                    } else if (option == 'P') {
                        response = place_flag(&game, row - 'A', column - 1);
                    }

                    // Send the server response so client can display a message
                    send(new_fd, &response, sizeof(response), 0);
                    // Send game state with data only on revealed tiles
                    send_revealed_game(&game, new_fd);

                    // Return from function on game end
                    if (response == GAME_WON || response == GAME_LOST) {
                        return response;
                    }
                }
            }
        }
    }

    printf("Thread %d: Leaving mid-game due to shutdown or quit.\n", thread_id);
    return -1;
}

/*
 * function send_revealed_game(): send game state with dataless unrevealed tiles
 * algorithm: Loop through game state and if tile is revealed, send it. Send
 *   a 'dummy' tile with no mine information for unrevealed tiles.
 * input: pointer to GameState, socked file descriptor.
 * output: none.
 */
void send_revealed_game(GameState *game, int new_fd) {
    // Set up dummy tile
    Tile dummy;
    dummy.revealed = 0;

    // Loop through all tiles in gamestate
    for (int row = 0; row < NUM_TILES_Y; row++) {
        for (int column = 0; column < NUM_TILES_X; column++) {
            Tile *tile = &game->tiles[row][column];

            if (tile->revealed) {
                // Send proper tile as it has already been revealed
                send(new_fd, tile, sizeof(Tile), 0);
            } else {
                // Set flag status of dummy tile then send it
                if (tile->flagged) {
                    dummy.flagged = 1;
                } else {
                    dummy.flagged = 0;
                }
                send(new_fd, &dummy, sizeof(Tile), 0);
            }
        }
    }

    // Send number of remaining mines in the game state to the client
    send(new_fd, &game->mines_left, sizeof(game->mines_left), 0);
}

/*
 * function score_selection(): process the viewing of scoreboard
 * algorithm: use mutexes to ensure that new scores are not added while
 *   other clients are trying to read the scoreboard.
 * input: socked file descriptor.
 * output: none.
 */
void score_selection(int new_fd) {
    // Lock writing if atleast one reader is present
    pthread_mutex_lock(&read_mutex);
    reader_count++;
    if (reader_count == 1) {
        pthread_mutex_lock(&write_mutex);
    }
    pthread_mutex_unlock(&read_mutex);

    send_highscore_data(new_fd);

    // Unlock writer once no other threads are trying to read the scoreboard
    pthread_mutex_lock(&read_mutex);
    reader_count--;
    if (reader_count == 0) {
        pthread_mutex_unlock(&write_mutex);
    }
    pthread_mutex_unlock(&read_mutex);
}

/*
 * function send_highscore_data(): send scoreboard data to client.
 * algorithm: Loop through Score linked list, sending relevant data to client,
 *   send a flag to indicate whether more scores will follow after the current
 *   one.
 * input: socket file descriptor
 * output: none.
 */
void send_highscore_data(int new_fd) {
    Score *node = score_head;
    // Send whether list is empty or not to client, as different text rendered
    int response_type;
    if (node == NULL) {
        response_type = HIGHSCORES_EMPTY;
    } else {
        response_type = HIGHSCORES_PRESENT;
    }
    send(new_fd, &response_type, sizeof(response_type), 0);

    // Loop through the Score linked list
    while (node != NULL) {
        // Send username, duration, games won, and games played respectively
        // Sent from longest to shortest duration (head to tail of list) as it
        // will appear in the opposite order on the client console.
        send(new_fd, node->user->username, sizeof(node->user->username), 0);
        send(new_fd, &(node->duration), sizeof(node->duration), 0);
        send(new_fd, &(node->user->games_won), sizeof(node->user->games_won),
             0);
        send(new_fd, &(node->user->games_played),
             sizeof(node->user->games_played), 0);

        // Send flag on if entries remain
        int entries_left;
        if (node->next == NULL) {
            entries_left = HIGHSCORES_END;
        } else {
            entries_left = HIGHSCORES_PRESENT;
        }
        send(new_fd, &entries_left, sizeof(entries_left), 0);

        node = node->next;
    }
}

/*
 * function insert_score(): insert new Score struct into the linked list
 * algorithm: only accessed exclusively. Loop through current list till new
 *   Score should be placed before 'node' (found through checks). Place node
 *   after the 'prev' Score and link it to the next 'node'.
 * input: pointer to new Score to be inserted.
 * output: none.
 */
void insert_score(Score *new) {
    Score *prev = NULL;
    Score *node = score_head;
    // Loop through
    while (1) {
        // Checks based on assignment criteria. Runs when 'new' should appear
        // above 'node' on the highscore display
        if (node == NULL || new->duration > node->duration ||
            (new->duration == node->duration &&
             new->user->games_won < node->user->games_won) ||
            (new->duration == node->duration &&
             new->user->games_won ==
                 node->user->games_won &&strcmp(new->user->username,
                                                node->user->username) < 0)) {
            // If empty list, set head to the score
            if (prev == NULL) {
                score_head = new;
            } else {
                // Else set it after the previous node
                prev->next = new;
            }
            // Link new score to the next nodee and return
            new->next = node;
            return;
        }
        // Move along the list to find where new Score should be inserted
        prev = node;
        node = node->next;
    }
}

/*
 * function read_helper(): provide non-blocking recv ability
 * algorithm: Continously poll the file descriptor with select to see if there
 *   is anything available to read, with a flag on shutdown and connection.
 *   If data is available to read, read it into the provided buffer.
 * input: socked file descriptor, pointer to buffer, length of buffer,
 *   connected flag.
 * output: none.
 */
int read_helper(int fd, void *buff, size_t len, int *connected) {
    while (!shutdown_active && *connected) {
        // Reset the file descriptor set to read from fd
        fd_set init_select;
        FD_ZERO(&init_select);
        FD_SET(fd, &init_select);

        // Set init_select with whether fd has data
        if (select(fd + 1, &init_select, NULL, NULL, &tv) <= 0) {
            continue;
        };
        // If fd was set, and data is available read it in
        if (FD_ISSET(fd, &init_select)) {
            if (recv(fd, buff, len, 0) <= 0) {
                // On receive error, set flag that client is not connected
                perror("Client ended connection");
                *connected = 0;
                continue;
            }
            return 1;
        }
    }
    return 0;
}

/*
 * function clear_allocated_memory(): explicitly free all dynamic memory
 * algorithm: loop through each stored linked list, freeing nodes each iteration
 * input: none.
 * output: none.
 */
void clear_allocated_memory() {
    // Free scoreboard elements
    while (score_head != NULL) {
        Score *next = score_head->next;
        free(score_head);
        score_head = next;
    }
    // Free read in verified login details
    while (login_head != NULL) {
        Login *next = login_head->next;
        free(login_head);
        login_head = next;
    }
    // Free any requests from clients still pending
    while (request_head != NULL) {
        Request *next = request_head->next;
        close(request_head->new_fd);
        free(request_head);
        request_head = next;
    }
}