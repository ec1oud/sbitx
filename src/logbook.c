#include <time.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <fcntl.h>
#include <math.h>
#include <complex.h>
#include <fftw3.h>
#include <unistd.h>
#include <wiringPi.h>
#include <wiringSerial.h>
#include <linux/limits.h>
#include <linux/types.h>
#include <stdint.h>
#include <pthread.h>
#include <stdbool.h>
#include <sys/types.h>
#include <stdint.h>
#include <errno.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <ctype.h>
#include <arpa/inet.h>
#include "sdr.h"
#include "sdr_ui.h"
#include "logbook.h"
#include "configure.h"

#include <sqlite3.h>

static int rc;
static sqlite3 *db=NULL;

void logbook_open();
int logbook_fill(int from_id, int count, const char *query);

int logbook_has_power_swr_xota() {
	static int ret = -1;
	if (ret < 0) {
		sqlite3_stmt *stmt;
		if (db == NULL)
			logbook_open();
		// https://stackoverflow.com/a/30348775
		// it might be called sqlite_schema in newer versions
		sqlite3_prepare_v2(db, "select sql from sqlite_master where name='logbook';", -1, &stmt, NULL);
		assert(sqlite3_step(stmt) == SQLITE_ROW);
		assert(sqlite3_column_count(stmt));
		assert(sqlite3_column_type(stmt, 0) == SQLITE3_TEXT);
		const char *sql = sqlite3_column_text(stmt, 0); // a CREATE TABLE command
		ret = (strstr(sql, "tx_power") && strstr(sql, "vswr") && strstr(sql, "xota"));
		sqlite3_finalize(stmt);
	}
	return ret;
}

/* writes the output to /tmp/sbitx_result_rows.txt
	if the from_id is negative, it returns the later 50 records (higher id)
	if the from_id is positive, it returns the prior 50 records (lower id) */

int logbook_query(char *query, int from_id, char *result_file){
	sqlite3_stmt *stmt;
	char statement[200], param[2000];

	if (db == NULL)
		logbook_open();

	//add to the bottom of the logbook
	if (from_id > 0){
		if (query)
			sprintf(statement, "select * from logbook "
				"where (callsign_recv LIKE '%s%%' AND id < %d) ",
				query, from_id);
		else
			sprintf(statement, "select * from logbook where id < %d ", from_id);
	}
	//last 50 QSOs
	else if (from_id == 0){
		if (query)
			sprintf(statement, "select * from logbook "
				"where callsign_recv LIKE '%s%%' ", query);
		else
			strcpy(statement, "select * from logbook ");
	}
	//latest QSOs after from_id (top of the log)
	else {
		if (query)
			sprintf(statement, "select * from logbook "
				"where (callsign_recv LIKE '%s%%' AND id > %d) ",
				query, -from_id);
		else
			sprintf(statement, "select * from logbook where id > %d ", -from_id);
	}
	strcat(statement, "ORDER BY id DESC LIMIT 50;");

	//printf("[%s]\n", statement);
	sqlite3_prepare_v2(db, statement, -1, &stmt, NULL);

	const char *output_path = "/tmp/sbitx_result_rows.txt";
	strcpy(result_file, output_path);

	FILE *pf = fopen(output_path, "w");
	if (!pf)
		return -1;

	int rec = 0;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		int i;
		int num_cols = sqlite3_column_count(stmt);
		for (i = 0; i < num_cols; i++){
			switch (sqlite3_column_type(stmt, i))
			{
			case (SQLITE3_TEXT):
				strcpy(param, sqlite3_column_text(stmt, i));
				break;
			case (SQLITE_INTEGER):
				sprintf(param, "%d", sqlite3_column_int(stmt, i));
				break;
			case (SQLITE_FLOAT):
				sprintf(param, "%g", sqlite3_column_double(stmt, i));
				break;
			case (SQLITE_NULL):
				break;
			default:
				sprintf(param, "%d", sqlite3_column_type(stmt, i));
				break;
			}
			//printf("%s|", param);
			fprintf(pf, "%s|", param);
		}
		//printf("\n");
		fprintf(pf, "\n");
	}
	sqlite3_finalize(stmt);
	fclose(pf);
	return rec;
}

int logbook_count_dup(const char *callsign, int last_seconds){
	char date_str[100], time_str[100], statement[1000];
	sqlite3_stmt *stmt;

	time_t log_time = time(NULL) - last_seconds;
	struct tm *tmp = gmtime(&log_time);
	sprintf(date_str, "%04d-%02d-%02d", tmp->tm_year + 1900, tmp->tm_mon + 1, tmp->tm_mday);
	sprintf(time_str, "%02d%02d", tmp->tm_hour, tmp->tm_min);

	sprintf(statement, "select * from logbook where "
		"callsign_recv=\"%s\" AND qso_date >= \"%s\" AND qso_time >= \"%s\"",
		callsign, date_str, time_str);

	sqlite3_prepare_v2(db, statement, -1, &stmt, NULL);
	int rec = 0;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		rec++;
	}
	sqlite3_finalize(stmt);
	return rec;
}

int logbook_get_grids(void (*f)(char *,int)) {
	sqlite3_stmt *stmt;
	char *statement = "SELECT exch_recv, COUNT(*) AS n FROM logbook "
		"GROUP BY exch_recv order by exch_recv";
	int res = sqlite3_prepare_v2(db, statement, -1, &stmt, NULL);
	int cnt = 0;
	char grid[10];
	int n = 0;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		int num_cols = sqlite3_column_count(stmt);
		for (int i = 0; i < num_cols; i++){
			char const *col_name = sqlite3_column_name(stmt, i);
			if (!strcmp(col_name, "exch_recv")) {
				strcpy(grid, sqlite3_column_text(stmt, i));
			} else
			if (!strcmp(col_name, "n")) {
				n = sqlite3_column_int(stmt, i);
			}
		}
		f(grid,n);
		cnt++;
	}
	sqlite3_finalize(stmt);
	return cnt;
}
bool logbook_caller_exists(char * id) {
	sqlite3_stmt *stmt;
	char * statement = "SELECT EXISTS(SELECT 1 FROM logbook WHERE callsign_recv=?)";
	int res = sqlite3_prepare_v2(db, statement, -1, &stmt, NULL);
	if (res != SQLITE_OK) return false;
	bool exists = false;
	res = sqlite3_bind_text(stmt, 1, id, strlen(id), SQLITE_STATIC);
	if (res == SQLITE_OK) {
		res = sqlite3_step(stmt);
		int i = sqlite3_column_int(stmt, 0);
		exists = ( res == SQLITE_ROW && i != 0);
	}
	sqlite3_finalize(stmt);
	return exists;
}
bool logbook_grid_exists(char *id) {
	sqlite3_stmt *stmt;
	char * statement = "SELECT EXISTS(SELECT 1 FROM logbook WHERE exch_recv=?)";
	int res = sqlite3_prepare_v2(db, statement, -1, &stmt, NULL);
	if (res != SQLITE_OK) return false;
	bool exists = false;
	res = sqlite3_bind_text(stmt, 1, id, strlen(id), SQLITE_STATIC);
	if (res == SQLITE_OK) {
		res = sqlite3_step(stmt);
		int i = sqlite3_column_int(stmt, 0);
		exists = ( res == SQLITE_ROW && i != 0);
	}
	sqlite3_finalize(stmt);
	return exists;
}
int logbook_prev_log(const char *callsign, char *result){
	char statement[1000], param[2000];
	sqlite3_stmt *stmt;
	sprintf(statement, "select * from logbook where "
		"callsign_recv=\"%s\" ORDER BY id DESC",
		callsign);
	strcpy(result, callsign);
	strcat(result, ": ");
	int res = sqlite3_prepare_v2(db, statement, -1, &stmt, NULL);
	int rec = 0;
	while (sqlite3_step(stmt) == SQLITE_ROW) {
		int i;
		int num_cols = sqlite3_column_count(stmt);
		if (rec == 0) {
			for (i = 0; i < num_cols; i++){
				char const *col_name = sqlite3_column_name(stmt, i);
			    if (!strcmp(col_name, "id")) { continue; }
				if (!strcmp(col_name, "callsign_recv")) { continue; }
				switch (sqlite3_column_type(stmt, i))
				{
				case (SQLITE3_TEXT):
					strcpy(param, sqlite3_column_text(stmt, i));
					break;
				case (SQLITE_INTEGER):
					sprintf(param, "%d", sqlite3_column_int(stmt, i));
					break;
				case (SQLITE_FLOAT):
					sprintf(param, "%g", sqlite3_column_double(stmt, i));
					break;
				case (SQLITE_NULL):
					break;
				default:
					sprintf(param, "%d", sqlite3_column_type(stmt, i));
					break;
				}
				strcat(result, param);
				if (!strcmp(col_name, "qso_date")) strcat(result, "_");
				else strcat(result, " ");
			}
		}
		rec++;
	}
	sqlite3_finalize(stmt);
	sprintf(param, ": %d", rec);
	strcat(result, param);
	return rec;
}
int row_count_callback(void *data, int argc, char **argv, char **azColName) {
    int *count = (int*)data;
    (*count)++;
    return 0;
}
void logbook_open(){
    char *zErrMsg = 0;
	const char *db_path = STATEDIR "/sbitx.db";

	rc = sqlite3_open(db_path, &db);
	if( rc != SQLITE_OK ){
		fprintf(stderr, "Failed to open logbook. SQL error: %s\n", zErrMsg);
		sqlite3_free(zErrMsg);
		return;
	}
	char *sql = "SELECT name FROM sqlite_master WHERE type='index' AND tbl_name='logbook' AND name IN ('gridIx', 'callIx');";
    int index_count = 0;
    rc = sqlite3_exec(db, sql, row_count_callback, &index_count, &zErrMsg);
    if (rc != SQLITE_OK) {
        fprintf(stderr, "logbook index check failed: %s\n", zErrMsg);
        sqlite3_free(zErrMsg);
		return;
    }
    if (index_count == 2) {
        printf("Logbook indexes are OK.\n");
    } else {
		char *sql1 = "CREATE INDEX gridIx ON logbook (exch_recv);";
		char *sql2 = "CREATE INDEX callIx ON logbook (callsign_recv);";
		rc = sqlite3_exec(db, sql1, NULL, NULL, &zErrMsg);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "SQL-error creating gridIx: %s\n", zErrMsg);
			sqlite3_free(zErrMsg);
			return;
		}
		rc = sqlite3_exec(db, sql2, NULL, NULL, &zErrMsg);
		if (rc != SQLITE_OK) {
			fprintf(stderr, "SQL-error creating callIx: %s\n", zErrMsg);
			sqlite3_free(zErrMsg);
			return;
		}
		printf("Logbook indexes created.\n");
    }
}

void message_add(char *mode, unsigned int frequency, int outgoing, char *message){
	char date_str[10], time_str[10], freq_str[12], statement[1000], *err_msg;
	static int err_output = 1;

	/* get the frequency */
	get_field_value("r1:freq", freq_str);
	frequency = frequency + atoi(freq_str);

	/* get the time */
	time_t log_time = time(NULL);
	struct tm *tmp = gmtime(&log_time);

	int date_utc = ((tmp->tm_year + 1900)*10000)
		+ ((tmp->tm_mon+1) * 100) + (tmp->tm_mday);
	int time_utc = (tmp->tm_hour * 10000) + (tmp->tm_min * 100) + tmp->tm_sec;

	sprintf(statement,
		"INSERT INTO messages (mode, freq, qso_date, qso_time, is_outgoing, data)"
		" VALUES('%s', '%d', '%d', '%d',  '%d','%s');",
			mode, frequency, date_utc, time_utc, outgoing, message);

	if (db == NULL)
		logbook_open();

	int res = sqlite3_exec(db, statement, 0,0, &err_msg);
	if (res != 0 && err_output) {
		printf("message_add: db err %d %s\n", res, err_msg);
		if (err_msg) sqlite3_free(err_msg);
		// only complain once, if the error is "no such table"
		// (it's quite alright to delete this table to avoid constant writing to the SSD)
		if (res == 1)
			err_output = 0;
	}
}

void logbook_add(char *contact_callsign, char *rst_sent, char *exchange_sent,
		char *rst_recv, char *exchange_recv, int tx_power, int tx_vswr,
		char *xota, char *xota_loc, char *comments){
	char statement[1000], *err_msg, date_str[11], time_str[5];
	char freq[12], log_freq[12], mode[10], mycallsign[12];

	time_t log_time = time(NULL);
	struct tm *tmp = gmtime(&log_time);
	get_field_value("r1:freq", freq);
	get_field_value("r1:mode", mode);
	get_field_value("#mycallsign", mycallsign);

	sprintf(log_freq, "%d", atoi(freq)/1000);
	//~ printf("log_freq '%s' -> %d %lf -> '%s'", freq, atoi(freq)/1000, atof(freq) / 1000.0, log_freq);

	sprintf(date_str, "%04d-%02d-%02d", tmp->tm_year + 1900, tmp->tm_mon + 1, tmp->tm_mday);
	sprintf(time_str, "%02d%02d", tmp->tm_hour, tmp->tm_min);

	if (logbook_has_power_swr_xota()) {
		sprintf(statement,
			"INSERT INTO logbook (freq, mode, qso_date, qso_time, callsign_sent,"
			"rst_sent, exch_sent, callsign_recv, rst_recv, exch_recv, tx_power, vswr, xota, xota_loc, comments) "
			"VALUES('%s', '%s', '%s', '%s',  '%s','%s','%s',  '%s','%s','%s','%d.%d','%d.%d','%s','%s','%s');",
				log_freq, mode, date_str, time_str, mycallsign,
				rst_sent, exchange_sent, contact_callsign, rst_recv, exchange_recv,
				tx_power / 10, tx_power % 10, tx_vswr / 10, tx_vswr % 10, xota, xota_loc, comments);
	} else {
		sprintf(statement,
			"INSERT INTO logbook (freq, mode, qso_date, qso_time, callsign_sent,"
			"rst_sent, exch_sent, callsign_recv, rst_recv, exch_recv, comments) "
			"VALUES('%s', '%s', '%s', '%s',  '%s','%s','%s',  '%s','%s','%s','%s');",
				log_freq, mode, date_str, time_str, mycallsign,
				rst_sent, exchange_sent, contact_callsign, rst_recv, exchange_recv, comments);
	}

	if (db == NULL)
		logbook_open();

	int res = sqlite3_exec(db, statement, 0,0, &err_msg);
	if (res != 0) {
		printf("logbook_add db: %d err=%s", res, err_msg);
		if (err_msg) sqlite3_free(err_msg);
	}
}

/*
void import_logs(char *filename){
	char entry_text[1000], statement[1000];
	char freq[10], mode[10], date_str[10], time_str[10], mycall[10], rst_sent[10],
	exchange_sent[10], contact_callsign[12], rst_recv[10], exchange_recv[10];

	FILE *pf = fopen(filename, "r");
	while(fgets(entry_text, sizeof(entry_text), pf)){
		char *p = strtok(entry_text, "\t ");
		strcpy(freq, p);
		strcpy(mode, strtok(NULL, "\t "));
		strcpy(date_str, strtok(NULL, "\t "));
		strcpy(time_str, strtok(NULL, "\t "));
		strcpy(mycall, strtok(NULL, "\t "));
		strcpy(rst_sent, strtok(NULL, "\t "));
		strcpy(exchange_sent, strtok(NULL, "\t "));
		strcpy(contact_callsign, strtok(NULL, "\t "));
		strcpy(rst_recv, strtok(NULL, "\t "));
		strcpy(exchange_recv, strtok(NULL, "\t\n"));
		sprintf(statement,
		"INSERT INTO logbook (freq, mode, qso_date, qso_time, callsign_sent,"
		"rst_sent, exch_sent, callsign_recv, rst_recv, exch_recv) "
		"VALUES('%s', '%s', '%s', '%s',  '%s','%s','%s',  '%s','%s','%s');",
			freq, mode, date_str, time_str,
			 mycall, rst_sent, exchange_sent,
			contact_callsign, rst_recv, exchange_recv);

		puts(statement);
	}
	fclose(pf);
}
*/

// ADIF field headers, see note above
// MY_SIG_INFO for POTA? use MY_POTA_REF for now
// MY_SOTA_REF for SOTA
// IOTA for IOTA
const static char *adif_names[]={"ID","MODE","FREQ","QSO_DATE","TIME_ON","OPERATOR","RST_SENT","STX_String","CALL","RST_RCVD","SRX_String","STX","COMMENTS","TX_PWR","MY_SIG","MY_SOTA_REF"};

struct band_name {
	char *name;
	int from, to;
} bands[] = {
	{"160M", 1800, 2000},
	{"80M", 3500, 4000},
	{"60M", 5000, 5500},
	{"40M", 7000, 7300},
	{"30M", 10000, 10150},
	{"20M", 14000, 14350},
	{"17M", 18000, 18200},
	{"15M", 21000, 21450},
	{"12M", 24800, 25000},
	{"10M", 28000, 29700},
};

static void strip_chr(char *str, const char to_remove){
    int i, j, len;

    len = strlen(str);
    for(i=0; i<len; i++) {
        if(str[i] == to_remove) {
            for(j=i; j<len; j++)
                str[j] = str[j+1];
            len--;
            i--;
        }
    }
}

void *prepare_query_by_date(const char *start_date, const char *end_date) {
	char statement[250];
	sqlite3_stmt *ret = NULL;
	if (logbook_has_power_swr_xota()) {
		if (start_date && start_date[0]) {
			snprintf(statement, sizeof(statement),
					"select id,mode,freq,qso_date,qso_time,callsign_sent,rst_sent,exch_sent,callsign_recv,rst_recv,exch_recv,tx_id,comments,tx_power,xota,xota_loc "
					" from logbook where (qso_date >= '%s' AND qso_date <= '%s') ORDER BY id DESC;",
					start_date, end_date);
		} else {
			strncpy(statement,
					"select id,mode,freq,qso_date,qso_time,callsign_sent,rst_sent,exch_sent,callsign_recv,rst_recv,exch_recv,tx_id,comments,tx_power,xota,xota_loc "
					" from logbook ORDER BY id DESC;", sizeof(statement));
		}
	} else {
		snprintf(statement, sizeof(statement),
				"select id,mode,freq,qso_date,qso_time,callsign_sent,rst_sent,exch_sent,callsign_recv,rst_recv,exch_recv,tx_id,comments "
				" from logbook where (qso_date >= '%s' AND qso_date <= '%s') ORDER BY id DESC;",
				start_date, end_date);
	}

	int r = sqlite3_prepare_v2(db, statement, -1, &ret, NULL);
	if (r != SQLITE_OK) {
		printf("problem with query: %s\n", statement);
		ret = NULL;
	}
	return ret;
}

int write_adif_header(char *buf, int len, const char *source) {
	return snprintf(buf,  len, "/ADIF file\n"
		"generated from sBITX log db by %s\n"
		"<adif version:5>3.1.4\n"
		"<EOH>\n", source);
}

int write_adif_record(void *stmt, char *buf, int len) {
	char field_value[2000]; // big enough for a comment, hopefully; the rest are much smaller
	int num_cols = sqlite3_column_count(stmt);
	int rec = 0;
	int buf_offset = 0;
	char sig[5]; // IOTA/SOTA/POTA
	for (int i = 1; i < num_cols; i++) {
		switch (sqlite3_column_type(stmt, i))
		{
		case (SQLITE3_TEXT):
			strcpy(field_value, sqlite3_column_text(stmt, i));
			break;
		case (SQLITE_INTEGER):
			sprintf(field_value, "%d", sqlite3_column_int(stmt, i));
			break;
		case (SQLITE_FLOAT):
			sprintf(field_value, "%g", sqlite3_column_double(stmt, i));
			break;
		case (SQLITE_NULL):
			break;
		default:
			sprintf(field_value, "%d", sqlite3_column_type(stmt, i));
			break;
		}
		//~ printf("col %d of %d type %d: ADIF %s value '%s'\n",
			//~ i, num_cols, sqlite3_column_type(stmt, i), adif_names[i], field_value);

		const int field_len = strlen(field_value);
		bool output_done = false;
		switch (i) { // columns are in the order requested in prepare_query_by_date()
		case 1: // mode
			// If mode is FT8, set rec to 1 so we switch to use gridsquare instead of stx/srx fields - n1qm
			if (!strcmp("FT8", field_value))
				rec = 1;
			else
				rec = 0;
			break;
		case 2: { // freq
			long f = atoi(field_value);
			float ffreq=atof(field_value)/1000.0;  // convert kHz to MHz
			sprintf(field_value, "%.3f",ffreq); // write out with 3 decimal digits
			for (int j = 0 ; j < sizeof(bands)/sizeof(struct band_name); j++)
				if (bands[j].from <= f && f <= bands[j].to)
					buf_offset += snprintf(buf + buf_offset, len - buf_offset,
						"<BAND:%d>%s ", strlen(bands[j].name), bands[j].name);
		} break;
		case 3: // qso_date
			strip_chr(field_value, '-');
			break;
		case 7: // exch_sent
			if (rec == 1)
				buf_offset += snprintf(buf + buf_offset, len - buf_offset,
					"<MY_GRIDSQUARE:%d>%s ", field_len, field_value);
			else
				buf_offset += snprintf(buf + buf_offset, len - buf_offset,
					"<%s:%d>%s ", adif_names[i], field_len, field_value);
			output_done = true;
			break;
		case 10: // exch_recv
			if (rec == 1)
				buf_offset += snprintf(buf + buf_offset, len - buf_offset,
					"<GRIDSQUARE:%d>%s ", field_len, field_value);
			else
				buf_offset += snprintf(buf + buf_offset, len - buf_offset,
					"<%s:%d>%s ", adif_names[i], field_len, field_value);
			output_done = true;
			break;
		case 14: // xota
			strncpy(sig, field_value, sizeof(sig));
			break;
		case 15: // xota_loc
			if (!strcmp("POTA", sig)) {
				buf_offset += snprintf(buf + buf_offset, len - buf_offset,
					"<MY_POTA_REF:%d>%s ", field_len, field_value);
				output_done = true;
			} else if (!strcmp("IOTA", sig)) {
				buf_offset += snprintf(buf + buf_offset, len - buf_offset,
					"<IOTA:%d>%s ", field_len, field_value);
				output_done = true;
			}
			// SOTA (as default) is taken care of below: adif_names[15] = MY_SOTA_REF
			break;
		default:
			break;
		}
		if (!output_done && field_len > 0) {
			//~ printf("    default output @%d\n", buf_offset);
			buf_offset += snprintf(buf + buf_offset, len - buf_offset,
				"<%s:%d>%s ", adif_names[i], field_len, field_value);
		}
	}
	buf_offset += snprintf(buf + buf_offset, len - buf_offset, "<EOR>\n");
	return buf_offset;
}

bool logbook_next(void *stmt) {
	return sqlite3_step(stmt) == SQLITE_ROW;
}

void logbook_end_query(void *stmt) {
	sqlite3_finalize(stmt);
}

int export_adif(const char *path, const char *start_date, const char *end_date, const char *source) {
	char buf[4096];
	sqlite3_stmt *stmt = prepare_query_by_date(start_date, end_date);
	if (!stmt)
		return -1;

	//add to the bottom of the logbook
	FILE *pf = fopen(path, "w");
	fwrite(buf, 1, write_adif_header(buf, sizeof(buf), source), pf);

	while (logbook_next(stmt)) {
		fwrite(buf, 1, write_adif_record(stmt, buf, sizeof(buf)), pf);
	}
	sqlite3_finalize(stmt);
	fclose(pf);
}

int logbook_fill(int from_id, int count, const char *query){
	sqlite3_stmt *stmt;
	char statement[200], param[2000];

	if (db == NULL)
		logbook_open();

	//add to the bottom of the logbook
	if (from_id > 0){
		if (query)
			sprintf(statement, "select * from logbook "
				"where (callsign_recv LIKE '%s%%' AND id < %d) ",
				query, from_id);
		else
			sprintf(statement, "select * from logbook where id < %d ", from_id);
	}
	//last 200 QSOs
	else if (from_id == 0){
		if (query)
			sprintf(statement, "select * from logbook "
				"where callsign_recv LIKE '%s%%' ", query);
		else
			strcpy(statement, "select * from logbook ");
	}
	//latest QSOs after from_id (top of the log)
	else {
		if (query)
			sprintf(statement, "select * from logbook "
				"where (callsign_recv LIKE '%s%%' AND id > %d) ",
				query, -from_id);
		else
			sprintf(statement, "select * from logbook where id > %d ", -from_id);
	}

	char stmt_count[100];
	sprintf(stmt_count, "ORDER BY id DESC LIMIT %d;", count);
	strcat(statement, stmt_count);
	//printf("[%s]\n", statement);
	sqlite3_prepare_v2(db, statement, -1, &stmt, NULL);

	int rec = 0;
	char id[10], qso_time[20], qso_date[20], freq[20], mode[20], callsign[20],
	rst_recv[20], exchange_recv[20], rst_sent[20], exchange_sent[20], tx_pwr[10], swr[10], comments[1000];

	while (sqlite3_step(stmt) == SQLITE_ROW) {
		int i;
		int num_cols = sqlite3_column_count(stmt);
		for (i = 0; i < num_cols; i++){

			char const *col_name = sqlite3_column_name(stmt, i);
			if (!strcmp(col_name, "id"))
				strcpy(id, sqlite3_column_text(stmt, i));
			else if (!strcmp(col_name, "qso_date"))
				strcpy(qso_date, sqlite3_column_text(stmt, i));
			else if (!strcmp(col_name, "qso_time"))
				strcpy(qso_time, sqlite3_column_text(stmt, i));
			else if (!strcmp(col_name, "qso_time"))
				strcpy(qso_time, sqlite3_column_text(stmt, i));
			else if (!strcmp(col_name, "freq"))
				strcpy(freq, sqlite3_column_text(stmt, i));
			else if (!strcmp(col_name, "mode"))
				strcpy(mode, sqlite3_column_text(stmt, i));
			else if (!strcmp(col_name, "callsign_recv"))
				strcpy(callsign, sqlite3_column_text(stmt, i));
			else if (!strcmp(col_name, "rst_sent"))
				strcpy(rst_sent, sqlite3_column_text(stmt, i));
			else if (!strcmp(col_name, "rst_recv"))
				strcpy(rst_recv, sqlite3_column_text(stmt, i));
			else if (!strcmp(col_name, "exch_sent"))
				strcpy(exchange_sent, sqlite3_column_text(stmt, i));
			else if (!strcmp(col_name, "exch_recv"))
				strcpy(exchange_recv, sqlite3_column_text(stmt, i));
			else if (!strcmp(col_name, "tx_power"))
				strcpy(tx_pwr, sqlite3_column_text(stmt, i));
			else if (!strcmp(col_name, "vswr"))
				strcpy(swr, sqlite3_column_text(stmt, i));
			else if (!strcmp(col_name, "comments"))
				strcpy(comments, sqlite3_column_text(stmt, i));
		}
	}
	sqlite3_finalize(stmt);
}

void logbook_delete(int id){
	char statement[100], *err_msg;
	sprintf(statement, "DELETE FROM logbook WHERE id='%d';", id);
	sqlite3_exec(db, statement, 0,0, &err_msg);
}

