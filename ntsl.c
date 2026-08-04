#include <stdlib.h>
#include <string.h>

#include "ntsl.h"

struct ntsl *
ntsladd(struct ntsl *p, char const *s)
{
	if (!s) return NULL;
	struct ntsl *x = malloc(sizeof(*x));
	if (!x) return NULL;
	x->length = strlen(s);
	x->content = malloc(x->length + 1);
	if (!x->content) { free(x); return NULL; }
	strncpy(x->content, s, x->length + 1);
	x->next = p;
	return x;
}

void
ntslappend(struct ntsl **p, char const *s)
{
	if (!p) return;
	while (*p) p = &(*p)->next;
	*p = ntsladd(*p, s);
}

int
ntslfind(struct ntsl const *p, char const *s)
{
	int len = strlen(s);
	for (int i = 0; p; p = p->next, i++) {
		if (len != p->length) continue;
		if (strncmp(p->content, s, p->length + 1) == 0) return i;
	}
	return -1;
}

void
ntslfree(struct ntsl *p)
{
	if (!p) return;
	ntslfree(p->next);
	free(p->content);
	free(p);
}

char *
ntslget(struct ntsl const *p, int i)
{
	for (; p && i >= 0; i--) if (i == 0) return p->content;
	return NULL;
}
