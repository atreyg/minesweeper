#define _GNU_SOURCE
#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
#include <pthread.h>
#include <semaphore.h>

#include "common_constants.h"
#include "highscore_logic.c"
#include "server.h"

#include "minesweeper_logic.h"

void add_request(int new_fd,
            pthread_mutex_t* p_mutex,
            pthread_cond_t*  p_cond_var);
struct request* get_request(pthread_mutex_t* p_mutex);
void handle_request(struct request* a_request, int thread_id);
void* handle_requests_loop(void* data);


/* number of threads used to service requests */
#define NUM_HANDLER_THREADS 3

/* global mutex for our program. assignment initializes it. */
/* note that we use a RECURSIVE mutex, since a handler      */
/* thread might try to lock it twice consecutively.         */
pthread_mutex_t request_mutex = PTHREAD_RECURSIVE_MUTEX_INITIALIZER_NP;

/* global condition variable for our program. assignment initializes it. */
pthread_cond_t  got_request = PTHREAD_COND_INITIALIZER;

pthread_mutex_t r_mutex, w_mutex;

sem_t mutex;

//used for synchronisation with highscores
int rdcount = 0;

int num_requests = 0;   /* number of pending requests, initially none */

Score *best = NULL;

logins *head = NULL;

/* format of a single request. */
struct request {
    int new_fd;
    struct request* next;   /* pointer to next request, NULL if none. */
};

struct request* requests = NULL;     /* head of linked list of requests. */
struct request* last_request = NULL; /* pointer to last request.         */


int main(int argc, char *argv[]) {
    if (argc != 2) {
        fprintf(stderr, "usage: server port_number\n");
        exit(1);
    }

    int        i;                                /* loop counter          */
    int        thr_id[NUM_HANDLER_THREADS];      /* thread IDs            */
    pthread_t  p_threads[NUM_HANDLER_THREADS];   /* thread's structures   */

    /* create the request-handling threads */
    for (i = 0; i < NUM_HANDLER_THREADS; i++) {
        thr_id[i] = i;
        pthread_create(&p_threads[i], NULL, handle_requests_loop, (void*)&thr_id[i]);
    }

    pthread_mutex_init(&r_mutex, NULL);
    pthread_mutex_init(&w_mutex, NULL);

    struct sockaddr_in their_addr; /* connector's address information */
    socklen_t sin_size;
    int sockfd = setup_server_connection(argv[1]);


    setup_login_information(&head);


    int new_fd;
    while (1) { /* main accept() loop */
        sin_size = sizeof(struct sockaddr_in);
        if ((new_fd = accept(sockfd, (struct sockaddr *)&their_addr,
                             &sin_size)) == -1) {
            perror("accept");
            continue;
        }

        add_request(new_fd, &request_mutex, &got_request);
    }
}

/*
 * function add_request(): add a request to the requests list
 * algorithm: creates a request structure, adds to the list, and
 *            increases number of pending requests by one.
 * input:     request number, linked list mutex.
 * output:    none.
 */
void add_request(int new_fd,
            pthread_mutex_t* p_mutex,
            pthread_cond_t*  p_cond_var)
{
    int rc;                         /* return code of pthreads functions.  */
    struct request* a_request;      /* pointer to newly added request.     */

    /* create structure with new request */
    a_request = (struct request*)malloc(sizeof(struct request));
    if (!a_request) { /* malloc failed?? */
        fprintf(stderr, "add_request: out of memory\n");
        exit(1);
    }
    a_request->new_fd = new_fd;
    a_request->next = NULL;

    /* lock the mutex, to assure exclusive access to the list */
    rc = pthread_mutex_lock(p_mutex);

    /* add new request to the end of the list, updating list */
    /* pointers as required */
    if (num_requests == 0) { /* special case - list is empty */
        requests = a_request;
        last_request = a_request;
    }
    else {
        last_request->next = a_request;
        last_request = a_request;
    }

    /* increase total number of pending requests by one. */
    num_requests++;

    /* unlock mutex */
    rc = pthread_mutex_unlock(p_mutex);

    /* signal the condition variable - there's a new request to handle */
    rc = pthread_cond_signal(p_cond_var);
}


/*
 * function get_request(): gets the first pending request from the requests list
 *                         removing it from the list.
 * algorithm: creates a request structure, adds to the list, and
 *            increases number of pending requests by one.
 * input:     request number, linked list mutex.
 * output:    pointer to the removed request, or NULL if none.
 * memory:    the returned request need to be freed by the caller.
 */
struct request* get_request(pthread_mutex_t* p_mutex)
{
    int rc;                         /* return code of pthreads functions.  */
    struct request* a_request;      /* pointer to request.                 */

    /* lock the mutex, to assure exclusive access to the list */
    rc = pthread_mutex_lock(p_mutex);

    if (num_requests > 0) {
        a_request = requests;
        requests = a_request->next;
        if (requests == NULL) { /* this was the last request on the list */
            last_request = NULL;
        }
        /* decrease the total number of pending requests */
        num_requests--;
    }
    else { /* requests list is empty */
        a_request = NULL;
    }

    /* unlock mutex */
    rc = pthread_mutex_unlock(p_mutex);

    /* return the request to the caller. */
    return a_request;
}


void handle_request(struct request* a_request, int thread_id) {
    int new_fd = a_request->new_fd;
    printf("created child for new game\n");

    logins *current_login = authenticate_access(new_fd, head);

    if (current_login != NULL) {
        while (1) {
            int selection;
            if (recv(new_fd, &selection, sizeof(selection), 0) == -1) {
                perror("Couldn't receive client selection.");
            }

            if (selection == 1) {
                long int start, end;
                time(&start);
                int game_result = play_minesweeper(new_fd);
                time(&end);
                current_login->games_played++;

                if (game_result == GAME_WON) {
                    current_login->games_won++;

                    Score *score = malloc(sizeof(Score));
                    score->user = current_login;
                    score->duration = end - start;

                    //need a mutex here
                    pthread_mutex_lock(&w_mutex);
                    insert_score(&best, score);
                    pthread_mutex_unlock(&w_mutex);
                }
            } else if (selection == 2) {
                pthread_mutex_lock(&r_mutex);
                rdcount++;
                if (rdcount == 1) {
                    pthread_mutex_lock(&w_mutex);
                }

                pthread_mutex_unlock(&r_mutex);
                
                send_highscore_data(best, new_fd);

                pthread_mutex_lock(&r_mutex);
                rdcount--;

                if (rdcount == 0) {
                    pthread_mutex_unlock(&w_mutex);
                }
                pthread_mutex_unlock(&r_mutex);


            } else if (selection == 3) {
                break;
            }
        }
    }

    // Quitting game
    printf("Closing connection and exiting child process\n");
    // close(new_fd);
    // exit(0);

    // close(new_fd); /* parent doesn't need this */
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
void* handle_requests_loop(void* data)
{
    int rc;                         /* return code of pthreads functions.  */
    struct request* a_request;      /* pointer to a request.               */
    int thread_id = *((int*)data);  /* thread identifying number           */


    /* lock the mutex, to access the requests list exclusively. */
    rc = pthread_mutex_lock(&request_mutex);

    /* do forever.... */
    while (1) {

        if (num_requests > 0) { /* a request is pending */
            a_request = get_request(&request_mutex);
            if (a_request) { /* got a request - handle it and free it */
                /* unlock mutex - so other threads would be able to handle */
                /* other reqeusts waiting in the queue paralelly.          */
                rc = pthread_mutex_unlock(&request_mutex);
                handle_request(a_request, thread_id);
                free(a_request);
                /* and lock the mutex again. */
                rc = pthread_mutex_lock(&request_mutex);
            }
        }
        else {
            /* wait for a request to arrive. note the mutex will be */
            /* unlocked here, thus allowing other threads access to */
            /* requests list.                                       */

            rc = pthread_cond_wait(&got_request, &request_mutex);
            /* and after we return from pthread_cond_wait, the mutex  */
            /* is locked again, so we don't need to lock it ourselves */

        }
    }
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
        send(new_fd, &(node->user->games_played),
             sizeof(node->user->games_played), 0);
        send(new_fd, &(node->user->games_won), sizeof(node->user->games_won),
             0);

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
             new->user->games_won < node->user->games_won)) {
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

int play_minesweeper(int new_fd) {
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

        send(new_fd, &response, sizeof(response), 0);
        send(new_fd, game, sizeof(GameState), 0);

        if (response == GAME_WON) {
            free(game);
            return GAME_WON;
        }
    } while (!game->gameOver);
    free(game);
    return 1;
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