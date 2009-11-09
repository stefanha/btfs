#include <u.h>
#include <libc.h>
#include "btfs.h"

void*
emalloc(ulong size)
{
	void *p;
	p = malloc(size);
	if(!p)
		sysfatal("emalloc");
	return p;
}

void*
ezalloc(ulong size)
{
	void *p;
	p = mallocz(size, 1);
	if(!p)
		sysfatal("ezalloc");
	return p;
}

void*
erealloc(void *p, ulong size)
{
	p = realloc(p, size);
	if(!p)
		sysfatal("erealloc");
	return p;
}

char*
estrdup(char *s)
{
	char *s1;
	s1 = strdup(s);
	if(!s1)
		sysfatal("estrdup");
	return s1;
}

int
urlopen(char *url, char *postbody){
	char buf[1024];
	long n;
	int cfd;
	int fd;
	int conn;

	snprint(buf, sizeof buf, "%s/clone", webmountpt);
	cfd = open(buf, ORDWR);
	if(cfd < 0)
		sysfatal("webfs open clone");

	n = read(cfd, buf, sizeof buf - 1);
	if(n <= 0)
		sysfatal("webfs read clone");
	buf[n] = '\0';
	conn = atoi(buf);

	snprint(buf, sizeof buf, "url %s", url);
	if(write(cfd, buf, strlen(buf)) < 0)
		sysfatal("webfs ctl write url");

	if(postbody){
		snprint(buf, sizeof buf, "%s/%d/postbody", webmountpt, conn);
		fd = open(buf, OWRITE);
		if(fd < 0)
			sysfatal("webfs open postbody");
		if(write(fd, postbody, strlen(postbody)) < 0)
			sysfatal("webfs write postbody");
		close(fd);
	}

	snprint(buf, sizeof buf, "%s/%d/body", webmountpt, conn);
	fd = open(buf, OREAD);
	if(fd < 0)
		sysfatal("webfs open body");
	close(cfd);
	return fd;
}

String*
readfile(char *path)
{
	String *sp;
	char *buf;
	int fd;
	long n;

	fd = open(path, OREAD);
	if(fd < 0)
		return nil;
	sp = s_new();
	buf = emalloc(8192);
	while((n = read(fd, buf, 8192)) > 0){
		s_memappend(sp, buf, n);
	}
	s_terminate(sp);
	free(buf);
	close(fd);
	if(n != 0){
		s_free(sp);
		return nil;
	}
	return sp;
}

static int
digestfmt(Fmt *fmt)
{
	char buf[SHA1dlen * 2 + 1];
	uchar *p;
	int i;

	p = va_arg(fmt->args, uchar*);
	for(i = 0; i < SHA1dlen; i++)
		sprint(buf + 2 * i, "%.2ux", p[i]);
	return fmtstrcpy(fmt, buf);
}

void
dofmtinstall(void)
{
	fmtinstall('M', digestfmt);
}
