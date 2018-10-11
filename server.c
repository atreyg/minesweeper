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
#include "server.h"

#include "minesweeper_logic.h"

#define RANDOM_NUMBER_SEED 42

// Allocate memory for threads
#define NUM_HANDLER_THREADS 2
int thr_id[NUM_HANDLER_THREADS];
pthread_t p_threads[NUM_HANDLER_THREADS];

// Synchronisation for client requests
pthread_mutex_t request_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;
pthread_cond_t got_request = PTHREAD_COND_INITIALIZER;

// Synchronisation for scoreboard
pthread_mutex_t read_mutex, write_mutex;
int reader_count = 0;

// Head to linked lists of structs: Score, Login, and Request respectively
Score *best = NULL;
Login *head = NULL;
Request *requests = NULL;

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

    // Create file descriptor set with sockfd
    fd_set master;
    FD_ZERO(&master);
    FD_SET(sockfd, &master);
    fd_set temp;

    // Loop continously while flag to shutdown hasnt been set
    while (!shutdown_active) {
        // Reset temp set to master each loop
        temp = master;
        select(sockfd + 1, &temp, NULL, NULL, &tv);

        // If connection is ready to accept on sockfd
        if (FD_ISSET(sockfd, &temp)) {
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

            // Add the new connection to the requests linked list
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
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
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
 * input:     pointer to head of login linked list.
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
    Login *prev = head;
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
        if (head == NULL) {
            head = curr_node;
        } else {
            prev->next = curr_node;
        }
        // Set previous to node, as next loop this will be the prev node
        prev = curr_node;
    }
    // Close file
    fclose(login_file);
    // Throwing away the first entry that contains column headers from file
    head = head->next;
}

/*
 * function initialise_thread_pool(): execute all handler threads for requests
 * algorithm: loop through number of handler threads and execute them.
 *   Initialise the read and write mutexes as well.
 * input:     none.
 * output:    none.
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
 * function add_request(): add a request to the requests linked list
 * algorithm: creates a Request, adds to the list, and increases number
 *   of pending requests by one, and signal that there is a new request.
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
    if (requests == NULL) {
        // Set new request as head if list is empty
        requests = a_request;
    } else {
        Request *last_request = requests;
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
 * function handle_requests_loop(): infinite loop of requests handling
 * algorithm: forever, if there are requests to handle, take the first
 *            and handle it. Then wait on the given condition variable,
 *            and when it is signaled, re-do the loop.
 * input:     id of thread, for printing purposes.
 * output:    none.
 */
void *handle_requests_loop(void *data) {
    Request *a_request;
    int thread_id = *((int *)data);

    // Lock the mutex, to access the requests list exclusively.
    pthread_mutex_lock(&request_mutex);

    // Loop forever as long as shutdown hasnt been activated
    while (!shutdown_active) {
        // Get a request from the list
        a_request = get_request(&request_mutex);

        // If a request was pending
        if (a_request) {
            // Unlock mutex so other threads can handle other requests
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
 * algorithm: creates a Request pointer and points it at the head of the
 *   requests list.
 * input: linked list mutex.
 * output: pointer to the requestr, or NULL if none.
 */
Request *get_request(pthread_mutex_t *p_mutex) {
    Request *a_request;

    // Lock the mutex, to assure exclusive access to the list
    pthread_mutex_lock(p_mutex);

    // Get the top value of the requests list
    a_request = requests;
    // If the list was not empty, progress the list down one link
    if (requests != NULL) {
        requests = a_request->next;
    }

    // Unlock mutex
    pthread_mutex_unlock(p_mutex);

    return a_request;
}

void handle_request(Request *a_request, int thread_id) {
    int new_fd = a_request->new_fd;
    int connection_made = 1;
    send(new_fd, &connection_made, sizeof(connection_made), 0);
    printf("Thread %d: Handling new game.\n", thread_id);

    Login *current_login = authenticate_access(new_fd, head, thread_id);
    if (current_login == NULL) {
        printf("Thread %d: Login failed.\n", thread_id);
        return;
    } else {
        printf("Thread %d: %s authenticated.\n", thread_id,
               current_login->username);
    }

    int selection;
    while (!shutdown_active) {
        if (read_helper(new_fd, &selection, sizeof(selection))) {
            if (selection == 1) {
                long int start, end;
                time(&start);
                int game_result = play_minesweeper(new_fd, thread_id);
                time(&end);
                current_login->games_played++;

                if (game_result == GAME_WON) {
                    current_login->games_won++;

                    Score *score = malloc(sizeof(Score));
                    score->user = current_login;
                    score->duration = end - start;
                    send(new_fd, &(score->duration), sizeof(score->duration),
                         0);
                    // need a mutex here
                    pthread_mutex_lock(&write_mutex);
                    insert_score(&best, score);
                    pthread_mutex_unlock(&write_mutex);
                }
            } else if (selection == 2) {
                pthread_mutex_lock(&read_mutex);
                reader_count++;
                if (reader_count == 1) {
                    pthread_mutex_lock(&write_mutex);
                }

                pthread_mutex_unlock(&read_mutex);

                send_highscore_data(best, new_fd);

                pthread_mutex_lock(&read_mutex);
                reader_count--;

                if (reader_count == 0) {
                    pthread_mutex_unlock(&write_mutex);
                }
                pthread_mutex_unlock(&read_mutex);
            } else if (selection == 3) {
                break;
            }
        }
    }

    // Quitting game
    close(new_fd);
    printf("Thread %d: Closing client connection and returning back to pool.\n",
           thread_id);
}

int read_helper(int fd, void *buff, size_t len) {
    fd_set init_select;
    while (!shutdown_active) {
        FD_ZERO(&init_select);
        FD_SET(fd, &init_select);
        select(fd + 1, &init_select, NULL, NULL, &tv);
        if (FD_ISSET(fd, &init_select)) {
            if (recv(fd, buff, len, 0) == -1) {
                perror("Couldn't receive client selection.");
            }
            return 1;
        }
    }
    return 0;
}

void send_highscore_data(Score *head, int new_fd) {
    Score *node = head;
    int response_type;
    if (node == NULL) {
        response_type = HIGHSCORES_EMPTY;
    } else {
        response_type = HIGHSCORES_PRESENT;
    }

    send(new_fd, &response_type, sizeof(response_type), 0);

    while (node != NULL) {
        send(new_fd, node->user->username, sizeof(node->user->username), 0);
        send(new_fd, &(node->duration), sizeof(node->duration), 0);
        send(new_fd, &(node->user->games_won), sizeof(node->user->games_won),
             0);
        send(new_fd, &(node->user->games_played),
             sizeof(node->user->games_played), 0);

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

void insert_score(Score **head, Score *new) {
    Score *prev = NULL;
    Score *node = *head;
    while (1) {
        if (node == NULL || new->duration > node->duration ||
            (new->duration == node->duration &&
             new->user->games_won < node->user->games_won) ||
            (new->duration == node->duration &&
             new->user->games_won ==
                 node->user->games_won &&strcmp(new->user->username,
                                                node->user->username) < 0)) {
            if (prev == NULL) {
                *head = new;
            } else {
                prev->next = new;
            }
            new->next = node;
            return;
        }

        prev = node;
        node = node->next;
    }
}

int play_minesweeper(int new_fd, int thread_id) {
    GameState game;

    initialise_game(&game);
    send(new_fd, &game, sizeof(GameState), 0);

    char row, option;
    int column;
    while (!shutdown_active) {
        if (read_helper(new_fd, &option, sizeof(option))) {
            if (option == 'Q') {
                break;
            }

            if (read_helper(new_fd, &row, sizeof(row))) {
                if (read_helper(new_fd, &column, sizeof(column))) {
                    int response;
                    if (option == 'R') {
                        response = search_tiles(&game, row - 'A', column - 1);
                    } else if (option == 'P') {
                        response = place_flag(&game, row - 'A', column - 1);
                    }

                    send(new_fd, &response, sizeof(response), 0);
                    send(new_fd, &game, sizeof(GameState), 0);

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

Login *check_details(Login *head, char *usr, char *pwd) {
    Login *curr_node = head;
    while (curr_node->next != NULL) {
        if (strcmp(curr_node->username, usr) == 0 &&
            strcmp(curr_node->password, pwd) == 0) {
            return curr_node;
        }
        curr_node = curr_node->next;
    }
    return NULL;
}

Login *authenticate_access(int new_fd, Login *access_list, int thread_id) {
    char usr[MAX_READ_LENGTH];
    char pwd[MAX_READ_LENGTH];

    if (read_helper(new_fd, &usr, MAX_READ_LENGTH)) {
        if (read_helper(new_fd, &pwd, MAX_READ_LENGTH)) {
            Login *auth_login = check_details(access_list, usr, pwd);
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
    }

    printf("Thread %d: Left login due to shutdown.\n", thread_id);
    return NULL;
}

void clear_allocated_memory() {
    while (best != NULL) {
        Score *next = best->next;
        free(best);
        best = next;
    }
    while (head != NULL) {
        Login *next = head->next;
        free(head);
        head = next;
    }
    while (requests != NULL) {
        Request *next = requests->next;
        close(requests->new_fd);
        free(requests);
        requests = next;
    }
}