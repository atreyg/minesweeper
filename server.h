typedef struct logins {
    char username[MAX_READ_LENGTH];
    char password[MAX_READ_LENGTH];
    int games_played;
    int games_won;
    struct logins *next;
} logins;

int setup_server_connection(char *port_no);
logins *authenticate_access(int new_fd, logins *access_list);
logins *check_details(logins *head, char *usr, char *pwd);
void play_minesweeper(int new_fd, logins *current_login);
void setup_login_information(logins **head);