#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "node.h"
#include "dynarr.h"

struct dynarr apn_arena;
struct dynarr apn_carena;

static struct node *
get(int i)
{
	if (!i) return NULL;
	return da_get(apn_arena, i);
}

int
apn_arg(struct node const *node, int i)
{
	if (i < 0 || i >= apn_arglen(node)) return 0;
	while (--i >= 0) node = (struct node *)da_get(apn_arena, node->a.next);
	return node->b.arg;
}

int
apn_arglen(struct node const *node)
{
	if (!node) return 0;
	if (node->type != APN_ARGLIST) return -1;
	int n = apn_arglen(get(node->a.next));
	if (n < 0) return n; /* propagate failure */
	return 1 + n;
}

int
apn_fillmargs(int body, int arglist)
{
	if (!body) return 0;
	if (get(body)->type == APN_MARG) {
		int arg = apn_arg(get(arglist), get(body)->c.ivalue - 1);
		if (!arg) {
			fprintf(stderr,
			        "bad macro parameter #%lld\n",
			        get(body)->c.ivalue);
			return -1;
		}
		return arg;
	}
	int n1;
	int n2;
	int n3;
	int n4;
	int x;
	struct node nbody = *get(body);
	switch (get(body)->type) {
	case APN_NONE:
	case APN_VALUE:
	case APN_MARG:
	case APN_SYMBOL:
	case APN_ARG_REG:
	case APN_ARG_STRING:
		break;
	case APN_FUNCALL:
		n1 = apn_fillmargs(get(body)->b.arglist, arglist);
		if (n1 < 0) return -1;
		if (n1 != get(body)->b.arglist) {
			nbody.b.arglist = n1;
			x = apn_retain(&nbody);
			return x? x : -1;
		}
		break;
	case APN_PRODUCT:
	case APN_QUOTIENT:
	case APN_REMAINDER:
	case APN_SUM:
	case APN_DIFFERENCE:
	case APN_BITSHL:
	case APN_BITSHR:
	case APN_BITAND:
	case APN_BITOR:
	case APN_BITXOR:
	case APN_LESS:
	case APN_LESSEQ:
	case APN_EQUAL:
	case APN_UNEQUAL:
	case APN_GREATEREQ:
	case APN_GREATER:
		n1 = apn_fillmargs(get(body)->a.left, arglist);
		n2 = apn_fillmargs(get(body)->b.right, arglist);
		if (n1 < 0 || n2 < 0) return -1;
		if (n1 != get(body)->a.left
		 || n2 != get(body)->b.right) {
			nbody.a.left = n1;
			nbody.b.right = n2;
			x = apn_retain(&nbody);
			return x? x : -1;
		}
		break;
	case APN_ARG_MEM:
		n1 = apn_fillmargs(get(body)->b.off, arglist);
		if (n1 < 0) return -1;
		if (n1 != get(body)->b.off) {
			nbody.b.off = n1;
			x = apn_retain(&nbody);
			return x? x : -1;
		}
		break;
	case APN_ARG_EXPR:
		n1 = apn_fillmargs(get(body)->b.evalue, arglist);
		if (n1 < 0) return -1;
		if (n1 != get(body)->b.evalue) {
			nbody.b.evalue = n1;
			x = apn_retain(&nbody);
			return x? x : -1;
		}
		break;
	case APN_ARGLIST:
		n1 = apn_fillmargs(get(body)->b.arg, arglist);
		n2 = apn_fillmargs(get(body)->a.next, arglist);
		if (n1 < 0 || n2 < 0) return -1;
		if (n1 != get(body)->b.arg
		 || n2 != get(body)->a.next) {
			nbody.b.arg = n1;
			nbody.a.next = n2;
			x = apn_retain(&nbody);
			return x? x : -1;
		}
		break;
	case APN_LABEL:
		n1 = apn_fillmargs(get(body)->a.next, arglist);
		if (n1 < 0) return -1;
		if (n1 != get(body)->a.next) {
			nbody.a.next = n1;
			x = apn_retain(&nbody);
			return x? x : -1;
		}
		break;
	case APN_INSTRUCTION:
		n1 = apn_fillmargs(get(body)->a.next, arglist);
		n2 = apn_fillmargs(get(body)->b.arglist, arglist);
		if (n1 < 0 || n2 < 0) return -1;
		if (n1 != get(body)->a.next
		 || n2 != get(body)->b.arglist) {
			nbody.a.next = n1;
			nbody.b.arglist = n2;
			x = apn_retain(&nbody);
			return x? x : -1;
		}
		break;
	case APN_SYMBIND:
		n1 = apn_fillmargs(get(body)->a.next, arglist);
		n2 = apn_fillmargs(get(body)->b.evalue, arglist);
		if (n1 < 0 || n2 < 0) return -1;
		if (n1 != get(body)->a.next
		 || n2 != get(body)->b.evalue) {
			nbody.a.next = n1;
			nbody.b.evalue = n2;
			x = apn_retain(&nbody);
			return x? x : -1;
		}
		break;
	case APN_CONDITIONAL:
		n1 = apn_fillmargs(get(body)->b.evalue, arglist);
		n2 = apn_fillmargs(get(body)->c.cond.tpath, arglist);
		n3 = apn_fillmargs(get(body)->c.cond.fpath, arglist);
		n4 = apn_fillmargs(get(body)->a.next, arglist);
		if (n1 < 0 || n2 < 0 || n3 < 0 || n4 < 0) return -1;
		if (n1 != get(body)->b.evalue
		 || n2 != get(body)->c.cond.tpath
		 || n3 != get(body)->c.cond.fpath
		 || n4 != get(body)->a.next) {
			nbody.b.evalue = n1;
			nbody.c.cond.tpath = n2;
			nbody.c.cond.fpath = n3;
			nbody.a.next = n4;
			x = apn_retain(&nbody);
			return x? x : -1;
		}
		break;
	case APN_MACRO:
		n1 = apn_fillmargs(get(body)->b.stream, arglist);
		n2 = apn_fillmargs(get(body)->a.next, arglist);
		if (n1 < 0 || n2 < 0) return -1;
		if (n1 != get(body)->b.stream
		 || n2 != get(body)->a.next) {
			nbody.b.stream = n1;
			nbody.a.next = n2;
			x = apn_retain(&nbody);
			return x? x : -1;
		}
		break;
	default:
		break;
	}
	return body;
}

void
apn_free(void)
{
	da_free(apn_arena);
	da_free(apn_carena);
	apn_arena.content = NULL;
	apn_arena.length = apn_arena.capacity = 0;
	apn_carena.content = NULL;
	apn_carena.length = apn_carena.capacity = 0;
}

int
apn_retain(struct node const *n)
{
	if (!apn_arena.content) {
		struct node nullnode = {0};
		apn_arena = da_new(sizeof(struct node));
		da_append(&apn_arena, &nullnode);
	}
	if (!n) return 0;
	int out = apn_arena.length;
	if (!da_append(&apn_arena, n)) return 0;
	return out;
}

int
apn_retainstr(char const *s)
{
	if (!apn_carena.content) {
		apn_carena = da_new(1);
		da_append(&apn_carena, "\0");
	}
	if (!s) return 0;
	int out = apn_carena.length;
	while (*s) if (!da_append(&apn_carena, s++)) return 0;
	if (!da_append(&apn_carena, "\0")) return 0;
	return out;
}
