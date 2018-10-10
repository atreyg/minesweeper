typedef struct logins_t {
    char username[MAX_READ_LENGTH];
    char password[MAX_READ_LENGTH];
    int games_played;
    int games_won;
    struct logins_t *next;
} Login;

typedef struct score_entry_t {
    Login *user;
    time_t duration;
    struct score_entry_t *next;
} Score;

/* format of a single request_t. */
typedef struct request_t {
    int new_fd;
    struct request_t *next; /* pointer to next request_t, NULL if none. */
} Request;

int setup_server_connection(int port_no);
Login *authenticate_access(int new_fd, Login *access_list, int thread_id);
Login *check_details(Login *head, char *usr, char *pwd);
int play_minesweeper(int new_fd, int thread_id);
void setup_login_information(Login **head);
void send_highscore_data(Score *head, int new_fd);
void insert_score(Score **score, Score *new);
void initiate_shutdown();
void add_request(int new_fd, pthread_mutex_t *p_mutex,
                 pthread_cond_t *p_cond_var);
struct request_t *get_request(pthread_mutex_t *p_mutex);
void handle_request(struct request_t *a_request, int thread_id);
void *handle_requests_loop(void *data);
int read_helper(int fd, void *buff, size_t len);