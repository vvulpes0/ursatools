#ifndef PARSE_H
#define PARSE_H
#include "common.h"


/**********************************************************************
 * Types
 **********************************************************************/

enum node_type {
	APN_NONE,
	// expression parts: values
	APN_VALUE,
	APN_SYMBOL,
	APN_FUNCALL,
	// multiplicative ops
	APN_PRODUCT,
	APN_QUOTIENT,
	APN_REMAINDER,
	// additive ops
	APN_SUM,
	APN_DIFFERENCE,
	// shifts
	APN_BITSHL,
	APN_BITSHR,
	// and-like ops
	APN_BITAND,
	// or-like ops
	APN_BITOR,
	APN_BITXOR,
	// comparison ops
	APN_LESS,
	APN_LESSEQ,
	APN_EQUAL,
	APN_UNEQUAL,
	APN_GREATEREQ,
	APN_GREATER,
	// done with expression parts
	APN_ARG_MEM,
	APN_ARG_REG,
	APN_ARG_EXPR,
	APN_ARG_STRING,
	APN_ARGLIST,
	APN_LABEL,
	APN_INSTRUCTION,
	APN_SYMBIND,
	APN_CONDITIONAL,
	APN_MACRO,
	APN_MARG,
};

/**
 * Node type for parsed program trees.
 * Anything in {@code a} or {@code b}, as well as anything in {@code c.cond},
 * is an index into {@code apn_arena}.
 * The value in {@code c.ntstring} indexes {@code apn_carena}.
 * Other values have evident meanings.
 * Members are annotated by the node types that use them.
 */
struct node {
	int type;
	char const *fname;
	int line;

	/* ONLY node references in this union; just different names */
	/* sometimes unused but nice to have for consistency        */
	union {
		int left; /* binary operator */
		int next; /* APN_ARGLIST
		           | APN_LABEL
		           | APN_INSTRUCTION
		           | APN_SYMBIND 
		           | APN_CONDITIONAL */
	} a;

	/* ONLY node references in this union; just different names */
	/* sometimes unused but nice to have for consistency        */
	union {
		int arg;     /* APN_ARGLIST */
		int arglist; /* APN_FUNCALL | APN_INSTRUCTION */
		int evalue;  /* APN_ARG_EXPR
		              | APN_SYMBIND
		              | APN_CONDITIONAL */
		int off;     /* APN_ARG_MEM */
		int right;   /* binary operator */
		int stream;  /* APN_MACRO */
	} b;

	/* non-node portions of things that need other stuff */
	union {
		struct { int tpath; int fpath; } cond;
			/* ^ APN_CONDITIONAL */
		struct string svalue; /* APN_FUNCALL
		                       | APN_INSTRUCTION
		                       | APN_LABEL
		                       | APN_SYMBIND
		                       | APN_SYMBOL */
		long long ivalue;     /* APN_VALUE */
		int ntstring;         /* APN_ARG_STRING */
		unsigned char reg;    /* APN_ARG_REG */
	} c;
};


/**********************************************************************
 * Variables
 **********************************************************************/

/**
 * Arena for node allocation.
 */
extern struct dynarr apn_arena;

/**
 * Arena for null-terminated string pool.
 */
extern struct dynarr apn_carena;


/**********************************************************************
 * Destructors
 **********************************************************************/

/**
 * Releases the two arenas {@code apn_arena} and {@code apn_carena}.
 */
void apn_free(void);


/**********************************************************************
 * Functions
 **********************************************************************/

/**
 * Returns a reference to the given index of this argument list.
 * @param argl  this node, which must have type {@code APN_ARGLIST}
 * @param i     the target index
 * @return the indexed node's location; 0 if out of range
 */
int apn_arg(struct node const *argl, int i);

/**
 * Returns the number of items in this argument list.
 * @param argl  this node, which must have type {@code APN_ARGLIST}
 * @return the size of this argument list, in items
 */
int apn_arglen(struct node const *argl);

/**
 * Instantiates macro parameters in this subtree.
 * If there are no parameters to instantiate, this subtree is returned.
 * Otherwise, a minimal copy is created, retained, and returned.
 * @param body  the index of this subtree in {@code apn_arena}
 * @param argl  the index of the macro argument list in {@code apn_arena}
 * @return the index of the new subtree root
 */
int apn_fillmargs(int body, int argl);

/**
 * Emits this subtree in GraphViz dot format.
 * @param tree  this subtree
 */
void apn_print(struct node const *tree);

/**
 * Places a copy of this node into {@code apn_arena}.
 * @param n  this node
 * @return the index of this node; 0 on and only on failure
 */
int apn_retain(struct node const *n);

/**
 * Places a copy of this null-terminated string into {@code apn_carena}.
 * The terminating null byte is maintained.
 * @param s  this string
 * @return the index of this node; 0 on and only on failure
 */
int apn_retainstr(char const *s);
#endif
