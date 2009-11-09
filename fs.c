/*
 * This proc is started by threadpostmountsrv() and simply forwards
 * calls to the fsthread.  This is done so that file system operations
 * happen in the main proc and not concurrently.
 */
#include <u.h>
#include <libc.h>
#include "btfs.h"

Channel *creq;	/* chan(Req*) */
Channel *cclunk;	/* chan(Fid*) */
Channel *cwait;	/* chan(void*) */
Channel *cwalk1;	/* chan(void*) */
Channel *cwalk1ret; /* chan(char*) */

static void
fsfwd(Req *r)
{
	sendp(creq, r);
	recvp(cwait);
}

static char*
fsfwdwalk1(Fid *fid, char *name, Qid *qid)
{
	void *args[] = {fid, name, qid};
	sendp(cwalk1, args);
	return recvp(cwalk1ret);
}

static void
fsfwddestroyfid(Fid *fid)
{
	sendp(cclunk, fid);
	recvp(cwait);
}

static void
fsend(Srv*)
{
	threadexitsall(nil);
}

Srv fs =
{
	.attach = fsfwd,
	.open = fsfwd,
	.read = fsfwd,
	.write = fsfwd,
	.stat = fsfwd,
	.walk1 = fsfwdwalk1,
	.destroyfid = fsfwddestroyfid,
	.flush = fsfwd,
	.end = fsend,
};
