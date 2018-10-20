typedef struct logins_t {
    char username[MAX_READ_LENGTH];
    char password[MAX_READ_LENGTH];
    int games_played;
    int games_won;
    struct logins_t *next;
} Login;

typedef struct score_entry_t {
    Login *user;
    int duration;
    struct score_entry_t *next;
} Score;

typedef struct request_t {
    int new_fd;
    struct request_t *next;
} Request;

void initiate_shutdown();
int setup_server_connection(int port_no);
void setup_login_information();
void initialise_thread_pool();
void add_request(int new_fd, pthread_mutex_t *p_mutex,
                 pthread_cond_t *p_cond_var);
void *handle_requests_loop(void *data);
Request *get_request();
void handle_request(struct request_t *a_request, int thread_id);
Login *auth_access(int new_fd, int thread_id, int *client_connected);
void minesweeper_selection(int new_fd, int thread_id, int *connected,
                           Login *curr_login);
int play_minesweeper(int new_fd, int thread_id, int *client_connected);
void send_revealed_game(GameState *game, int new_fd);
void score_selection(int new_fd);
void send_highscore_data(int new_fd);
void insert_score(Score *new);
int read_helper(int fd, void *buff, size_t len, int *client_connected);
void send_int(int fd, int val);
void send_string(int fd, char *str);
void send_tile(int fd, Tile *tile);
void clear_allocated_memory();