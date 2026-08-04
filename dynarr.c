#include <stdlib.h>
#include <string.h>

#include "dynarr.h"

_Bool
da_append(struct dynarr *d, void const *e)
{
	if (!d || !d->elemsize) return 0;
	if (d->length == d->capacity) {
		int cap = d->capacity? d->capacity : 1;
		void *t = realloc(d->content, 2*cap*d->elemsize);
		if (!t) return 0;
		d->content = t;
		d->capacity = 2*cap;
	}
	memcpy(da_get(*d, d->length++), e, d->elemsize);
	return 1;
}

void
da_free(struct dynarr d)
{
	free(d.content);
}

void *
da_get(struct dynarr d, int i)
{
	if (i < 0) return NULL;
	if (i >= d.length) return NULL;
	return (void*)((char*)d.content + i*d.elemsize);
}

struct dynarr
da_new(int elemsize)
{
	return (struct dynarr){NULL, elemsize, 0, 0};
}
