typedef struct logins_t {
    char username[MAX_READ_LENGTH];
    char password[MAX_READ_LENGTH];
    int games_played;
    int games_won;
    struct logins_t *next;
} Logins;

typedef struct score_entry_t {
    Logins *user;
    time_t duration;
    struct score_entry_t *next;
} Score;

int setup_server_connection(int port_no);
Logins *authenticate_access(int new_fd, Logins *access_list);
Logins *check_details(Logins *head, char *usr, char *pwd);
int play_minesweeper(int new_fd);
void setup_login_information(Logins **head);
void send_highscore_data(Score *head, int new_fd);
void insert_score(Score **score, Score *new);
void initiate_shutdown();
void add_request(int new_fd, pthread_mutex_t *p_mutex,
                 pthread_cond_t *p_cond_var);
struct request_t *get_request(pthread_mutex_t *p_mutex);
void handle_request(struct request_t *a_request, int thread_id);
void *handle_requests_loop(void *data);
