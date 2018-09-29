#include <time.h>

typedef struct score_entry {
    logins* user;
    time_t duration;
    struct score_entry* next;
} Score;
