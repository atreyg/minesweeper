typedef struct logins {
    char username[MAX_READ_LENGTH];
    char password[MAX_READ_LENGTH];
    int games_played;
    int games_won;
    struct logins *next;
} logins;

typedef struct score_entry {
    logins *user;
    time_t duration;
    struct score_entry *next;
} Score;

int setup_server_connection(int port_no);
logins *authenticate_access(int new_fd, logins *access_list);
logins *check_details(logins *head, char *usr, char *pwd);
int play_minesweeper(int new_fd);
void setup_login_information(logins **head);
void send_highscore_data(Score *head, int new_fd);
void insert_score(Score **score, Score *new);
void initiate_shutdown();