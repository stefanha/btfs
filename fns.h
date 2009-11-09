#define DBG print

/* util.c */
void *emalloc(ulong size);
void *ezalloc(ulong size);
void *erealloc(void *p, ulong size);
char *estrdup(char *s);
String *readfile(char *path);
int urlopen(char *url, char *postbody);
void dofmtinstall(void);

/* benc.c */
Bencval *bencalloc(int type);
void bencfree(Bencval *benc);
Bencval *bencparse(char *s, long len);
void bencprint(Bencval *benc, char **s, long *len);
Bencval *benclookup(Bencval *dict, char *key, int type);
int benclistlen(Bencval *list);

/* torrent.c */
int loadtorrents(void);
Torrent *torrentalloc(void);
void torrentfree(Torrent *t);
int torrentinit(Torrent *t, Bencval *metainfo);

/* fsthread.c */
void fsthreadinit(void);
