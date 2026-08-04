#include <stdio.h>
#include "node.h"
#include "dynarr.h"

static struct node *get(int);
static void innerprint(struct node const * node);
static void subprint(struct node const * parent,
                     struct node const * child,
                     struct node const * fnodey);
static void header(struct node const * node, char const * name);
static void rsame(struct node const * parent, struct node const * child);
static void list_subtree(struct node const * node, _Bool precomma);

static struct node *
get(int i)
{
	if (!i) return NULL;
	return da_get(apn_arena, i);
}

void
apn_print(struct node const * node)
{
	puts("digraph {");
	puts("\tnode [shape=none]");
	if (node) { innerprint(node); }
	puts("}");
}

static void
header(struct node const * node, char const * name)
{
	if (!node) return;
	printf("\t<TR><TD><B>%s @ %d</B></TD></TR>\n", name, node->line);
}

static void
rsame(struct node const * parent, struct node const * child)
{
	if (!parent || !child) return;
	printf("\t{rank=same L%p L%p;}\n", (void*)parent, (void*)child);
}

static void
subprint(struct node const * parent, struct node const * child,
         struct node const * fnodey)
{
	if (!child) return;
	innerprint(child);
	if (fnodey) {
		printf("{");
		list_subtree(child, 0);
		printf("} -> L%p [weight=0,style=invis]\n", (void*)fnodey);
	}
	printf("\tL%p -> L%p\n", (void*)parent, (void*)child);
}

static void
innerprint(struct node const * node)
{
	#define CSH(x) case x: header(node, #x);
	if (!node) return;
	printf("L%p [margin=0,label=<\n", (void*)node);
	/* label box */
	puts("\t<TABLE>");
	switch (node->type) {
	CSH(APN_NONE) break;
	CSH(APN_VALUE)
		printf("\t<TR><TD>%llu</TD></TR>", node->c.ivalue);
		break;
	CSH(APN_MARG)
		printf("\t<TR><TD>#%llu</TD></TR>", node->c.ivalue);
		break;
	CSH(APN_SYMBOL)
		printf("\t<TR><TD>%s</TD></TR>\n", node->c.svalue.content);
		break;
	CSH(APN_FUNCALL)
		printf("\t<TR><TD>%s</TD></TR>", node->c.svalue.content);
		break;
	CSH(APN_PRODUCT) break;
	CSH(APN_QUOTIENT) break;
	CSH(APN_REMAINDER) break;
	CSH(APN_SUM) break;
	CSH(APN_DIFFERENCE) break;
	CSH(APN_BITSHL) break;
	CSH(APN_BITSHR) break;
	CSH(APN_BITAND) break;
	CSH(APN_BITOR) break;
	CSH(APN_BITXOR) break;
	CSH(APN_LESS) break;
	CSH(APN_LESSEQ) break;
	CSH(APN_EQUAL) break;
	CSH(APN_UNEQUAL) break;
	CSH(APN_GREATEREQ) break;
	CSH(APN_GREATER) break;
	CSH(APN_ARG_MEM)
		printf("\t<TR><TD>R%d</TD></TR>",node->c.reg);
		break;
	CSH(APN_ARG_REG)
		printf("\t<TR><TD>R%d</TD></TR>",node->c.reg);
		break;
	CSH(APN_ARG_EXPR) break;
	CSH(APN_ARG_STRING)
		printf("\t<TR><TD>%s</TD></TR>\n",
		       (char *)da_get(apn_carena, node->c.ntstring));
		break;
	CSH(APN_ARGLIST) break;
	CSH(APN_LABEL)
		printf("\t<TR><TD>%s</TD></TR>\n", node->c.svalue.content);
		break;
	CSH(APN_INSTRUCTION)
		printf("\t<TR><TD>%s</TD></TR>\n", node->c.svalue.content);
		break;
	CSH(APN_SYMBIND)
		printf("\t<TR><TD>%s</TD></TR>\n", node->c.svalue.content);
		break;
	CSH(APN_CONDITIONAL) break;
	CSH(APN_MACRO) break;
	default:
		header(node, "UNKNOWN");
		break;
	}
	puts("\t</TABLE>>]");

	/* subprints and connections */
	switch (node->type) {
	case APN_NONE:
	case APN_VALUE:
	case APN_SYMBOL:
	case APN_ARG_REG:
	case APN_ARG_STRING:
		break;
	case APN_FUNCALL:
		subprint(node, get(node->b.arglist), NULL);
		rsame(node, get(node->b.arglist));
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
		subprint(node, get(node->a.left), NULL);
		subprint(node, get(node->b.right), NULL);
		break;
	case APN_ARG_MEM:
		subprint(node, get(node->b.off), NULL);
		break;
	case APN_ARG_EXPR:
		subprint(node, get(node->b.evalue), NULL);
		break;
	case APN_ARGLIST:
		subprint(node, get(node->a.next), NULL);
		rsame(node, get(node->a.next));
		subprint(node, get(node->b.arg), NULL);
		break;
	case APN_LABEL:
		subprint(node, get(node->a.next), NULL);
		break;
	case APN_INSTRUCTION:
		subprint(node, get(node->b.arglist), get(node->a.next));
		rsame(node, get(node->b.arglist));
		subprint(node, get(node->a.next), NULL);
		break;
	case APN_SYMBIND:
		subprint(node, get(node->b.evalue), get(node->a.next));
		rsame(node, get(node->b.evalue));
		subprint(node, get(node->a.next), NULL);
		break;
	case APN_CONDITIONAL:
		subprint(node, get(node->b.evalue), get(node->a.next));
		subprint(node, get(node->c.cond.tpath),
		         node->c.cond.fpath? get(node->c.cond.fpath)
		                           : get(node->a.next));
		rsame(node, get(node->c.cond.tpath));
		subprint(node, get(node->c.cond.fpath), get(node->a.next));
		subprint(node, get(node->a.next), NULL);
		break;
	case APN_MACRO:
		subprint(node, get(node->b.stream), get(node->a.next));
		rsame(node, get(node->b.stream));
		subprint(node, get(node->a.next), NULL);
		break;
	default:
		break;
	}
}

static void
list_subtree(struct node const * node, _Bool precomma)
{
	if (!node) return;
	if (precomma) printf(", ");
	printf("L%p", (void*)node);
	switch (node->type) {
	case APN_NONE:
	case APN_VALUE:
	case APN_SYMBOL:
	case APN_ARG_REG:
	case APN_ARG_STRING:
		break;
	case APN_FUNCALL:
		list_subtree(get(node->b.arglist), 1);
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
		list_subtree(get(node->a.left), 1);
		list_subtree(get(node->b.right), 1);
		break;
	case APN_ARG_MEM:
		list_subtree(get(node->b.off), 1);
		break;
	case APN_ARG_EXPR:
		list_subtree(get(node->b.evalue), 1);
		break;
	case APN_ARGLIST:
		list_subtree(get(node->b.arg), 1);
		list_subtree(get(node->a.next), 1);
		break;
	case APN_LABEL:
		list_subtree(get(node->a.next), 1);
		break;
	case APN_INSTRUCTION:
		list_subtree(get(node->b.arglist), 1);
		list_subtree(get(node->a.next), 1);
		break;
	case APN_SYMBIND:
		list_subtree(get(node->b.evalue), 1);
		list_subtree(get(node->a.next), 1);
		break;
	case APN_CONDITIONAL:
		list_subtree(get(node->b.evalue), 1);
		list_subtree(get(node->c.cond.tpath), 1);
		list_subtree(get(node->c.cond.fpath), 1);
		break;
	case APN_MACRO:
		list_subtree(get(node->b.stream), 1);
		break;
	default:
		break;
	}
}
