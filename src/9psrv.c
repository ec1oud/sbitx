/* libixp and example Copyright ©2007 Ron Minnich <rminnich at gmail dot com>
 * Copyright ©2007-2010 Kris Maglione <maglione.k@gmail.com>
 * sbitx 9p implementation Copyright ©2025 Shawn Rutledge <s@ecloud.org>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.

 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

#include <assert.h>
#include <ctype.h>
#include <err.h>
#include <errno.h>
#include <ifaddrs.h>
#include <netdb.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/param.h>
#include <time.h>
#include <unistd.h>

#include <ixp_local.h>

#include "modem_ft8.h"
#include "sdr_ui.h"

/* Macros */
// TODO output timestamps
#define fatal(...) ixp_eprint("fatal: " __VA_ARGS__)
#define debug(...) if(debuglevel) fprintf(stderr, __VA_ARGS__)
#define QID(t, i) ((int64_t)(t))
#ifndef TRUE
#define TRUE 1
#define FALSE 0
#endif
#define MAX_OPEN_FDS 256
#define MAX_CLIENTS 256
#define MAX_EVENTS 64
#define MAX_FILE_SIZE 1024 // max content size of typical field files (but console data can be bigger)
#define MAX_PATH_SUFFIX_SIZE 64
#define FIRST_CLIENT_ID 0xa44a000000000000

/* Forward declarations */
typedef struct Devfile Devfile;
typedef struct FidAux FidAux;
typedef struct ClientEvents ClientEvents;
static Devfile *find_by_field_id(const char *read_name);
static void stat_event(Ixp9Req *r, IxpStat *s, const Devfile *df, int data_index);
static int read_event(Ixp9Req *r, const Devfile *df, char *out, int len, int offset);
static int size_read(Ixp9Req *r, const Devfile *df);
static int read_field(Ixp9Req *r, const Devfile *df, char *out, int len, int offset);
static void write_field(const Devfile *df, const char *val, int len, int offset);
//~ static void stat_field_meta(Ixp9Req *r, IxpStat *s, const Devfile *df, int data_index);
static int read_field_meta(Ixp9Req *r, const Devfile *df, char *out, int len, int offset);
static void stat_raw(Ixp9Req *r, IxpStat *s, const Devfile *df, int data_index);
static int read_raw(Ixp9Req *r, const Devfile *df, char *out, int len, int offset);
static void stat_text(Ixp9Req *r, IxpStat *s, const Devfile *df, int data_index);
static int read_text(Ixp9Req *r, const Devfile *df, char *out, int len, int offset);
static void stat_text_spans(Ixp9Req *r, IxpStat *s, const Devfile *df, int data_index);
static int read_text_spans(Ixp9Req *r, const Devfile *df, char *out, int len, int offset);
static void update_console_mtimes_and_sizes(time_t mtime);

/* Datatypes */
struct Devfile {
	uint64_t id; // aka qid.path
	char	*name;
	int parent;
	sbitx_style semantic_filter;
	void	(*dostat)(Ixp9Req *r, IxpStat *s, const Devfile *df, int data_index);
	int	(*doread)(Ixp9Req *r, const Devfile *df, char*, int, int);
	const char	*read_name;
	void	(*dowrite)(const Devfile *df, const char*, int, int);
	const char	*write_name;
	mode_t	mode;
	time_t atime;
	time_t mtime;
	uint32_t version;
};

struct FidAux {
	int fd;
	int offset;
	int data_index; // for the console: console_last_line() at the time the spans were opened (clients: please open spans first!)
	void *srvaux; // client_id from the attach call
	Devfile *file;
	// TODO mode_t omode;
};

struct ClientEvents {
	void *srvaux; // client_id from the attach call
	Devfile *changed[MAX_EVENTS]; // a list of files that have some change to report
	int count; // how many entries in `changed`
	int byte_len; // length in bytes of all Devfile->names listed in `changed`
};

/* Error Messages */
static char
	Enoperm[] = "permission denied",
	Enofile[] = "file not found",
	Ebadvalue[] = "bad value",
	Ebadfid[] = "bad FID";

/* Devfile QIDs */
typedef enum {
	QID_ROOT = 0,
	QID_EVENT = 1,
	
	QID_SETTINGS = 2,
	QID_SETTINGS_CALL,
	QID_SETTINGS_GRID,
	
	QID_TEXT = 0x10, // whole console
	QID_BATTERY,
	QID_BATT_VOLTAGE,
	QID_S_METER,
	QID_SPECTRUM,
	QID_SPECTRUM_META,
	QID_SPECTRUM_SPAN,
	QID_SPECTRUM_SPAN_META,
	QID_SPECTRUM_SPAN_CHOICES,
	QID_SPECTRUM_WIDTH,
	QID_SPECTRUM_DEPTH,
	
	QID_MODES = 0x100,
	QID_MODES_SSB = 0x101,
	QID_MODES_FT8 = 0x102,
	QID_SSB_CHANNEL1 = 0x1000,
	QID_FT8_CHANNEL1 = 0x2000,
} DevfileID;

typedef enum {
	QID_CH_FREQ = 1,
	QID_CH_FREQ_META,
	QID_CH_FREQ_LABEL,
	QID_CH_FREQ_FMT,
	QID_CH_FREQ_MIN,
	QID_CH_FREQ_MAX,
	QID_CH_FREQ_STEP,
	QID_CH_IF_GAIN,
	QID_CH_IF_GAIN_META,
	QID_CH_IF_GAIN_LABEL,
	QID_CH_IF_GAIN_FMT,
	QID_CH_IF_GAIN_MIN,
	QID_CH_IF_GAIN_MAX,
	QID_CH_IF_GAIN_STEP,
	QID_CH_RECEIVED,
	QID_CH_RECEIVED_META,
	QID_CH_RECEIVED_SPANS,
	QID_CH_SENT,
	QID_CH_SEND,
	QID_MASK = 0xFF
} ChannelDevfileID;

/* Global Vars */
static IxpServer server;
static pid_t pid = 0;
static char *user;
static int debuglevel = 1;
static time_t start_time;
static char *argv0;
static uint64_t next_client_id = FIRST_CLIENT_ID;

/* Table of all files to be served up */
#define SEM_NONE STYLE_LOG
static Devfile devfiles[] = {
	{ QID_ROOT, "/", -1, SEM_NONE,
		nil, nil, nil, nil, nil, P9_DMDIR|DMEXCL|0777, 0, 0, 0 },
	{ QID_EVENT, "event", QID_ROOT, SEM_NONE,
		stat_event, read_event, nil, nil, nil, DMEXCL|0444, 0, 0, 0 },
	{ QID_SETTINGS, "settings", QID_ROOT, SEM_NONE,
		nil, nil, nil, nil, nil, P9_DMDIR|DMEXCL|0777, 0, 0, 0 },
	{ QID_SETTINGS_CALL, "callsign", QID_SETTINGS, SEM_NONE,
		nil, read_field, "#mycallsign", write_field, "#mycallsign", DMEXCL|0666, 0, 0, 0 },
	{ QID_SETTINGS_GRID, "grid", QID_SETTINGS, SEM_NONE,
		nil, read_field, "#mygrid", write_field, "#mygrid", DMEXCL|0666, 0, 0, 0 },
		
	{ QID_TEXT, "text", QID_ROOT, SEM_NONE,
		stat_text, read_text, "#console", nil, "", DMEXCL|0666, 0, 0, 0 },
		
	{ QID_BATTERY, "battery", QID_ROOT, SEM_NONE,
		nil, nil, nil, nil, nil, P9_DMDIR|DMEXCL|0555, 0, 0, 0 },
	{ QID_BATT_VOLTAGE, "voltage", QID_BATTERY, SEM_NONE,
		nil, read_field, "#batt", nil, nil, DMEXCL|0444, 0, 0, 0 },

	{ QID_S_METER, "s", QID_ROOT, SEM_NONE,
		nil, read_field, "#smeter", nil, nil, DMEXCL|0444, 0, 0, 0 },

	{ QID_SPECTRUM, "spectrum", QID_ROOT, SEM_NONE,
		stat_raw, read_raw, "", nil, "", DMEXCL|0666, 0, 0, 0 },
	{ QID_SPECTRUM_META, "spectrum.meta", QID_ROOT, SEM_NONE,
		nil, nil, nil, nil, nil, P9_DMDIR|DMEXCL|0777, 0, 0, 0 },
	{ QID_SPECTRUM_SPAN, "span", QID_SPECTRUM_META, SEM_NONE,
		nil, read_field, "#span", write_field, "#span", DMEXCL|0666, 0, 0, 0 },
	{ QID_SPECTRUM_SPAN_META, "span.meta", QID_SPECTRUM_META, SEM_NONE,
		nil, nil, nil, nil, nil, P9_DMDIR|DMEXCL|0777, 0, 0, 0 },
	{ QID_SPECTRUM_SPAN_CHOICES, "choices", QID_SPECTRUM_SPAN_META, SEM_NONE,
		nil, read_field_meta, "#span", nil, nil, DMEXCL|0666, 0, 0, 0 },
	// TODO waterfall metadata
	// TODO audio, power, swr

	{ QID_MODES, "modes", QID_ROOT, SEM_NONE,
		nil, nil, nil, nil, nil, P9_DMDIR|DMEXCL|0777, 0, 0, 0 },
	//~ { QID_MODES_SSB, "ssb", QID_MODES,
		//~ nil, nil, nil, nil, nil, P9_DMDIR|DMEXCL|0777, 0, 0, 0 },
	//~ { QID_SSB_CHANNEL1, "1", QID_MODES_SSB,
		//~ nil, nil, nil, nil, nil, P9_DMDIR|DMEXCL|0777, 0, 0, 0 },
	//~ { QID_SSB_CHANNEL1 + QID_CH_FREQ, "frequency", QID_SSB_CHANNEL1,
		//~ nil, read_field, "r1:freq", write_field, "r1:freq", DMEXCL|0666, 0, 0, 0 },
	//~ { QID_SSB_CHANNEL1 + QID_CH_IF_GAIN, "if_gain", QID_SSB_CHANNEL1,
		//~ nil, read_field, "r1:gain", write_field, "r1:gain", DMEXCL|0666, 0, 0, 0 },
	{ QID_MODES_FT8, "ft8", QID_MODES, SEM_NONE,
		nil, nil, nil, nil, nil, P9_DMDIR|DMEXCL|0777, 0, 0, 0 },
	{ QID_FT8_CHANNEL1, "1", QID_MODES_FT8, SEM_NONE,
		nil, nil, nil, nil, nil, P9_DMDIR|DMEXCL|0777, 0, 0, 0 },

	{ QID_FT8_CHANNEL1 + QID_CH_FREQ, "frequency", QID_FT8_CHANNEL1, SEM_NONE,
		nil, read_field, "r1:freq", write_field, "r1:freq", DMEXCL|0666, 0, 0, 0 },
	{ QID_FT8_CHANNEL1 + QID_CH_FREQ_META, "frequency.meta", QID_FT8_CHANNEL1, SEM_NONE,
		nil, nil, nil, nil, nil, P9_DMDIR|DMEXCL|0777, 0, 0, 0 },
	{ QID_FT8_CHANNEL1 + QID_CH_FREQ_LABEL, "label", QID_FT8_CHANNEL1 + QID_CH_FREQ_META,
		SEM_NONE, nil, read_field_meta, "r1:freq", nil, "", DMEXCL|0444, 0, 0, 0 },
	{ QID_FT8_CHANNEL1 + QID_CH_FREQ_FMT, "format", QID_FT8_CHANNEL1 + QID_CH_FREQ_META,
		SEM_NONE, nil, read_field_meta, "r1:freq", nil, "", DMEXCL|0444, 0, 0, 0 },
	{ QID_FT8_CHANNEL1 + QID_CH_FREQ_MIN, "min", QID_FT8_CHANNEL1 + QID_CH_FREQ_META,
		SEM_NONE, nil, read_field_meta, "r1:freq", nil, "", DMEXCL|0444, 0, 0, 0 },
	{ QID_FT8_CHANNEL1 + QID_CH_FREQ_MAX, "max", QID_FT8_CHANNEL1 + QID_CH_FREQ_META,
		SEM_NONE, nil, read_field_meta, "r1:freq", nil, "", DMEXCL|0444, 0, 0, 0 },
	{ QID_FT8_CHANNEL1 + QID_CH_FREQ_STEP, "step", QID_FT8_CHANNEL1 + QID_CH_FREQ_META,
		SEM_NONE, nil, read_field_meta, "#step", write_field, "#step", DMEXCL|0666, 0, 0, 0 },
	// TODO drive, pitch, tx1st; separate QSO fields and ctl file to transmit?

	{ QID_FT8_CHANNEL1 + QID_CH_IF_GAIN, "if_gain", QID_FT8_CHANNEL1, SEM_NONE,
		nil, read_field, "r1:gain", write_field, "r1:gain", DMEXCL|0666, 0, 0, 0 },
	{ QID_FT8_CHANNEL1 + QID_CH_IF_GAIN_META, "if_gain.meta", QID_FT8_CHANNEL1, SEM_NONE,
		nil, nil, nil, nil, nil, P9_DMDIR|DMEXCL|0777, 0, 0, 0 },
	{ QID_FT8_CHANNEL1 + QID_CH_IF_GAIN_LABEL, "label", QID_FT8_CHANNEL1 + QID_CH_IF_GAIN_META,
		SEM_NONE, nil, read_field_meta, "r1:gain", nil, "", DMEXCL|0444, 0, 0, 0 },
	{ QID_FT8_CHANNEL1 + QID_CH_IF_GAIN_FMT, "format", QID_FT8_CHANNEL1 + QID_CH_IF_GAIN_META,
		SEM_NONE, nil, read_field_meta, "r1:gain", nil, "", DMEXCL|0444, 0, 0, 0 },
	{ QID_FT8_CHANNEL1 + QID_CH_IF_GAIN_MIN, "min", QID_FT8_CHANNEL1 + QID_CH_IF_GAIN_META,
		SEM_NONE, nil, read_field_meta, "r1:gain", nil, "", DMEXCL|0444, 0, 0, 0 },
	{ QID_FT8_CHANNEL1 + QID_CH_IF_GAIN_MAX, "max", QID_FT8_CHANNEL1 + QID_CH_IF_GAIN_META,
		SEM_NONE, nil, read_field_meta, "r1:gain", nil, "", DMEXCL|0444, 0, 0, 0 },
	{ QID_FT8_CHANNEL1 + QID_CH_IF_GAIN_STEP, "step", QID_FT8_CHANNEL1 + QID_CH_IF_GAIN_META,
		SEM_NONE, nil, read_field_meta, "r1:gain", nil, "", DMEXCL|0666, 0, 0, 0 },

	{ QID_FT8_CHANNEL1 + QID_CH_RECEIVED, "received", QID_FT8_CHANNEL1,
		STYLE_FT8_RX, stat_text, read_text, "ft8_1", nil, "", DMEXCL|0666, 0, 0, 0 },
	{ QID_FT8_CHANNEL1 + QID_CH_RECEIVED_META, "received.meta", QID_FT8_CHANNEL1,
		STYLE_FT8_RX, nil, nil, nil, nil, nil, P9_DMDIR|DMEXCL|0777, 0, 0, 0 },
	{ QID_FT8_CHANNEL1 + QID_CH_RECEIVED_SPANS, "spans", QID_FT8_CHANNEL1 + QID_CH_RECEIVED_META,
		STYLE_FT8_RX, stat_text_spans, read_text_spans, "ft8_1", nil, "", DMEXCL|0666, 0, 0, 0 },
	{ QID_FT8_CHANNEL1 + QID_CH_SENT, "sent", QID_FT8_CHANNEL1,
		STYLE_FT8_TX, stat_text, read_text, "ft8_1", nil, "", DMEXCL|0666, 0, 0, 0 },
	{ QID_FT8_CHANNEL1 + QID_CH_SEND, "send", QID_FT8_CHANNEL1, SEM_NONE,
		nil, read_field, "#text_in", write_field, "#text_in", DMEXCL|0666, 0, 0, 0 },
};
static const int devfiles_count = sizeof(devfiles) / sizeof(Devfile);

static FidAux open_fds[MAX_OPEN_FDS];
static ClientEvents client_data[MAX_CLIENTS];

/* Functions */
static int size_read(Ixp9Req *r, const Devfile *df) {
	char buf[MAX_FILE_SIZE];
	return df->doread(r, df, buf, MAX_FILE_SIZE, 0);
}

static int read_field(Ixp9Req *r, const Devfile *df, char *out, int len, int offset) {
	char val[64];
	get_field_value(df->read_name, val); // TODO unsafe: pass sizeof as len
	int vlen = strlen(val);
	if (offset >= vlen)
		return 0;
	char *end = stpncpy(out, val, len); // Plan 9: strecpy
	return end - out;
}

static void write_field(const Devfile *df, const char *val, int len, int offset) {
	debug("write_field %s: '%s' %d %d\n", df->name, val, len, offset);
	set_field(df->write_name, val);
	// workaround for lack of multitasking for now:
	// if the user (re)sets the frequency, set the mode too
	// TODO maybe add files like modes/ft8/1/focus, modes/ssb/1/focus, etc.
	if (df->id == QID_FT8_CHANNEL1 + QID_CH_FREQ)
		set_field("r1:mode", "FT8");
	// else if SSB... etc.
}

static int read_field_meta(Ixp9Req *req, const Devfile *df, char *out, int len, int offset) {
	int min, max, step;
	int r = get_field_meta(df->read_name, &min, &max, &step);
	debug("read_field_meta '%s' 0x%x '%s' len %d offset %d; field min %d max %d step %d\n",
		df->name, df->id, df->read_name, len, offset, min, max, step);
	if (offset == 0) {
		switch (df->id & QID_MASK) {
			case QID_CH_FREQ_LABEL:
				return snprintf(out, len, "Frequency");
			case QID_CH_FREQ_FMT:
				return snprintf(out, len, "%%.0f");
			case QID_CH_FREQ_MIN:
				return snprintf(out, len, "%d", min);
			case QID_CH_FREQ_MAX:
				return snprintf(out, len, "%d", max);
			case QID_CH_FREQ_STEP:
				step = field_int("STEP");
				debug("   special for freq step: %d\n", step);
				return snprintf(out, len, "%d", step);
			
			case QID_SPECTRUM_SPAN_CHOICES: {
				int r = stpncpy(out, get_field_selections(df->read_name), len) - out;
				// replace slashes with tabs (not sbitx-specific: in general, combobox items could have slashes)
				for (int i = 0; i < r; ++i)
					if (out[i] == '/')
						out[i] = '\t';
				return r;
			}

			case QID_CH_IF_GAIN_LABEL:
				return snprintf(out, len, "IF");
			case QID_CH_IF_GAIN_FMT:
				return snprintf(out, len, "%%.0f");
			case QID_CH_IF_GAIN_MIN:
				return snprintf(out, len, "%d", min);
			case QID_CH_IF_GAIN_MAX:
				return snprintf(out, len, "%d", max);
			case QID_CH_IF_GAIN_STEP:
				return snprintf(out, len, "%d", step);
		}
	}
	return 0;
}

static void stat_text(Ixp9Req *r, IxpStat *s, const Devfile *df, int data_index) {
	s->type = 0;
	s->dev = 0;
	// P9_DMDIR is 0x80000000; we send back QID type 0x80 if it's a directory, 0 if not
	// s->qid.type = (df->mode & P9_DMDIR) ? P9_QTDIR : P9_QTFILE;
	s->qid.type = 0;
	s->qid.path = df->id; // fake "inode"
	s->qid.version = df->version;
	s->mode = df->mode;
	s->mtime = console_last_time();
	s->atime = df->atime;
	s->length = console_current_length(df->semantic_filter, data_index);
	s->name = df->name;
	s->uid = user;
	s->gid = user;
	s->muid = user;
	debug("stat_text '%s' 0x%x filter %d console last line %d len %d mtime %u version %u\n",
		df->name, df->id, df->semantic_filter, data_index, s->length, s->mtime, s->qid.version);

	// side-effect: usually the console has a newer mtime than last time;
	// so update mtimes on all 'text' files and their parent dirs, recursively.
	// A better design might be a console callback, but this way seems cheaper for now.
	update_console_mtimes_and_sizes(s->mtime);
}

static int read_text(Ixp9Req *r, const Devfile *df, char *out, int len, int offset) {
	//~ debug("read_text '%s' 0x%x len %d offset %d\n", df->name, df->id, len, offset);
	return get_console_text(out, len, offset, df->semantic_filter);
}

static void stat_raw(Ixp9Req *r, IxpStat *s, const Devfile *df, int data_index) {
	s->type = 0;
	s->dev = 0;
	s->qid.type = 0;
	s->qid.path = df->id; // fake "inode"
	s->qid.version = df->version;
	s->mode = df->mode;
	s->mtime = df->atime; // assume it's different data every time
	s->atime = df->atime;
	s->length = MAX_BINS / 2; // AKA sizeof(wf) / sizeof(int)
	s->name = df->name;
	s->uid = user;
	s->gid = user;
	s->muid = user;
}

static int read_raw(Ixp9Req *r, const Devfile *df, char *out, int len, int offset) {
	//~ debug("read_raw '%s' 0x%x len %d offset %d\n", df->name, df->id, len, offset);
	static uint8_t data[MAX_BINS / 2];
	if (df->id == QID_SPECTRUM) {
		if (offset == 0)
			get_spectrum_8bit((uint8_t *)data, MAX_BINS / 2);
		const int toread = MIN(len, sizeof(data) - offset);
		memcpy(out, data + offset, toread);
		return toread;
	}
	printf("warning: unknown raw data source '%s'\n", df->name);
	return 0;
}

static void stat_text_spans(Ixp9Req *r, IxpStat *s, const Devfile *df, int data_index) {
	// expectation of the file server's user: verify that the struct packs as intended
	assert(sizeof(text_span_semantic) == sizeof(uint64_t));
	s->type = 0;
	s->dev = 0;
	// P9_DMDIR is 0x80000000; we send back QID type 0x80 if it's a directory, 0 if not
	// s->qid.type = (df->mode & P9_DMDIR) ? P9_QTDIR : P9_QTFILE;
	s->qid.type = 0;
	s->qid.path = df->id; // fake "inode"
	s->qid.version = df->version;
	s->mode = df->mode;
	s->mtime = console_last_time();
	s->atime = df->atime;
	s->length = console_current_spans_length(df->semantic_filter, data_index);
	s->name = df->name;
	s->uid = user;
	s->gid = user;
	s->muid = user;
	debug("stat_text_spans '%s' 0x%x filter %d console last line %d len %d mtime %u version %u\n",
		df->name, df->id, df->semantic_filter, data_index, s->length, s->mtime, s->qid.version);
}

static int read_text_spans(Ixp9Req *r, const Devfile *df, char *out, int len, int offset) {
	//~ debug("read_text_spans '%s' 0x%x len %d offset %d\n", df->name, df->id, len, offset);
	return get_console_text_spans((text_span_semantic *)out, len, offset, df->semantic_filter);
}

static void update_console_mtimes_and_sizes(time_t mtime) {
	int updated_parents = 0;
	for (int f = devfiles_count - 1; f > 0; --f)
		if (devfiles[f].dostat == stat_text) {
			if (mtime > devfiles[f].mtime) {
				devfiles[f].mtime = mtime;
				++devfiles[f].version;
			}
			if (updated_parents)
				continue;
			int parent_sought = devfiles[f].parent;
			//~ debug("mtime %u version %u; checking for ancestor %d of %d '%s' (total files %d)\n",
				//~ mtime, devfiles[f].version, parent_sought, devfiles[f].id, devfiles[f].name, devfiles_count);
			for (int j = f - 1; j >= 0; --j) {
				if (devfiles[j].id == parent_sought) {
					if (mtime > devfiles[j].mtime) {
						devfiles[j].mtime = mtime;
						++devfiles[j].version;
						//~ debug("   updated %d '%s': set mtime %u version %u\n",
							//~ devfiles[j].id, devfiles[j].name, devfiles[j].mtime, devfiles[j].version);
					}
					parent_sought = devfiles[j].parent;
				}
			}
			// one trip up the hierarchy is enough (at least for one mode, one channel)
			updated_parents = 1;
		}
}

static void fs_open(Ixp9Req *r);
static void fs_walk(Ixp9Req *r);
static void fs_read(Ixp9Req *r);
static void fs_stat(Ixp9Req *r);
static void fs_write(Ixp9Req *r);
static void fs_clunk(Ixp9Req *r);
static void fs_flush(Ixp9Req *r);
static void fs_attach(Ixp9Req *r);
static void fs_create(Ixp9Req *r);
static void fs_remove(Ixp9Req *r);
static void fs_freefid(IxpFid *f);

Ixp9Srv p9srv = {
	.open=	fs_open,
	.walk=	fs_walk,
	.read=	fs_read,
	.stat=	fs_stat,
	.write=	fs_write,
	.clunk=	fs_clunk,
	.flush=	fs_flush,
	.attach=fs_attach,
	.create=fs_create,
	.remove=fs_remove,
	.freefid=fs_freefid
};

static void usage() {
	fprintf(stderr,
		   "usage: %1$s [-a <address>] {create | read | ls [-ld] | remove | write} <file>\n"
		   "       %1$s [-a <address>] xwrite <file> <data>\n"
		   "       %1$s -v\n", argv0);
	exit(1);
}

/* Utility Functions */
static FidAux* newfidaux(Devfile *df, void *srvaux) {
	if (df->id == QID_EVENT) {
		bool notfound = TRUE;
		// find srvaux in client_data
		for (int i = 0; i < MAX_CLIENTS && notfound; ++i) {
			if (client_data[i].srvaux == srvaux) {
				notfound = FALSE;
				debug("newfidaux(srv-aux %p): found client_data idx %d for event file\n", srvaux, i);
			}
		}
		if (notfound) {
			// find an empty slot in client_data
			for (int i = 0; i < MAX_CLIENTS; ++i) {
				if (!client_data[i].srvaux) {
					// TODO memset to clear this entry? nah, do it on detach
					client_data[i].srvaux = srvaux;
					debug("newfidaux(srv-aux %p): chose client_data idx %d for event file\n", srvaux, i);
					break;
				}
			}
		}
	}
	// find an empty slot in open_fds
	for (int i = 0; i < MAX_OPEN_FDS; ++i) {
		if (!open_fds[i].file) {
			open_fds[i].file = df;
			open_fds[i].fd = i;
			open_fds[i].offset = 0;
			open_fds[i].data_index = -1;
			open_fds[i].srvaux = srvaux;
			debug("newfidaux(df %p srv-aux %p): fd %d %p\n", df, srvaux, i, &open_fds[i]);
			return &open_fds[i];
		}
		//~ debug("FID %p already in use: fd %d %p\n", &open_fds[i], open_fds[i].fd, open_fds[i].file);
	}
	fprintf(stderr, "newfidaux: MAX_OPEN_FDS exceeded\n");
	return nil;
}

static FidAux *findfidaux(void *srvaux, FidAux *start_from) {
	const int startfrom_idx = start_from ? start_from - open_fds : 0;
	//~ if (start_from) debug("findfidaux: start from %p rather than %p: index %d\n", start_from, open_fds, startfrom_idx);
	for (int i = startfrom_idx; i < MAX_OPEN_FDS; ++i)
		if (open_fds[i].srvaux == srvaux)
			return &open_fds[i];
	return nil;
}

static ClientEvents *find_client_data(void *srvaux, ClientEvents *start_from) {
	const int startfrom_idx = start_from ? start_from - client_data : 0;
	//~ if (start_from) debug("find_client_data: start from %p rather than %p: index %d\n", start_from, client_data, startfrom_idx);
	for (int i = startfrom_idx; i < MAX_CLIENTS; ++i)
		if (client_data[i].srvaux == srvaux)
			return &client_data[i];
	return nil;
}

static Devfile *find_file(const char *name, int start_from_idx) {
	if (!strcmp(name, "/"))
		return devfiles;

	for (int i = start_from_idx; i < devfiles_count; ++i) {
		if (!strcmp(name, devfiles[i].name))
			return &devfiles[i];
	}

	return nil;
}

static Devfile *find_by_field_id(const char *read_name) {
	if (!read_name || !read_name[0])
		return nil;
	for (int f = devfiles_count - 1; f > 0; --f)
		if (devfiles[f].read_name && !strcmp(devfiles[f].read_name, read_name))
			return &devfiles[f];
	return nil;
}

void notify_field_changed(const char *field_id, const char *old, const char *newval) {
	if (next_client_id == FIRST_CLIENT_ID)
		return; // no clients connected, nobody to notify
	// TODO do this below, after df and connected client are found
	if (!strncmp(old, newval, 64))
		return; // no change in value
	Devfile *df = find_by_field_id(field_id);
	debug("notify_field_changed: '%s' found 0x%X\n", field_id, df ? df->id : 0);
	if (df) {
		for (int i = 0; i < MAX_CLIENTS; ++i)
			if (client_data[i].srvaux) {
				bool notfound = TRUE;
				for (int j = 0; j < MAX_EVENTS && notfound; ++j)
					if (client_data[i].changed[j] == df)
						notfound = FALSE;
				if (notfound) {
					client_data[i].changed[client_data[i].count++] = df;
					client_data[i].byte_len += strlen(df->name) + 1;
				}
			}
	}
}

static void stat_event(Ixp9Req *r, IxpStat *s, const Devfile *df, int data_index) {
	ClientEvents *cd = find_client_data(r->srv->aux, client_data);
	
	s->type = 0;
	s->dev = 0;
	// P9_DMDIR is 0x80000000; we send back QID type 0x80 if it's a directory, 0 if not
	// s->qid.type = (df->mode & P9_DMDIR) ? P9_QTDIR : P9_QTFILE;
	s->qid.type = 0;
	s->qid.path = df->id; // fake "inode"
	s->qid.version = df->version;
	s->mode = df->mode;
	s->mtime = console_last_time();
	s->atime = df->atime;
	s->length = cd ? cd->byte_len : 0; // add up lengths of all changed filenames
	s->name = df->name;
	s->uid = user;
	s->gid = user;
	s->muid = user;
	debug("stat_event srv-aux %p len %d mtime %u version %u\n",
		r->srv->aux, s->length, s->mtime, s->qid.version);

	// side-effect: usually the console has a newer mtime than last time;
	// so update mtimes on all 'text' files and their parent dirs, recursively.
	// A better design might be a console callback, but this way seems cheaper for now.
	update_console_mtimes_and_sizes(s->mtime);
}

static int read_event(Ixp9Req *r, const Devfile *df, char *out, int len, int offset) {
	ClientEvents *cd = find_client_data(r->srv->aux, client_data);
	char *end = out;
	if (cd) {
		for (int i = 0; i < cd->count; ++i) {
			assert(cd->changed[i]);
			if ((end - out) + strlen(cd->changed[i]->name) + 1 > len) {
				memmove(cd->changed, cd->changed + i, (cd->count - i) * sizeof(Devfile *));
				cd->count -= i;
				cd->byte_len -= (end - out);
				break;
			}
			end = stpncpy(end, cd->changed[i]->name, MAX_PATH_SUFFIX_SIZE);
			*end++ = '\n';
		}
		cd->count = 0;
		cd->byte_len = 0;
		memset(cd->changed, 0, sizeof(cd->changed));
	}
	return end - out;
}

static void dostat(Ixp9Req *r, IxpStat *s, const Devfile *df, int index) {
	if (df->dostat) {
		df->dostat(r, s, df, index);
		return;
	}

	s->type = 0;
	s->dev = 0;
	// P9_DMDIR is 0x80000000; we send back QID type 0x80 if it's a directory, 0 if not
	// s->qid.type = (df->mode & P9_DMDIR) ? P9_QTDIR : P9_QTFILE;
	s->qid.type = df->mode >> 24;
	s->qid.path = df->id; // fake "inode"
	s->qid.version = df->version;
	s->mode = df->mode;
	s->atime = df->atime ? df->atime : start_time;
	s->mtime = df->mtime ? df->mtime : start_time;
	s->length = df->doread ? size_read(r, df) : 0;
	s->name = df->name;
	s->uid = user;
	s->gid = user;
	s->muid = user;
	debug("dostat: '%s'\ttype 0x%02x id %d parent %d mode 0x%x user '%s' times %d %d\n",
				s->name, s->qid.type, s->qid.path, df->parent, s->mode, s->uid, s->mtime, s->atime);
}

void rerrno(Ixp9Req *r, char *m) {
	ixp_respond(r, m);
}

void fs_attach(Ixp9Req *r) {
	debug("fs_attach(%p fid %p) srv-aux %p\n", r, r->fid, next_client_id);
	r->fid->qid.type = QTDIR;
	r->fid->qid.path = (uintptr_t)r->fid;
	r->fid->aux = newfidaux(devfiles, (void*)next_client_id);
	// TODO return client_data ptr as srv->aux for this client rather than just using a recognizable fake pointer?
	r->srv->aux = (void*)next_client_id;
	r->ofcall.rattach.qid = r->fid->qid;
	ixp_respond(r, nil);
	++next_client_id;
}

void fs_walk(Ixp9Req *r) {
	char name[PATH_MAX];
	FidAux *f;
	int i = 0;

	f = r->fid->aux;
	if (!f || !f->file) {
		ixp_respond(r, Enofile);
		return;
	}
	//~ debug("fs_walk(%p %d) srv-aux %p %p from %s\n", f, r->fid->fid, r->srv->aux, f->file, f->file->name);
	name[0] = 0;

	Devfile *df = find_file(f->file->name, 1);
	if (!df)
		ixp_respond(r, Enofile);
	//~ debug("   found starting qid 0x%d mode 0x%x %s\n", df->id, df->mode, df->name);

	// build full path; populate qid type and ID to return
	for(i=0; i < r->ifcall.twalk.nwname; i++) {
		char *subname = r->ifcall.twalk.wname[i];
		if (subname[0] != '/')
			strcat(name, "/");
		strcat(name, subname);
		df = find_file(subname, df - devfiles);
		if (df) {
			//~ debug("   sub %s (path %s): found %s ID %d mode 0x%x\n",
				//~ subname, name, df->name, df->id, df->mode);
		} else {
			//~ debug("   sub %s (path %s): not found\n", subname, name);
			rerrno(r, Enoperm);
			return;
		}

		// P9_DMDIR is 0x80000000; we send back QID type 0x80 if it's a directory, 0 if not
		r->ofcall.rwalk.wqid[i].type = df->mode >> 24;
		r->ofcall.rwalk.wqid[i].path = df->id;
	}
	debug("   fs_walk %d srv-aux %p fid %d name %s\n", i, r->srv->aux, r->fid->fid, name);
	r->newfid->aux = newfidaux(df, r->srv->aux);
	r->ofcall.rwalk.nwqid = i;
	ixp_respond(r, nil);
}

void fs_stat(Ixp9Req *r) {
	IxpStat s;
	IxpMsg m;
	char *buf;
	FidAux *f;
	int size;

	f = r->fid->aux;
	if (!f || !f->file) {
		rerrno(r, Ebadfid);
		return;
	}
	//~ debug("fs_stat(%p) %s\n", r, f->file->name);

	dostat(r, &s, f->file, f->data_index);
	r->fid->qid = s.qid;
	r->ofcall.rstat.nstat = size = ixp_sizeof_stat(&s);
	buf = ixp_emallocz(size);
	m = ixp_message(buf, size, MsgPack);
	ixp_pstat(&m, &s);
	r->ofcall.rstat.stat = m.data;
	ixp_respond(r, nil);
}

void fs_read(Ixp9Req *r) {
	FidAux *f;
	char *buf;
	int offset;
	int size;

	f = r->fid->aux;
	if (!f || !f->file) {
		rerrno(r, Ebadfid);
		return;
	}

	if (f->file->mode & P9_DMDIR) {
		IxpStat s;
		IxpMsg m;

		offset = 0;
		size = r->ifcall.tread.count;
		buf = ixp_emallocz(size);
		m = ixp_message(buf, size, MsgPack);

		//~ debug("fs_read fd %d srv-aux %p: dir starting from offset %d in qid 0x%x '%s'; total files %d\n", f->fd, r->srv->aux, f->offset, f->file->id, f->file->name, devfiles_count);

		/*  for each entry in dir, populate IxpStat s,
			then use that to append to IxpMsg m
			f->offset is a dir entry index in this case (whereas when reading from a file, it's offset within the file)
			but when we are reading from offset 0, that's the dir itself: skip it */
		int found_count = 0;
		for (int i = 0; i < devfiles_count; ++i) {
			if (devfiles[i].parent != f->file->id || found_count < f->offset) {
//~ debug("skipping '%s' par %d not? %d found_count %d offset %d\n", devfiles[i].name, devfiles[i].parent, f->file->id, found_count, f->offset);
				continue;
			}
			dostat(r, &s, &devfiles[i], f->data_index);
			offset += ixp_sizeof_stat(&s);
			ixp_pstat(&m, &s);
			++found_count;
		}

		f->offset = found_count;
		r->ofcall.rread.count = offset;
		r->ofcall.rread.data = (char*)m.data;
		ixp_respond(r, nil);
		return;
	} else if (f->file->doread) {
		//~ debug("fs_read '%s' qid 0x%x: req size %d offset %d\n", f->file->name, f->file->id, r->ifcall.tread.count, r->ifcall.tread.offset);
		r->ofcall.rread.data = ixp_emallocz(r->ifcall.tread.count);
		if (! r->ofcall.rread.data) {
			ixp_respond(r, nil);
			return;
		}
		r->ofcall.rread.count = f->file->doread(r, f->file, r->ofcall.rread.data, r->ifcall.tread.count, r->ifcall.tread.offset);
		if ((f->file->id & QID_FT8_CHANNEL1) && (f->file->id & QID_MASK) == QID_CH_RECEIVED && f->data_index >= 0) {
			// unlock / end transaction, in case new data is coming in. The client will find out about that at the next fs_stat().
			debug("   qid 0x%x: resetting console last line (was %d)\n", f->file->id, f->data_index);
			f->data_index = -1;
		}
		if (r->ofcall.rread.count < 0)
			rerrno(r, Enoperm);
		else
			ixp_respond(r, nil);
		return;
	}

	// fs_read should not be called if the file is not open for reading
	assert(!"Read called on an unreadable file");
}

// https://stackoverflow.com/questions/122616/how-do-i-trim-leading-trailing-whitespace-in-a-standard-way
// Note: This function returns a pointer to a substring of the original string.
// If the given string was allocated dynamically, the caller must not overwrite
// that pointer with the returned value, since the original pointer must be
// deallocated using the same allocator with which it was allocated.  The return
// value must NOT be deallocated using free() etc.
char *trimwhitespace(char *str)
{
  char *end;

  // Trim leading space
  while(isspace((unsigned char)*str)) str++;

  if(*str == 0)  // All spaces?
    return str;

  // Trim trailing space
  end = str + strlen(str) - 1;
  while(end > str && isspace((unsigned char)*end)) end--;

  // Write new null terminator character
  end[1] = '\0';

  return str;
}

void fs_write(Ixp9Req *r) {
	if(r->ifcall.twrite.count == 0) {
		ixp_respond(r, nil);
		return;
	}
	FidAux *f = r->fid->aux;
	if (!f || !f->file) {
		rerrno(r, Ebadfid);
		return;
	}
	if (!(f->file->mode & P9_DMWRITE) || !f->file->dowrite) {
		rerrno(r, Enoperm);
		return;
	}
	char buf[r->ifcall.twrite.count + 1];
	char *end = stpncpy(buf, r->ifcall.twrite.data, r->ifcall.twrite.count);
	*end = 0;
	char *trimmed = trimwhitespace(buf);
	debug("fs_write(%p) %s: '%s' count %d offset %d\n", r, f->file->name, trimmed, r->ifcall.twrite.count, r->ifcall.twrite.offset);
	f->file->dowrite(f->file, trimmed, r->ifcall.twrite.count, r->ifcall.twrite.offset);
	f->offset = r->ofcall.rwrite.count = r->ifcall.twrite.count;
	ixp_respond(r, nil);
}

void fs_open(Ixp9Req *r) {
	// the client had to walk to the file first, therefore this request should include the FidAux
	FidAux *f = r->fid->aux;
	if (!f || !f->file) {
		rerrno(r, Ebadfid);
		return;
	}
	int data_index = -1;
	// If the client has opened "spans" first (as it should) and is now opening some text file
	// that has spans (such as "received"), use the same data_index (console line number).
	if (!strcmp(f->file->name, "received")) {
		FidAux *spans_fidaux = findfidaux(r->srv->aux, nil);
		while (spans_fidaux && spans_fidaux->file &&
				!((spans_fidaux->file->id & QID_FT8_CHANNEL1) && (spans_fidaux->file->id & QID_MASK) == QID_CH_RECEIVED_SPANS) )
			spans_fidaux = findfidaux(r->srv->aux, spans_fidaux + 1);
		if (spans_fidaux) {
			data_index = spans_fidaux->data_index;
			debug("fs_open '%s': got console last line %d from previous\n", f->file->name, data_index);
			//debug("fs_open '%s': got console last line %d from previously-opened '%s'\n", f->file->name, data_index, (spans_fidaux->file->name ? spans_fidaux->file->name : "noname"));
			f->data_index = data_index;
		}
	}
	if (!strcmp(f->file->name, "spans")) {
		if (data_index < 0)
			data_index = console_last_line();
		f->data_index = data_index;
	}
	//~ debug("fs_open '%s' mode 0x%x fd %d aux %p srv-aux %p\n", f->file->name, r->fid->omode, f->fd, r->aux, r->srv->aux);

	/*
	if (f->file->mode & P9_DMDIR) {
		// nothing to do
	} else {
		// if it somehow doesn't exist: rerrno(r, Enoperm); return;
		// nothing to do: fd is set in fs_walk
	}
	*/
	f->file->atime = time_sbitx();
	ixp_respond(r, nil);
}

void fs_create(Ixp9Req *r) {
	debug("fs_create: nope\n", r);
	ixp_respond(r, Enoperm);
}

void fs_remove(Ixp9Req *r) {
	debug("fs_remove: nope\n");
	ixp_respond(r, Enoperm);
}

void fs_flush(Ixp9Req *r) {
	//~ debug("fs_flush: nothing to do\n");
	ixp_respond(r, nil);
}

void fs_clunk(Ixp9Req *req) {
	FidAux *f = req->fid->aux;
	if (!f || !f->file) {
		rerrno(req, Ebadfid);
		return;
	}
	debug("fs_clunk '%s' fd %d srv-aux %p mode 0x%x offset %d file %p\n", f->file->name, f->fd, req->srv->aux, req->fid->omode, f->offset, f->file);
	if (f->file->id == QID_FT8_CHANNEL1 + QID_CH_SEND && (req->fid->omode & P9_OWRITE)) {
		if (f->offset > 1) {
			char text[64];
			int r = read_field(req, f->file, text, sizeof(text), 0);
			if (r > 1) {
				int pitch = field_int("TX_PITCH");
				debug("--- send %d '%s' pitch %d\n", r, text, pitch);
				ft8_tx(text, pitch);
			}
		} else {
			set_field(f->file->write_name, "");
			ft8_abort();
		}
	}
	f->fd = -1;
	// TODO if the FidAux was allocated in fs_attach, free client data at this time (the client detached)
	ixp_respond(req, nil);
}

void fs_freefid(IxpFid *f) {
	if (!f || !f->aux) {
		debug("free FID: null FID or aux\n");
		return;
	}
	FidAux *aux = f->aux;
	debug("fs_freefid(%p) fd %d file %p\n", aux, aux->fd, aux->file);
	aux->file = nil;
}

void kill_9p()
{
	printf("kill_9p (pid %d)\n", pid);
	kill(pid, SIGINT);
}

void *run_9p(void *arg) {
	if(!(user = getenv("USER"))) {
		fatal("start_9p: $USER not set\n");
		return nil;
	}

    char listen_addr[NI_MAXHOST];

	{
		struct ifaddrs *ifaddr, *ifa;
		int family, s;
		char host[NI_MAXHOST];

		if (getifaddrs(&ifaddr) == -1) {
			perror("start_9p: getifaddrs");
			return nil;
		}

		for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) {
			if (ifa->ifa_addr == NULL)
				continue;

			if (strncmp(ifa->ifa_name, "lo", 2) && ifa->ifa_addr->sa_family==AF_INET) {
				int s=getnameinfo(ifa->ifa_addr,sizeof(struct sockaddr_in),host, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);
				if (s != 0) {
					printf("start_9p: getnameinfo() failed: %s\n", gai_strerror(s));
					return nil;
				}
				sprintf(listen_addr, "tcp!%s!564", host);
				printf("start_9p found %s: %s; will listen on %s\n",ifa->ifa_name, host, listen_addr);
				break;
			}
		}

		freeifaddrs(ifaddr);
	}

	int fd = ixp_announce(listen_addr);
	if(fd < 0) {
		perror(listen_addr);
		return nil; // fatal("start_9p: ixp_announce: %s\n", errstr);
	}

	memset(open_fds, 0, sizeof(open_fds));
	memset(client_data, 0, sizeof(client_data));
	start_time = time_sbitx();

	IxpConn *acceptor = ixp_listen(&server, fd, &p9srv, ixp_serve9conn, NULL);
	ixp_serverloop(&server);
	return nil; // shouldn't get here
}

void start_9p() {
	pthread_t listener_thread;
    if (pthread_create(&listener_thread, NULL, run_9p, NULL) != 0) {
        perror("Failed to create 9p listener thread");
        exit(1);
    }
    pthread_detach(listener_thread); // Detach the thread to run independently
}
