#include <u.h>
#include <libc.h>
#include <ctype.h>
#include "btfs.h"

/* Useful for parsing */
typedef struct Stream Stream;
struct Stream
{
	long len; /* bytes left */
	char *s;  /* current pointer */
};

static
int sread(Stream *s, void *p, long len)
{
	if(!s || !p || len < 0)
		return -1;
	if(len > s->len){
		len = s->len;
	}
	memmove(p, s->s, len);
	s->len -= len;
	s->s += len;
	return len;
}

static
void srewind(Stream *s, long len)
{
	s->len += len;
	s->s -= len;
}

Bencval*
bencalloc(int type)
{
	Bencval *benc;
	benc = ezalloc(sizeof *benc);
	benc->type = type;
	return benc;
}

void
bencfree(Bencval *benc)
{
	if(!benc)
		return;
	if(benc->next){
		bencfree(benc->next);
	}
	switch(benc->type){
		case Bstring:
			free(benc->s);
			break;
		case Blist:
		case Bdict:
			bencfree(benc->head);
			break;
	}
	free(benc);
}

static vlong
parselong(Stream *s)
{
	int neg;
	char c;
	vlong i;

	if(sread(s, &c, 1) != 1)
		return 0;
	if(c == '-')
		neg = 1;
	else{
		neg = 0;
		srewind(s, 1);
	}
	i = 0;
	while(sread(s, &c, 1) == 1 && isdigit(c)){
		i = c - '0' + i * 10;
	}
	if(!isdigit(c))
		srewind(s, 1);
	return neg ? -i : i;
}

static Bencval*
bencparsestream(Stream *pstr)
{
	Bencval *benc;
	Bencval *elem;
	Bencval *lastelem;
	long nelems;
	char c;

	benc = nil;
	lastelem = nil;
	if(sread(pstr, &c, 1) != 1){
		print("sread failed (1)\n");
		return nil;
	}
	if(isdigit(c)){
		srewind(pstr, 1);
		benc = bencalloc(Bstring);
		benc->len = parselong(pstr);
		if(sread(pstr, &c, 1) != 1 || c != ':'){
			print("expected ':', got '%c'\n", c);
			bencfree(benc);
			return nil;
		}
		benc->s = emalloc(benc->len + 1);
		if(sread(pstr, benc->s, benc->len) != benc->len){
			print("sread failed (2)\n");
			bencfree(benc);
			return nil;
		}
		benc->s[benc->len] = '\0';
	}
	else if(c == 'i'){
		benc = bencalloc(Bint);
		benc->i = parselong(pstr);
		if(sread(pstr, &c, 1) != 1 || c != 'e'){
			print("expected 'e', got '%c'\n", c);
			bencfree(benc);
			return nil;
		}
	}
	else if(c == 'l' || c == 'd'){
		benc = bencalloc(c == 'l' ? Blist : Bdict);
		nelems = 0;
		while(sread(pstr, &c, 1) == 1 && c != 'e'){
			srewind(pstr, 1);
			elem = bencparsestream(pstr);
			if(!elem){
				print("bencparse failed\n");
				bencfree(benc);
				return nil;
			}
			if(lastelem)
				lastelem->next = elem;
			else
				benc->head = elem;
			lastelem = elem;
			if(benc->type == Bdict && nelems % 2 == 0){
				if(elem->type != Bstring){
					print("key not string\n");
					bencfree(benc);
					return nil;
				}
			}
			nelems++;
		}
		if(benc->type == Bdict && nelems % 2){
			print("odd number of elements in dict\n");
			bencfree(benc);
			return nil;
		}
	}
	return benc;
}

Bencval*
bencparse(char *s, long len)
{
	Stream str;
	str.s = s;
	str.len = len;
	return bencparsestream(&str);
}

static void
bencprintstring(Bencval *benc, String *sp)
{
	char buf[32];
	Bencval *elem;
	switch(benc->type){
	case Bstring:
		snprint(buf, sizeof buf, "%ld:", benc->len);
		s_append(sp, buf);
		s_memappend(sp, benc->s, benc->len);
		break;
	case Bint:
		snprint(buf, sizeof buf, "i%llde", benc->i);
		s_append(sp, buf);
		break;
	case Blist:
	case Bdict:
		s_putc(sp, benc->type == Blist ? 'l' : 'd');
		for(elem = benc->head; elem; elem = elem->next)
			bencprintstring(elem, sp);
		s_putc(sp, 'e');
		break;
	}
}

void
bencprint(Bencval *benc, char **s, long *len)
{
	String *sp;

	sp = s_new();
	bencprintstring(benc, sp);
	s_terminate(sp); /* not really necessary */
	*len = s_len(sp);

	/* Detach string buffer from String.  This is naughty since
	 * it relies on s_free() implementation details. */
	*s = sp->base;
	sp->base = sp->ptr = sp->end = nil;

	s_free(sp);
}

Bencval*
benclookup(Bencval *dict, char *key, int type)
{
	Bencval *item;
	long klen;

	if(!key || !dict || dict->type != Bdict)
		return nil;
	klen = strlen(key);
	for(item = dict->head; item; item = item->next->next){
		if (item->type != Bstring || !item->next)
			return nil;
		if(item->len == klen && !strcmp(item->s, key) &&
				(type == -1 || type == item->next->type))
			return item->next;
	}
	return nil;
}

int
benclistlen(Bencval *list)
{
	Bencval *item;
	int i;

	if(!list || list->type != Blist)
		return -1;
	i = 0;
	for(item = list->head; item; item = item->next)
		i++;
	return i;
}
