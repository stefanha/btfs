#include <u.h>
#include <libc.h>
#include "btfs.h"

enum
{
	Tincr = 16
};

Torrent **torrents;
int ntorrents;
int mtorrents;

static int
newidx(void)
{
	int i;
	for(i = 0; i < mtorrents; i++)
		if(!torrents[i])
			break;
	if(i == mtorrents){
		mtorrents += Tincr;
		torrents = erealloc(torrents, sizeof(torrents[0]) * mtorrents);
		memset(&torrents[mtorrents - Tincr], 0, sizeof(torrents[0]) * Tincr);
	}
	ntorrents++;
	return i;
}

static int
tryload(char *infohashstr)
{
	Torrent *t;
	Bencval *metainfo;
	String *sp;
	char *path;

	path = smprint("%s/%s/metainfo", torrentdir, infohashstr);
	if(!path)
		return 0;
	sp = readfile(path);
	free(path);
	if(!sp)
		return 0;
	metainfo = bencparse(s_to_c(sp), s_len(sp));
	s_free(sp);
	if(!metainfo)
		return 0;

	t = torrentalloc();
	if(!torrentinit(t, metainfo)){
		torrentfree(t);
		return 0;
	}
	return 1;
}

int
loadtorrents(void)
{
	int fd;
	Dir *d;
	long n, i;

	DBG("loading torrents...\n");
	fd = open(torrentdir, OREAD);
	if(fd < 0)
		return 0;
	n = dirreadall(fd, &d);
	close(fd);
	if(n < 0)
		return 0;
	for(i = 0; i < n; i++){
		if(strlen(d[i].name) != sizeof(SHA1))
			continue;
		DBG("\t%s...", d[i].name);
		if(!tryload(d[i].name)){
			DBG("failed\n");
			free(d);
			return 0;
		}
		DBG("done\n");
	}
	free(d);
	DBG("done\n");
	return 1;
}

Torrent*
torrentalloc(void)
{
	Torrent *t;
	t = ezalloc(sizeof *t);
	t->n = newidx();
	torrents[t->n] = t;
	return t;
}

void
torrentfree(Torrent *t)
{
	if(!t)
		return;
	s_free(t->metainfowrite);
	torrents[t->n] = nil;
	free(t);
	ntorrents--;
}

static int
calcinfohash(Torrent *t, Bencval *metainfo)
{
	Bencval *b;
	char *s;
	long len;

	b = benclookup(metainfo, "info", Bdict);
	if(!b)
		return 0;
	bencprint(b, &s, &len);
	sha1((uchar*)s, len, t->infohash, nil);
	free(s);
	return 1;
}

static int
loadpieces(Torrent *t, Bencval *metainfo)
{
	Bencval *b;

	b = benclookup(metainfo, "pieces", Bstring);
	if(!b || b->len % (sizeof(SHA1) * 2))
		return 0;
	t->npieces = b->len / (sizeof(SHA1) * 2);
	if(t->npieces <= 0)
		return 0;
	t->pieces = emalloc(b->len);
	memcpy(t->pieces, b->s, b->len);

	b = benclookup(metainfo, "piece length", Bint);
	if(!b || b->i <= 0)
		return 0;
	t->piecelength = b->i;
	if(t->piecelength <= 0)
		return 0;
	return 1;
}

static int
loadfile(Bencval *dict, FileInfo *fi, int multifile)
{
	Bencval *length;
	Bencval *name;
	String *strname;

	length = benclookup(dict, "length", Bint);
	if(multifile)
		name = benclookup(dict, "path", Blist);
	else
		name = benclookup(dict, "name", Bstring);
	if(!length || !name || length->i < 0)
		return 0;

	if(multifile){
		strname = s_new();
		if(!strname)
			return 0;
		for(name = name->head; name; name = name->next){
			if(name->type != Bstring)
				return 0;

			/* Unsafe file names */
			if(strchr(name->s, '/') || strcmp(name->s, "..") == 0)
				return 0;

			if(s_len(strname) > 0)
				s_putc(strname, '/');
			s_append(strname, name->s);
		}
	}
	else
		strname = s_copy(name->s);
	if(!strname)
		return 0;

	fi->length = length->i;
	fi->name = estrdup(s_to_c(strname));
	s_free(strname);
	return 1;
}

/* TODO free resources on error returns */

static int
loadfiles(Torrent *t, Bencval *metainfo)
{
	Bencval *b;
	int i;

	b = benclookup(metainfo, "files", Blist);
	if(b){	/* multifile */
		t->nfiles = benclistlen(b);
		t->files = emalloc(sizeof(t->files[0]) * t->nfiles);
		for(i = 0, b = b->head; b; i++, b = b->next)
			if(!loadfile(b, &t->files[i], 1))
				return 0;
	}
	else{	/* single file */
		t->nfiles = 1;
		t->files = emalloc(sizeof(t->files[0]));
		if(!loadfile(metainfo, &t->files[0], 0))
			return 0;
	}
	return 1;
}

static int
loadannouncelist(Torrent *t, Bencval *metainfo)
{
	return 0;
}

static int
loadmetainfo(Torrent *t, Bencval *metainfo)
{
	return calcinfohash(t, metainfo) &&
		loadpieces(t, metainfo) &&
		loadfiles(t, metainfo) &&
		loadannouncelist(t, metainfo);
}

static int
createemptyfile(char *path, vlong len)
{
	int fd;

	fd = create(path, OWRITE, 0600);
	if(fd < 0)
		return 0;
	if(seek(fd, len - 1, 0) != len - 1)
		return 0;
	if(write(fd, "\x0", 1) != 1)
		return 0;
	if(close(fd) < 0)
		return 0;
	return 1;
}

static int
createdir(Torrent *t, Bencval *metainfo)
{
	char *path;
	char *s;
	long len;
	int fd;

	/* The directory. */
	path = smprint("%s/%M", torrentdir, t->infohash);
	if(access(path, AEXIST) == 0)
		goto out;
	DBG("creating %s...\n", path);
	fd = create(path, OREAD, DMDIR | 0700);
	if(fd < 0)
		goto err;
	close(fd);
	free(path);

	/* metainfo */
	path = smprint("%s/%M/metainfo", torrentdir, t->infohash);
	fd = create(path, OWRITE, 0600);
	if(fd < 0)
		goto errdir;
	bencprint(metainfo, &s, &len);
	if(write(fd, s, len) != len){
		free(s);
		goto errdir;
	}
	free(s);
	close(fd);
	free(path);

	/* data */
/*	path = smprint("%s/%M/data", torrentdir, t->infohash);
	if (createemptyfile(path, ...Len...)) */

out:
	free(path);
	return 1;

errdir:
	free(path);
	path = smprint("%s/%M", torrentdir, t->infohash);
	remove(path);
err:
	free(path);
	return 0;
}

int
torrentinit(Torrent *t, Bencval *metainfo)
{
	if(!loadmetainfo(t, metainfo))
		return 0;
	if(!createdir(t, metainfo))
		return 0;
	/* TODO check pieces */
	return 1;
}
