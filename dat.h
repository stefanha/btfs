enum{
	Stacksize = 8192,
};

enum
{
	Bstring,
	Bint,
	Blist,
	Bdict
};

typedef struct Bencval Bencval;
struct Bencval
{
	int type;
	union {
		/* String */
		struct {
			long len;
			char *s;
		};

		/* Integer */
		vlong i;

		/* List or dict */
		Bencval *head;
	};
	Bencval *next; /* for list or dict elements */
};

/* Hash */
typedef uchar SHA1[SHA1dlen];

/* Multi-tracker announce list */
typedef struct Tier Tier;
struct Tier
{
	char **urls;
	int nurls;
	Tier *next;
};

/* FileInfo in the torrent */
typedef struct FileInfo FileInfo;
struct FileInfo
{
	char *name;
	vlong length;
};

typedef struct Torrent Torrent;
struct Torrent
{
	Ref;	/* currently open fids */
	int n;	/* index in torrents array */
	String *metainfowrite; /* buffer for metainfo */

	SHA1 infohash;
	int npieces;
	SHA1 *pieces;
	vlong piecelength;
	int nfiles;
	FileInfo *files;
	Tier *announcelist;
};

extern char *webmountpt;
extern char *torrentdir;

extern Srv fs;
extern Channel *creq;	/* chan(Req*) */
extern Channel *cclunk;	/* chan(Fid*) */
extern Channel *cwait;	/* chan(void*) */
extern Channel *cwalk1;	/* chan(void*) */
extern Channel *cwalk1ret; /* chan(char*) */

extern Torrent **torrents;
extern int ntorrents;
extern int mtorrents;

#pragma	varargck	type	"M"	uchar*
