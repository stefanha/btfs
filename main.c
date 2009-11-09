#include <u.h>
#include <libc.h>
#include "btfs.h"

char *webmountpt = "/mnt/web";
char *torrentdir = "/tmp/btfs";

static void
usage(void)
{
	fprint(2, "usage: btfs\n");
	threadexitsall("usage");
}

void
threadmain(int argc, char *argv[])
{
	ARGBEGIN{
	case 'D':
		chatty9p++;
		break;
	default:
		usage();
	}ARGEND

	if(argc != 0)
		usage();

	dofmtinstall();
	fsthreadinit();

	if(!loadtorrents())
		threadexits("loadtorrents failed");

	threadpostmountsrv(&fs, "bt", "/mnt/bt", MREPL);
	threadexits(nil);
}
