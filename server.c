#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <semaphore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
//#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include "common_constants.h"
#include "server.h"

#include "minesweeper_logic.h"

/* number of threads used to service requests */
#define NUM_HANDLER_THREADS 10

#define RANDOM_NUMBER_SEED 42

/* global mutex for our program. assignment initializes it. */
/* note that we use a RECURSIVE mutex, since a handler      */
/* thread might try to lock it twice consecutively.         */
pthread_mutex_t request_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

/* global condition variable for our program. assignment initializes it. */
pthread_cond_t got_request = PTHREAD_COND_INITIALIZER;

pthread_mutex_t read_mutex, write_mutex;
pthread_mutex_t sigint_mutex;
// used for synchronisation with highscores
int reader_count = 0;
int num_requests = 0; /* number of pending requests, initially none */

Score *best = NULL;

Login *head = NULL;

Request *requests = NULL;     /* head of linked list of requests. */
Request *last_request = NULL; /* pointer to last request_t.         */

struct timeval tv;

volatile int shutdown_active = 0;

int main(int argc, char *argv[]) {
    int port_no;

    if (argc > 2) {
        fprintf(stderr, "usage: server port_number\n");
        exit(1);
    }

    if (argc == 2) {
        port_no = atoi(argv[1]);
    } else {
        port_no = 12345;
    }

    srand(RANDOM_NUMBER_SEED);
    signal(SIGINT, initiate_shutdown);

    int thr_id[NUM_HANDLER_THREADS];          /* thread IDs            */
    pthread_t p_threads[NUM_HANDLER_THREADS]; /* thread's structures   */

    fd_set master;    // master file descriptor list
    fd_set read_fds;  // temp file descriptor list for select()
    tv.tv_sec = 0;
    tv.tv_usec = 0;

    /* create the request_t-handling threads */
    for (int i = 0; i < NUM_HANDLER_THREADS; i++) {
        thr_id[i] = i;
        pthread_create(&p_threads[i], NULL, handle_requests_loop,
                       (void *)&thr_id[i]);
    }

    pthread_mutex_init(&read_mutex, NULL);
    pthread_mutex_init(&write_mutex, NULL);

    struct sockaddr_in their_addr; /* connector's address information */
    socklen_t sin_size;
    int sockfd = setup_server_connection(port_no);
    setup_login_information(&head);

    FD_ZERO(&master);
    FD_SET(sockfd, &master);
    int fdmax = sockfd;

    int new_fd;
    while (!shutdown_active) { /* main accept() loop */
        read_fds = master;
        select(fdmax + 1, &read_fds, NULL, NULL, &tv);

        if (FD_ISSET(sockfd, &read_fds)) {
            sin_size = sizeof(struct sockaddr_in);
            if ((new_fd = accept(sockfd, (struct sockaddr *)&their_addr,
                                 &sin_size)) == -1) {
                perror("accept");
                continue;
            }

            add_request(new_fd, &request_mutex, &got_request);
        }
    }

    printf("Main thread: Clearing shared data.\n");
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
    printf("Main thread: Unblocking all threads waiting on request.\n");
    pthread_cond_broadcast(&got_request);

    printf("Main thread: Cleared data, exiting.\n");
    pthread_exit(0);
    return 0;
}

void initiate_shutdown() {
    printf("Ctrl+C pressed, initiating clean shutdown.\n");
    shutdown_active = 1;
}

/*
 * function add_request(): add a request_t to the requests list
 * algorithm: creates a request_t structure, adds to the list, and
 *            increases number of pending requests by one.
 * input:     request_t number, linked list mutex.
 * output:    none.
 */
void add_request(int new_fd, pthread_mutex_t *p_mutex,
                 pthread_cond_t *p_cond_var) {
    // int rc;                    /* return code of pthreads functions.  */
    Request *a_request; /* pointer to newly added request_t.     */

    /* create structure with new request_t */
    a_request = malloc(sizeof(Request));
    a_request->new_fd = new_fd;
    a_request->next = NULL;

    /* lock the mutex, to assure exclusive access to the list */
    pthread_mutex_lock(p_mutex);

    /* add new request_t to the end of the list, updating list */
    /* pointers as required */
    if (num_requests == 0) { /* special case - list is empty */
        requests = a_request;
        last_request = a_request;
    } else {
        last_request->next = a_request;
        last_request = a_request;
    }

    /* increase total number of pending requests by one. */
    num_requests++;

    /* unlock mutex */
    pthread_mutex_unlock(p_mutex);

    /* signal the condition variable - there's a new request_t to handle */
    pthread_cond_signal(p_cond_var);
}

/*
 * function get_request(): gets the first pending request_t from the requests
 * list removing it from the list. algorithm: creates a request_t structure,
 * adds to the list, and increases number of pending requests by one. input:
 * request_t number, linked list mutex. output:    pointer to the removed
 * request_t, or NULL if none. memory:    the returned request_t need to be
 * freed by the caller.
 */
Request *get_request(pthread_mutex_t *p_mutex) {
    // int rc;                    /* return code of pthreads functions.  */
    Request *a_request; /* pointer to request_t.                 */

    /* lock the mutex, to assure exclusive access to the list */
    pthread_mutex_lock(p_mutex);

    if (num_requests > 0) {
        a_request = requests;
        requests = a_request->next;
        if (requests == NULL) { /* this was the last request_t on the list */
            last_request = NULL;
        }
        /* decrease the total number of pending requests */
        num_requests--;
    } else { /* requests list is empty */
        a_request = NULL;
    }

    /* unlock mutex */
    pthread_mutex_unlock(p_mutex);

    /* return the request_t to the caller. */
    return a_request;
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

void handle_request(Request *a_request, int thread_id) {
    int new_fd = a_request->new_fd;
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
    do {
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
            }
        } else {
            break;
        }
    } while (selection != 3);

    // Quitting game
    close(new_fd);
    printf("Thread %d: Closing client connection and returning back to pool.\n",
           thread_id);
}

/*
 * function handle_requests_loop(): infinite loop of requests handling
 * algorithm: forever, if there are requests to handle, take the first
 *            and handle it. Then wait on the given condition variable,
 *            and when it is signaled, re-do the loop.
 *            increases number of pending requests by one.
 * input:     id of thread, for printing purposes.
 * output:    none.
 */
void *handle_requests_loop(void *data) {
    Request *a_request;             /* pointer to a request_t. */
    int thread_id = *((int *)data); /* thread identifying number           */

    /* lock the mutex, to access the requests list exclusively. */
    pthread_mutex_lock(&request_mutex);

    /* do forever.... */
    while (!shutdown_active) {
        a_request = get_request(&request_mutex);
        if (a_request) { /* got a request_t - handle it and free it */
            /* unlock mutex - so other threads would be able to handle */
            /* other reqeusts waiting in the queue paralelly.          */
            pthread_mutex_unlock(&request_mutex);

            handle_request(a_request, thread_id);
            free(a_request);
            /* and lock the mutex again. */
            pthread_mutex_lock(&request_mutex);

        } else {
            /* wait for a request_t to arrive. note the mutex will be */
            /* unlocked here, thus allowing other threads access to */
            /* requests list.                                       */
            printf("Thread %d: Waiting for request...\n", thread_id);
            pthread_cond_wait(&got_request, &request_mutex);
            /* and after we return from pthread_cond_wait, the mutex  */
            /* is locked again, so we don't need to lock it ourselves */
            pthread_mutex_unlock(&request_mutex);
        }
    }

    // pthread_cond_broadcast(&got_request);
    printf("Thread %d: Exiting\n", thread_id);
    pthread_exit(0);
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

void setup_login_information(Login **head_address) {
    FILE *login_file = fopen("Authentication.txt", "r");
    if (!login_file) {
        exit(1);
    }

    Login *prev = *head_address;
    while (1) {
        Login *curr_node = malloc(sizeof(Login));

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
    // Throwing away the first entry that contains column headers from file
    *head_address = (*head_address)->next;
}

int play_minesweeper(int new_fd, int thread_id) {
    GameState game;

    initialise_game(&game);
    send(new_fd, &game, sizeof(GameState), 0);

    char row, option;
    int column;
    if (read_helper(new_fd, &option, sizeof(option))) {
        if (option != 'Q') {
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

int setup_server_connection(int port_no) {
    int sockfd; /* listen on sock_fd, new connection on new_fd */
    struct sockaddr_in my_addr; /* my address information */

    /* generate the socket */
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        perror("socket");
        exit(1);
    }

    /* Allow port to be reused */
    int yes = 1;
    if (setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1) {
        perror("reuse addr");
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

    printf("server starts listening ...\n");
    return sockfd;
}