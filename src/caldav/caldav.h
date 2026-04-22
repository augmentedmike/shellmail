#pragma once
#include <stddef.h>

typedef struct {
    char uid[256];
    char summary[512];
    char location[256];
    char description[4096];
    char dtstart[32];
    char dtend[32];
    int  all_day;
    char date[11];   // "YYYY-MM-DD" start date
    int  hour;       // start hour (0-23), -1 if all_day
    int  minute;     // start minute (0-59)
} CalEvent;

typedef struct {
    CalEvent *events;
    size_t    count;
    size_t    cap;
} CalEventList;

typedef struct {
    char server[256];
    char port[6];
    char path[512];
    char username[256];
    char password[128];
} CalDavConn;

// Parse "https://server[:port]/path" into conn, with optional credential overrides.
void caldav_init(CalDavConn *conn, const char *url,
                 const char *user, const char *pass);

// Fetch events for the given calendar month. Returns 0 on success.
// On failure, returns the HTTP status code (or -1 for connection error).
// Performs CalDAV autodiscovery if the initial REPORT fails.
int  caldav_fetch_month(CalDavConn *conn, int year, int month,
                        CalEventList *out);

void cal_event_list_init(CalEventList *l);
void cal_event_list_free(CalEventList *l);
