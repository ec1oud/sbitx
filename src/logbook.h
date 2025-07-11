#define _XOPEN_SOURCE
#include <time.h>

void logbook_add(char *contact_callsign, char *rst_sent, char *exchange_sent,
	char *rst_recv, char *exchange_recv, int tx_power, int tx_vswr, char *comments);
int logbook_query(char *query, int from_id, char *result_file);
int logbook_count_dup(const char *callsign, int last_seconds);
int logbook_prev_log(char *out, int outlen, const char *callsign);
int logbook_last_date(struct tm *out, const char *callsign);
int logbook_get_grids(void (*f)(char *,int));
void logbook_list_open();
void logbook_open();
bool logbook_grid_exists(char *id);
bool logbook_caller_exists(char * id);
void logbook_delete(int id);
void message_add(char *mode, unsigned int frequency, int outgoing, char *message);

