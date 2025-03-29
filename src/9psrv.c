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

#include "sdr_ui.h"

/* Datatypes */
typedef struct Devfile Devfile;
struct Devfile {
	uint64_t id; // aka qid.path
	char	*name;
	int parent;
	sbitx_style semantic_filter;
	void	(*dostat)(IxpStat *s, const Devfile *df);
	int	(*doread)(const Devfile *df, char*, int, int);
	const char	*read_name;
	void	(*dowrite)(const char*, const char*, int, int);
	const char	*write_name;
	mode_t	mode;
	time_t atime;
	time_t mtime;
	uint32_t version;
};

typedef struct FidAux FidAux;
struct FidAux {
	int fd;
	int offset;
	Devfile *file;
};

/* Error Messages */
static char
	Enoperm[] = "permission denied",
	Enofile[] = "file not found",
	Ebadvalue[] = "bad value",
	Ebadfid[] = "bad FID";

/* Macros */
// TODO output timestamps
#define fatal(...) ixp_eprint("fatal: " __VA_ARGS__)
#define debug(...) if(debuglevel) fprintf(stderr, __VA_ARGS__)
#define QID(t, i) ((int64_t)(t))
#define MAX_OPEN_FDS 256
#define MAX_FILE_SIZE 1024

/* Global Vars */
static IxpServer server;
static pid_t pid = 0;
static char *user;
static int debuglevel = 1;
static time_t start_time;
static char *argv0;

static int size_read(const struct Devfile *df) {
	char buf[MAX_FILE_SIZE];
	return df->doread(df, buf, MAX_FILE_SIZE, 0);
}

static int read_field(const struct Devfile *df, char *out, int len, int offset) {
	char val[64];
	get_field_value(df->read_name, val); // TODO unsafe: pass sizeof as len
	int vlen = strlen(val);
	if (offset >= vlen)
		return 0;
	char *end = stpncpy(out, val, len); // Plan 9: strecpy
	return end - out;
}

static void write_field(const char *name, const char *val, int len, int offset) {
	debug("write_field %s: '%s' %d %d\n", name, val, len, offset);
	set_field(name, val);
}

static void update_console_mtimes_and_sizes(time_t mtime);

static void stat_text(IxpStat *s, const Devfile *df) {
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
	s->length = console_current_length(df->semantic_filter);
	s->name = df->name;
	s->uid = user;
	s->gid = user;
	s->muid = user;
	debug("stat_text '%s' filter %d len %d mtime %u\n", df->name, df->semantic_filter, s->length, s->mtime);

	// side-effect: usually the console has a newer mtime than last time;
	// so update mtimes on all 'text' files and their parent dirs, recursively.
	// A better design might be a console callback, but this way seems cheaper for now.
	update_console_mtimes_and_sizes(s->mtime);
}

static int read_text(const Devfile *df, char *out, int len, int offset) {
	debug("read_text '%s' len %d offset %d\n", df->name, len, offset);
	return get_console_text(out, len, offset, df->semantic_filter);
}

static void stat_text_spans(IxpStat *s, const Devfile *df) {
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
	s->length = console_current_spans_length(df->semantic_filter);
	s->name = df->name;
	s->uid = user;
	s->gid = user;
	s->muid = user;
	debug("stat_text_spans '%s' filter %d len %d mtime %u\n", df->name, df->semantic_filter, s->length, s->mtime);
}

static int read_text_spans(const Devfile *df, char *out, int len, int offset) {
	debug("read_text_spans '%s' len %d offset %d\n", df->name, len, offset);
	return get_console_text_spans((text_span_semantic *)out, len, offset, df->semantic_filter);
}

#define SEM_NONE STYLE_LOG
static Devfile devfiles[] = {
	{ 0, "/", -1, SEM_NONE, nil, nil, nil, nil, nil, P9_DMDIR|DMEXCL|0777, 0, 0, 0 },
	{ 1, "settings", 0, SEM_NONE, nil, nil, nil, nil, nil, P9_DMDIR|DMEXCL|0777, 0, 0, 0 },
	{ 2, "callsign", 1, SEM_NONE, nil, read_field, "#mycallsign", write_field, "#mycallsign", DMEXCL|0666, 0, 0, 0 },
	{ 3, "grid", 1, SEM_NONE, nil, read_field, "#mygrid", write_field, "#mygrid", DMEXCL|0666, 0, 0, 0 },
	{ 40, "text", 0, SEM_NONE, stat_text, read_text, "all", nil, "", DMEXCL|0666, 0, 0, 0 },
	{ 100, "modes", 0, SEM_NONE, nil, nil, nil, nil, nil, P9_DMDIR|DMEXCL|0777, 0, 0, 0 },
	//~ { 101, "ssb", 100, nil, nil, nil, nil, nil, P9_DMDIR|DMEXCL|0777, 0, 0, 0 },
	//~ { 1000, "1", 101, nil, nil, nil, nil, nil, P9_DMDIR|DMEXCL|0777, 0, 0, 0 },
	//~ { 1001, "frequency", 1000, nil, read_field, "r1:freq", write_field, "r1:freq", DMEXCL|0666, 0, 0, 0 },
	//~ { 1002, "if_gain", 1000, nil, read_field, "r1:gain", write_field, "r1:gain", DMEXCL|0666, 0, 0, 0 },
	{ 102, "ft8", 100, SEM_NONE, nil, nil, nil, nil, nil, P9_DMDIR|DMEXCL|0777, 0, 0, 0 },
	{ 2000, "1", 102, SEM_NONE, nil, nil, nil, nil, nil, P9_DMDIR|DMEXCL|0777, 0, 0, 0 },
	{ 2001, "frequency", 2000, SEM_NONE, nil, read_field, "r1:freq", write_field, "r1:freq", DMEXCL|0666, 0, 0, 0 },
	{ 2002, "if_gain", 2000, SEM_NONE, nil, read_field, "r1:gain", write_field, "r1:gain", DMEXCL|0666, 0, 0, 0 },
	{ 2003, "received", 2000, STYLE_FT8_RX, stat_text, read_text, "ft8_1", nil, "", DMEXCL|0666, 0, 0, 0 },
	{ 2004, "received.meta", 2000, STYLE_FT8_RX, nil, nil, nil, nil, nil, P9_DMDIR|DMEXCL|0777, 0, 0, 0 },
	{ 2005, "spans", 2004, STYLE_FT8_RX, stat_text_spans, read_text_spans, "ft8_1", nil, "", DMEXCL|0666, 0, 0, 0 },
	{ 2006, "sent", 2000, STYLE_FT8_TX, stat_text, read_text, "ft8_1", nil, "", DMEXCL|0666, 0, 0, 0 },
};
static const int devfiles_count = sizeof(devfiles) / sizeof(Devfile);

static void update_console_mtimes_and_sizes(time_t mtime) {
	for (int f = devfiles_count - 1; f > 0; --f)
		if (devfiles[f].dostat == stat_text) {
			int parent_sought = devfiles[f].parent;
			debug("mtime %u; checking for ancestor %d of %d '%s' (total files %d)\n",
				mtime, parent_sought, devfiles[f].id, devfiles[f].name, devfiles_count);
			for (int j = f - 1; j >= 0; --j) {
				if (devfiles[j].id == parent_sought) {
					if (mtime > devfiles[j].mtime) {
						devfiles[j].mtime = mtime;
						++devfiles[j].version;
						debug("   found %d '%s', set mtime %u version %u\n",
							devfiles[j].id, devfiles[j].name, devfiles[j].mtime, devfiles[j].version);
					}
					parent_sought = devfiles[j].parent;
				}
			}
			return; // one trip up the hierarchy is enough (at least for one mode, one channel)
		}
}

static FidAux open_fds[MAX_OPEN_FDS];

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
static FidAux* newfidaux(Devfile *df) {
	// find an empty slot in open_fds
	for (int i = 0; i < MAX_OPEN_FDS; ++i) {
		if (!open_fds[i].file) {
			open_fds[i].file = df;
			open_fds[i].fd = i;
			open_fds[i].offset = 0;
			debug("newfidaux(df %p): fd %d %p\n", df, i, &open_fds[i]);
			return &open_fds[i];
		}
		debug("FID %p already in use: fd %d %p\n", &open_fds[i], open_fds[i].fd, open_fds[i].file);
	}
	return nil;
}

static Devfile *find_file(const char *name) {
	if (!strcmp(name, "/"))
		return devfiles;

	for (int i = 1; i < devfiles_count; ++i) {
		if (!strcmp(name, devfiles[i].name))
			return &devfiles[i];
	}

	return nil;
}

static void dostat(IxpStat *s, const Devfile *df) {
	if (df->dostat) {
		df->dostat(s, df);
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
	s->length = df->doread ? size_read(df) : 0;
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

	debug("fs_attach(%p fid %p)\n", r, r->fid);

	r->fid->qid.type = QTDIR;
	r->fid->qid.path = (uintptr_t)r->fid;
	r->fid->aux = newfidaux(devfiles);
	r->ofcall.rattach.qid = r->fid->qid;
	ixp_respond(r, nil);
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
	debug("fs_walk(%p %d) %p from %s\n", f, r->fid->fid, f->file, f->file->name);
	name[0] = 0;

	Devfile *df = find_file(f->file->name);
	if (!df)
		ixp_respond(r, Enofile);
	debug("   found starting qid %d mode 0x%x %s\n", df->id, df->mode, df->name);

	// build full path; populate qid type and ID to return
	for(i=0; i < r->ifcall.twalk.nwname; i++) {
		char *subname = r->ifcall.twalk.wname[i];
		if (subname[0] != '/')
			strcat(name, "/");
		strcat(name, subname);
		df = find_file(subname);
		if (df)
			debug("   sub %s (path %s): found %s ID %d mode 0x%x\n",
				subname, name, df->name, df->id, df->mode);
		else
			debug("   sub %s: not found\n");

		// P9_DMDIR is 0x80000000; we send back QID type 0x80 if it's a directory, 0 if not
		r->ofcall.rwalk.wqid[i].type = df->mode >> 24;
		r->ofcall.rwalk.wqid[i].path = df->id;
	}
	debug("   fs_walk final %d name %s\n", i, name);
	r->newfid->aux = newfidaux(df);
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
	debug("fs_stat(%p) %s\n", r, f->file->name);

	dostat(&s, f->file);
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

		debug("fs_read fd %d: dir starting from offset %d in qid %d '%s'; total files %d\n", f->fd, f->offset, f->file->id, f->file->name, devfiles_count);

		/*  for each entry in dir, populate IxpStat s,
			then use that to append to IxpMsg m
			f->offset is a dir entry index in this case (whereas when reading from a file, it's offset within the file)
			but when we are reading from offset 0, that's the dir itself: skip it */
		int found_count = 0;
		for (int i = 0; i < devfiles_count; ++i) {
			if (devfiles[i].parent != f->file->id || found_count < f->offset) {
//~ printf("skipping '%s' par %d not? %d found_count %d offset %d\n", devfiles[i].name, devfiles[i].parent, f->file->id, found_count, f->offset);
				continue;
			}
			dostat(&s, &devfiles[i]);
			offset += ixp_sizeof_stat(&s);
			ixp_pstat(&m, &s);
			++found_count;
		}

		f->offset = found_count;
		r->ofcall.rread.count = offset;
		r->ofcall.rread.data = (char*)m.data;
		ixp_respond(r, nil);
		return;
	} else {
		debug("   fs_read %s: req size %d offset %d\n", f->file->name, r->ifcall.tread.count, r->ifcall.tread.offset);
		r->ofcall.rread.data = ixp_emallocz(r->ifcall.tread.count);
		if (! r->ofcall.rread.data) {
			ixp_respond(r, nil);
			return;
		}
		r->ofcall.rread.count = f->file->doread(f->file, r->ofcall.rread.data, r->ifcall.tread.count, r->ifcall.tread.offset);
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
	debug("fs_write(%p) %s: '%s' %d %d\n", r, f->file->name, trimmed, r->ifcall.twrite.count, r->ifcall.twrite.offset);
	f->file->dowrite(f->file->write_name, trimmed, r->ifcall.twrite.count, r->ifcall.twrite.offset);
	r->ofcall.rwrite.count = r->ifcall.twrite.count;
	ixp_respond(r, nil);
}

void fs_open(Ixp9Req *r) {
	FidAux *f = r->fid->aux;
	if (!f || !f->file) {
		rerrno(r, Ebadfid);
		return;
	}
	debug("fs_open(%p) '%s' fd %d\n", r, f->file->name, f->fd);
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
	debug("fs_create(%p)\n", r);
	ixp_respond(r, Enoperm);
}

void fs_remove(Ixp9Req *r) {
	debug("fs_remove(%p)\n", r);
	ixp_respond(r, Enoperm);
}

void fs_clunk(Ixp9Req *r) {
	FidAux *f = r->fid->aux;
	if (!f || !f->file) {
		rerrno(r, Ebadfid);
		return;
	}
	debug("fs_clunk(%p) fd %d file %p\n", f, f->fd, f->file);
	f->fd = -1;
	ixp_respond(r, nil);
}

void fs_flush(Ixp9Req *r) {
	debug("fs_flush(%p)\n", r);
	ixp_respond(r, nil);
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
				sprintf(listen_addr, "tcp!%s!1564", host);
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
