/*
 * BitTorrent file system.  Conventionally mounted at /mnt/bt.
 *
 * clone		open and read to obtain new torrent
 * n			torrent directory
 * 	ctl		control messages (like start or stop)
 * 	metainfo	torrent meta-information (.torrent file)
 * 	n		file directory
 * 		name	file recommended name
 * 		data	file contents
 */
#include <u.h>
#include <libc.h>
#include "btfs.h"

enum
{
	Qroot,
	Qclone,
	Qtorrent,
	Qctl,
	Qmetainfo,
	Qfile,
	Qname,
	Qdata
};

#define PATH(type, n) ((type)|((n)<<8))
#define TYPE(path) ((int)(path) & 0xFF)
#define NUM(path) ((uint)(path)>>8)

typedef struct Tab Tab;
struct Tab
{
	char *name;
	ulong mode;
};

static Tab tab[] =
{
	"/",		DMDIR | 0555,
	"clone",	0666,
	"n",		DMDIR | 0555,
	"ctl",		0666,
	"metainfo",	DMEXCL | DMAPPEND | 0666,
	"n",		DMDIR | 0555,
	"name",		0444,
	"data",		0444
};

static char Espec[] = "invalid attach specifier";
static char Eperm[] = "permission denied";
static char Ewalknondir[] = "walk in non-directory";
static char Enoent[] = "directory entry not found";
static char Eintr[] = "interrupted";
static char Enometainfo[] = "metainfo not available";
static char Emetainfoexist[] = "metainfo exists";
static char Emetainfofull[] = "metainfo full";
static char Emetainfoopen[] = "metainfo already open";

static long time0; /* init timestamp used for all files */

static void
fillstat(Dir *d, uvlong path)
{
	Tab *t;
	int type;
	char buf[32];

	memset(d, 0, sizeof *d);
	d->atime = time0;
	d->mtime = time0;
	d->uid = estrdup("bt");
	d->gid = estrdup("bt");
	d->qid.path = path;
	/* TODO length */

	type = TYPE(path);
	t = &tab[type];

	if(strcmp(t->name, "n") == 0){
		snprint(buf, sizeof buf, "%d", NUM(path));
		d->name = estrdup(buf);
	}
	else{
		d->name = estrdup(t->name);
	}
	d->qid.type = t->mode >> 24;
	d->mode = t->mode;
}

static void
fsattach(Req *r)
{
	if(r->ifcall.aname && r->ifcall.aname[0]){
		respond(r, Espec);
		return;
	}
	r->fid->qid.path = PATH(Qroot, 0);
	r->fid->qid.type = QTDIR;
	r->fid->qid.vers = 0;
	r->ofcall.qid = r->fid->qid;
	respond(r, nil);
}

static void
fsopen(Req *r)
{
	/* Convert OREAD-style mode to rwx-style mode. */
	int need[] = { 4, 2, 6, 1 };
	ulong path;
	Tab *te;
	Torrent *t;
	int n;

	/* Permission checking. */
	path = r->fid->qid.path;
	te = &tab[TYPE(path)];
	n = need[r->ifcall.mode & 3];
	if((n & te->mode) != n){
		respond(r, Eperm);
		return;
	}

	switch(TYPE(path)){
	case Qroot:
		break;

	case Qclone:
		t = torrentalloc();
		incref(t);
		r->fid->qid.path = PATH(Qctl, t->n);
		r->ofcall.qid.path = r->fid->qid.path;
		break;

	case Qtorrent:
	case Qctl:
	case Qmetainfo:
		n = NUM(path);
		assert(n < ntorrents && torrents[n]);
		t = torrents[n];
		if(TYPE(path) == Qmetainfo){
			if(t->metainfowrite){
				respond(r, Emetainfoopen);
				return;
			}
			t->metainfowrite = s_new();
		}
		incref(torrents[n]);
		break;
	
	default:
		respond(r, "fsopen not implemented\n");
		return;
	}

	respond(r, nil);
}

static int
rootgen(int i, Dir *dir, void*)
{
	/* Skip root directory entry. */
	i++;

	if(i < Qtorrent){
		fillstat(dir, PATH(i, 0));
		return 0;
	}

	/* Torrent directories. */
	i -= Qtorrent;
	if(i < ntorrents){
		fillstat(dir, PATH(Qtorrent, i));
		return 0;
	}
	return -1;
}

static int
torrentgen(int i, Dir *dir, void *aux)
{
	Torrent *t;
	t = aux;

	/* Skip torrent directory entry. */
	i += Qtorrent + 1;

	if(i < Qfile){
		fillstat(dir, PATH(i, t->n));
		return 0;
	}

	/* TODO File directories. */
	return -1;
}

static void
ctlread(Req *r, ulong path)
{
	char buf[32];
	sprint(buf, "%uX\n", NUM(path));
	readstr(r, buf);
}

static char*
metainforead(Req *r, ulong path)
{
	Torrent *t;
	char *mi;
	t = torrents[NUM(path)];
	/* TODO */
/*	if(!t->metainfo)
		return Enometainfo;
	mi = bencprint(t->metainfo);
	readstr(r, mi);
	free(mi); */
	return nil;
}

static void
fsread(Req *r)
{
	char *error;
	ulong path;

	error = nil;
	path = r->fid->qid.path;
	switch(TYPE(path)){
	case Qroot:
		dirread9p(r, rootgen, nil);
		break;

	case Qtorrent:
		dirread9p(r, torrentgen, torrents[NUM(path)]);
		break;
	
	case Qctl:
		ctlread(r, path);
		break;

	case Qmetainfo:
		error = metainforead(r, path);
		break;
	
	default:
		error = "fsread not implemented";
	}
	respond(r, error);
}

static char*
metainfowrite(ulong path, char *data, ulong count)
{
	Torrent *t;
	t = torrents[NUM(path)];
/*	if(t->metainfo)
		return Emetainfoexist; */
	if(s_len(t->metainfowrite) >= 8 * 1024 * 1024)
		return Emetainfofull;
	s_memappend(t->metainfowrite, data, count);
	return nil;
}

static void
fswrite(Req *r)
{
	char *error;
	ulong path;

	path = r->fid->qid.path;
	switch(TYPE(path)){
	case Qmetainfo:
		error = metainfowrite(path, r->ifcall.data, r->ifcall.count);
		if(!error)
			r->ofcall.count = r->ifcall.count;
		break;
	default:
		error = "fswrite not implemented";
		break;
	}
	respond(r, error);
}

static void
fsstat(Req *r)
{
	fillstat(&r->d, r->fid->qid.path);
	respond(r, nil);
}

static char*
fswalk1(Fid *fid, char *name, Qid *qid)
{
	ulong path;
	char *end;
	long n;
	int i;

	path = fid->qid.path;
	if(!(fid->qid.type & QTDIR))
		return Ewalknondir;

	if(strcmp(name, "..") == 0){
		qid->path = PATH(Qroot, 0);
		qid->type = tab[Qroot].mode >> 24;
		return nil;
	}

	/* Start at first file. */
	i = TYPE(path) + 1;

	for(; i < nelem(tab); i++){
		if(i == Qtorrent){
			end = nil;
			n = strtol(name, &end, 10);
			if(!end || end[0] != '\0')
				break;
			if(n < mtorrents && torrents[n]){
				qid->path = PATH(i, n);
				qid->type = tab[i].mode >> 24;
				return nil;
			}
			break;
		}
		/* TODO else if(i == Qfile){ */
		else if(strcmp(name, tab[i].name) == 0){
			qid->path = PATH(i, NUM(path));
			qid->type = tab[i].mode >> 24;
			return nil;
		}
		if(tab[i].mode & DMDIR)
			break;
	}
	return Enoent;
}

static void
fsdestroyfid(Fid *fid)
{
	Torrent *t;
	Bencval *metainfo;
	ulong path;
	char *buf;
	long len;

	path = fid->qid.path;
	if(TYPE(path) < Qtorrent || fid->omode == -1)
		return;
	t = torrents[NUM(path)];

	/* Parse metainfo if write complete. */
	if(TYPE(path) == Qmetainfo){
/*		if(!t->metainfo){
			buf = s_to_c(t->metainfowrite);
			len = s_len(t->metainfowrite);
			t->metainfo = bencparse(buf, len);
		} */
		DBG("committing new torrent...\n");
		buf = s_to_c(t->metainfowrite);
		len = s_len(t->metainfowrite);
		metainfo = bencparse(buf, len);
		if(!metainfo)
			DBG("bencparse failed\n");
		else{
			if (torrentinit(t, metainfo))
				DBG("torrentinit succeeded\n");
			else
				DBG("torrentinit failed\n");
		}
		s_free(t->metainfowrite);
		t->metainfowrite = nil;
	}

	/* TODO */
	/* Free torrents that have not been initialized. */
/*	if(decref(t) == 0 && !t->metainfo){
		torrentfree(t);
	} */
}

static void
fsflush(Req *r)
{
	/* TODO */
	respond(r->oldreq, Eintr);
	respond(r, nil);
}

static void
fsthread(void*)
{
	Alt alts[4];
	Req *r;
	Fid *fid;
	void **args;

	threadsetname("fsthread");

	alts[0] = (Alt){creq, &r, CHANRCV, 0, 0};
	alts[1] = (Alt){cclunk, &fid, CHANRCV, 0, 0};
	alts[2] = (Alt){cwalk1, &args, CHANRCV, 0, 0};
	alts[3].op = CHANEND;

	for(;;){
		switch(alt(alts)){
		case 0:
			switch(r->ifcall.type){
			case Tattach:	fsattach(r); break;
			case Topen:	fsopen(r); break;
			case Tread:	fsread(r); break;
			case Twrite:	fswrite(r); break;
			case Tstat:	fsstat(r); break;
			case Tflush:	fsflush(r); break;
			default:
				respond(r, "unexpected fcall type in fsthread");
				break;
			}
			sendp(cwait, nil);
			break;
		case 1:
			fsdestroyfid(fid);
			sendp(cwait, nil);
			break;
		case 2:
			sendp(cwalk1ret, fswalk1(args[0], args[1], args[2]));
			break;
		default:
			sysfatal("alt failed");
		}
	}
}

void
fsthreadinit(void)
{
	time0 = time(nil);
	creq = chancreate(sizeof(Req*), 0);
	cclunk = chancreate(sizeof(Fid*), 0);
	cwait = chancreate(sizeof(void*), 0);
	cwalk1 = chancreate(sizeof(void*), 0);
	cwalk1ret = chancreate(sizeof(char*), 0);
	procrfork(fsthread, nil, Stacksize, RFNAMEG);
}
