#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dynarr.h"
#include "elf32.h"
#include "instr.h"
#include "lex.h"
#include "ntsl.h"
#include "node.h"
#include "object.h"
#include "reloc.h"
#include "version.h"
/* begin from .y */
extern char const *LL1_fname;
extern int LL1_line, LL1_col;
extern _Bool LL1_fail;
_Bool LL1_parse(struct token (*next)(void), struct node *);
/* end from .y */

/* Local Types *******************************************************/
enum progsection {
	PS_UNK = 0,
	PS_TEXT = 1,
	PS_DATA = 2,
	PS_ABS = 0xFFF1
};
struct value {
	enum progsection sec;
	_Bool fromsym;
	struct string symbol;
	int baseoff;
	enum reloc roverride;
	union {
		long long ivalue;
		int evalue; /* node reference */
	} u;
};
struct symbol {
	struct string name;
	enum progsection section;
	int offset; /* if expr is NULL: the value, else, used for '.' sym */
	int expr; /* node reference */
	int size;
	enum Elf_SYMTYPE type;
};
struct fixup {
	enum reloc type;
	enum progsection section;
	int offset; /* location for fixup, used also for '.' sym */
	int expr; /* node reference */
	_Bool handled; /* if handled, done with it, don't do it twice */
};

enum asterflags {
	AF_PRINTHELP = 1,
	AF_PRINTVERSION = 2,
	AF_PRINTSTATE = 4,
	AF_EMITGV = 8,
	AF_USESTDIN = 16,
	AF_DIE = 2048,
};

struct state {
	struct dynarr text;
	struct dynarr data;
	struct dynarr syms;
	struct dynarr fixups;
	struct dynarr macros;
	struct ntsl *globals;
	int textalign;
	int dataalign;
	int macdepth;
	enum progsection cursection;
	enum Elf_SYMTYPE nexttype;
};

/* Local Functions ***************************************************/
static int parse(char const *, _Bool);
static struct node *get(int);
static struct node *get_arg(int, int);
static struct node *get_args(int);
static char *get_str(int);
static _Bool applyfixups(void);
static struct ntsl *collectsyms(Elf32_Word *nlocals);
static _Bool installsyms(struct section *symtab, struct section *strtab,
	int textseci, int dataseci, int bssseci);
static _Bool makeelf(struct objfile *out);

static _Bool addmacro(int);
static int findmacro(struct string);

static _Bool eval(int);
static _Bool evalascii(int, _Bool);
static _Bool evaldbytes(int, enum reloc, int n);
static _Bool evaldbyte(int);
static _Bool evaldhword(int);
static _Bool evaldword(int);
static void evalerr(char const *, int);
static void evalwarn(char const *, int);
static void evalerrdirective(int);
static _Bool evalwarndirective(int);
static struct value evalexpr(int, enum progsection, int);
static struct value evalfunc(int, enum progsection, int);
static _Bool evalglobalize(int);
static _Bool evalif(int);
static _Bool evalinclude(int);
static _Bool evalinstr(int);
static _Bool evallabel(int);
static _Bool evalmacro(int);
static _Bool evalp2align(int);
static _Bool evalsec(int, enum progsection);
static _Bool evalsizer(int);
static _Bool evalsymbind(int);

static void printhelp(FILE *file);
static void printstate(void);
static void printsec(char const *, struct dynarr, int);
static void printsyms(struct dynarr, struct ntsl const *);
static void printfixups(struct dynarr);

/* Global State *******************************************************/
struct ntsl *sl; /* filenames, to be referenced in tree nodes */
struct ntsl *incdirs; /* dirnames */
struct state state;

static void
printhelp(FILE *file) {
	fprintf(file,
	"usage: aster [-g|-s] [-hv?] [-I path] [-o outfile] [file]\n");
	fprintf(file, "\t-g          print tree as GraphViz\n");
	fprintf(file, "\t-h, -?      print this help and exit\n");
	fprintf(file,
		"\t-I path     add path to the .include search path\n");
	fprintf(file, "\t-o outfile  write to outfile instead of a.out\n");
	fprintf(file, "\t-s          print internal state\n");
	fprintf(file, "\t-v          print version and exit\n");
}
int
main(int argc, char **argv) {
	int retval = 0;

	char const *outfname = "a.out";
	FILE *helpfile = stdout;
	argv += 1; argc -= 1;
	int flags = 0;
	_Bool process = 1;
	while (process && argc && *argv && **argv == '-') {
		char const *str = *(argv++); argc--;
		if (str[1] == '\0') {
			flags |= AF_USESTDIN;
			break;
		}
		int i = 1;
		while (str && str[i] != '\0') {
			switch (str[i++]) {
			case '-':
				if (i == 2 && str[i] == '\0') {
					process = 0;
				} else {
					flags = AF_PRINTHELP;
					process = 0;
					retval = 1;
					i = strlen(str);
				}
				break;
			case 'I':
				if (str[i] == '\0') {
					i = 0;
					str = *(argv++); argc--;
				}
				if (str) {
					ntslappend(&incdirs, str + i);
					i += strlen(str + i);
				} else {
					fprintf(stderr, "aster: error: ");
					fprintf(stderr, "missing parameter ");
					fprintf(stderr, "to -I flag\n");
					process = 0;
					flags |= AF_DIE;
				}
				break;
			case 'g':
				flags |= AF_EMITGV;
				break;
			case 'h':
			case '?':
				flags |= AF_PRINTHELP;
				break;
			case 'o':
				if (str[i] == '\0') {
					i = 0;
					str = *(argv++); argc--;
				}
				if (str) {
					outfname = str + i;
					i += strlen(outfname);
				} else {
					fprintf(stderr, "aster: error: ");
					fprintf(stderr, "missing parameter ");
					fprintf(stderr, "to -o flag\n");
					process = 0;
					flags |= AF_DIE;
				}
				break;
			case 's':
				flags |= AF_PRINTSTATE;
				break;
			case 'v':
				flags |= AF_PRINTVERSION;
				break;
			default:
				retval = 1;
				flags |= AF_PRINTHELP;
				helpfile = stderr;
				break;
			}
		}
	}

	if (flags & AF_DIE) {
		ntslfree(incdirs);
		return 1;
	}

	if (*argv && ((flags&AF_USESTDIN) || *(argv + 1))) {
		retval = 1;
		flags |= AF_PRINTHELP;
		helpfile = stderr;
	}

	if ((flags&(AF_PRINTSTATE | AF_EMITGV))
	        == (AF_PRINTSTATE | AF_EMITGV)) {
		retval = 1;
		flags |= AF_PRINTHELP;
		helpfile = stderr;
	}
	if (flags&AF_PRINTVERSION) {
		ntslfree(incdirs);
		incdirs = NULL;
		printf("aster (URSA) " URSA_VERSION "\n");
		if (!(flags&AF_PRINTHELP)) return retval;
	}
	if (flags&AF_PRINTHELP) {
		ntslfree(incdirs);
		printhelp(helpfile);
		return retval;
	}
	incdirs = ntsladd(incdirs, ""); /* start with curdir! */

	state.text = da_new(1);
	state.data = da_new(1);
	state.syms = da_new(sizeof(struct symbol));
	state.fixups = da_new(sizeof(struct fixup));
	state.macros = da_new(sizeof(int));
	state.cursection = PS_TEXT;
	state.textalign = 2;
	state.dataalign = 1;
	state.globals = NULL;
	state.nexttype = STT_NOTYPE;

	int prog = parse((flags&AF_USESTDIN)? NULL : *argv, 0);
	if (prog && eval(prog) && applyfixups()) {
		if (flags&AF_EMITGV) apn_print(get(prog));
		if (flags&AF_PRINTSTATE) printstate();
		struct objfile elf = {0};
		if (makeelf(&elf) && outfname) {
			FILE *outfile = fopen(outfname, "wb");
			if (!outfile) {
				perror(outfname);
				retval = 1;
			} else {
				obj_emit(elf, outfile);
			}
		}
		obj_free(elf);
	} else {
		retval = 1;
	}
	apn_free();
	ntslfree(state.globals);
	da_free(state.fixups);
	da_free(state.syms);
	da_free(state.data);
	da_free(state.text);
	ntslfree(sl);
	ntslfree(incdirs);
	return retval;
}

static int
parse(char const *fname, _Bool searchinc)
{
	if (fname) {
		_Bool found = 0;
		int len = strlen(fname) + 1;
		struct ntsl *p = incdirs;
		if (!p) {
			fprintf(stderr, "internal error: lost incdirs\n");
			return 0;
		}
		do {
			char *buf = malloc(p->length + len + 1);
			strncpy(buf, p->content, p->length + 1);
			if (p->length) {
				buf[p->length] = '/';
				strncpy(buf + p->length + 1, fname, len);
			} else {
				strncpy(buf + p->length, fname, len);
			}
			if (alx_usefile(buf)) {
				found = 1;
				sl = ntsladd(sl, buf);
				free(buf); buf = NULL;
				if (!sl) { alx_close(); return 0; }
				LL1_fname = sl->content;
				break;
			}
			free(buf);
			p = p->next;
		} while (searchinc && p);
		if (!found) {
			fprintf(stderr, "error: could not find %s\n", fname);
			return 0;
		}
	} else {
		alx_usestdin();
		LL1_fname = "<stdin>";
	}

	struct node out;
	_Bool parsed = LL1_parse(alx_next, &out);
	alx_close();
	if (!parsed) return 0;
	return apn_retain(&out);
}

static _Bool
applyfixups(void)
{
	for (int i = 0; i < state.fixups.length; i++) {
		struct fixup *f = (struct fixup *)da_get(state.fixups, i);
		if (f->handled) continue;
		unsigned char *buf = (f->section == PS_TEXT)?
			state.text.content : state.data.content;
		buf += f->offset;
		struct value v = evalexpr(f->expr, f->section, f->offset);
		if (v.sec == PS_UNK && !v.fromsym) {
			evalerr("illegal fixup", f->expr);
			return 0;
		}
		if (v.sec != PS_ABS && f->type != R_URSA_PC8) continue;
		if (v.sec != f->section && f->type == R_URSA_PC8) continue;
		enum reloc r = v.roverride? v.roverride : f->type;
		if (rel_apply(r, buf, f->offset, v.baseoff, v.u.ivalue)) {
			f->handled = 1;
		}
	}
	return 1;
}

static _Bool
eval(int prog)
{
	while (prog && get(prog)->type != APN_NONE) {
		switch (get(prog)->type) {
		case APN_LABEL:
			if (!evallabel(prog)) return 0;
			break;
		case APN_INSTRUCTION:
		{
			char *instr = get(prog)->c.svalue.content;
			int len = get(prog)->c.svalue.length;
			if (!strncmp(instr, ".ascii", len + 1)) {
				if (!evalascii(prog, 0)) return 0;
			} else if (!strncmp(instr, ".asciz", len + 1)) {
				if (!evalascii(prog, 1)) return 0;
			} else if (!strncmp(instr, ".byte", len + 1)) {
				if (!evaldbyte(prog)) return 0;
			} else if (!strncmp(instr, ".data", len + 1)) {
				if (!evalsec(prog, PS_DATA)) return 0;
			} else if (!strncmp(instr, ".function", len + 1)) {
				if (apn_arglen(get_args(prog)) != 0) {
					evalerr(".function has no parameters",
					        prog);
					return 0;
				}
				state.nexttype = STT_FUNC;
			} else if (!strncmp(instr, ".error", len + 1)) {
				evalerrdirective(prog);
				return 0;
			} else if (!strncmp(instr, ".global", len + 1)) {
				if (!evalglobalize(prog)) return 0;
			} else if (!strncmp(instr, ".hword", len + 1)) {
				if (!evaldhword(prog)) return 0;
			} else if (!strncmp(instr, ".include", len + 1)) {
				if (!evalinclude(prog)) return 0;
				continue;
			} else if (!strncmp(instr, ".object", len + 1)) {
				if (apn_arglen(get_args(prog)) != 0) {
					evalerr(".object has no parameters",
					        prog);
					return 0;
				}
				state.nexttype = STT_OBJECT;
			} else if (!strncmp(instr, ".p2align", len + 1)) {
				if (!evalp2align(prog)) return 0;
			} else if (!strncmp(instr, ".size", len + 1)) {
				if (!evalsizer(prog)) return 0;
			} else if (!strncmp(instr, ".text", len + 1)) {
				if (!evalsec(prog, PS_TEXT)) return 0;
			} else if (!strncmp(instr, ".warning", len + 1)) {
				if (!evalwarndirective(prog)) return 0;
			} else if (!strncmp(instr, ".word", len + 1)) {
				if (!evaldword(prog)) return 0;
			} else {
				if (!evalinstr(prog)) return 0;
			}
			break;
		}
		case APN_SYMBIND:
			if (!evalsymbind(prog)) return 0;
			break;
		case APN_CONDITIONAL:
			if (!evalif(prog)) return 0;
			break;
		case APN_MACRO:
			if (!addmacro(prog)) return 0;
			break;
		default:
			fprintf(stderr, "how did we get here?\n");
			fprintf(stderr, "\t%d\n", get(prog)->type);
			return 0;
			break;
		}
		if (prog) prog = get(prog)->a.next;
	}
	return 1;
}

static _Bool
evalascii(int node, _Bool terminate)
{
	if (!node) return 0;
	int args = get(node)->b.arglist;
	if (!args) return 0;
	int n = apn_arglen(get(args));
	if (n != 1) {
		evalerr("bad argument count", node);
		return 0;
	}
	int arg = apn_arg(get(args), 0);
	if (!arg || get(arg)->type != APN_ARG_STRING) {
		evalerr("bad argument", node);
		return 0;
	}
	struct dynarr *d = &state.text;
	if (state.cursection == PS_DATA) d = &state.data;
	char *str = get_str(arg);
	n = strlen(str);
	for (int i = 0; i < n; i++) {
		da_append(d, str + i);
	}
	if (terminate) da_append(d, "\0");
	return 1;
}

static _Bool
evaldbytes(int node, enum reloc r, int size)
{
	if (!node) return 0;
	int args = get(node)->b.arglist;
	if (!args) return 1; /* success if no arguments */
	int n = apn_arglen(get(args));
	struct dynarr *d = &state.text;
	if (state.cursection == PS_DATA) d = &state.data;
	for (int i = 0; i < n; i++) {
		int arg = apn_arg(get(args), i);
		if (!arg || get(arg)->type != APN_ARG_EXPR) {
			evalerr("bad argument for .byte", node);
			return 0;
		}
		struct fixup f = {
			r,
			state.cursection,
			d->length,
			get(arg)->b.evalue
		};
		for (int j = 0; j < size; j++) da_append(d, "\0");
		da_append(&state.fixups, &f);
	}
	return 1;
}

static _Bool
evaldbyte(int node)
{
	return evaldbytes(node, R_URSA_ABS8, 1);
}


static _Bool
evaldhword(int node)
{
	return evaldbytes(node, R_URSA_ABS16, 2);
}

static _Bool
evaldword(int node)
{
	return evaldbytes(node, R_URSA_ABS32, 4);
}

static void
evalerr(char const *msg, int node)
{
	if (node) {
		fprintf(stderr, "%s:%d: ", get(node)->fname, get(node)->line);
	}
	fprintf(stderr, "Error: %s\n", msg);
}

static void
evalwarn(char const *msg, int node)
{
	if (node) {
		fprintf(stderr, "%s:%d: ", get(node)->fname, get(node)->line);
	}
	fprintf(stderr, "Warning: %s\n", msg);
}

struct value
evalexpr(int node, enum progsection sec, int off)
{
	struct value v1, v2;
	struct value out = {PS_UNK};
	out.u.evalue = node;
	if (!node) return out;
	switch (get(node)->type) {
	case APN_MARG:
		evalerr("macro argument outside of macro", node);
		break;
	case APN_FUNCALL:
		out = evalfunc(node, sec, off);
		break;
	case APN_VALUE:
		out.sec = PS_ABS;
		out.u.ivalue = get(node)->c.ivalue;
		break;
	case APN_SYMBOL:
		if (!strncmp(get(node)->c.svalue.content, ".", 2)) {
			out.sec = sec;
			out.u.ivalue = off;
			break;
		}
		out.fromsym = 1;
		out.symbol = get(node)->c.svalue;
		out.u.ivalue = 0;
		for (int i = 0; i < state.syms.length; i++) {
			struct symbol s =
				*(struct symbol *)da_get(state.syms, i);
			if (strncmp(get(node)->c.svalue.content,
			            s.name.content, 
			            s.name.length + 1)) continue;
			if (s.expr)
				return evalexpr(s.expr, s.section, s.offset);
			out.sec = s.section;
			out.baseoff = s.offset;
			break;
		}
		break;
	case APN_PRODUCT:
		v1 = evalexpr(get(node)->a.left, sec, off);
		v2 = evalexpr(get(node)->b.right, sec, off);
		if (v1.sec == PS_ABS && v2.sec == PS_ABS) {
			out.sec = PS_ABS;
			out.u.ivalue = v1.u.ivalue * v2.u.ivalue;
			break;
		}
		break;
	case APN_QUOTIENT:
		v1 = evalexpr(get(node)->a.left, sec, off);
		v2 = evalexpr(get(node)->b.right, sec, off);
		if (v1.sec == PS_ABS && v2.sec == PS_ABS) {
			out.sec = PS_ABS;
			out.u.ivalue = v1.u.ivalue / v2.u.ivalue;
			break;
		}
		break;
	case APN_REMAINDER:
		v1 = evalexpr(get(node)->a.left, sec, off);
		v2 = evalexpr(get(node)->b.right, sec, off);
		if (v1.sec == PS_ABS && v2.sec == PS_ABS) {
			out.sec = PS_ABS;
			out.u.ivalue = v1.u.ivalue % v2.u.ivalue;
			break;
		}
		break;
	case APN_SUM:
		v1 = evalexpr(get(node)->a.left, sec, off);
		v2 = evalexpr(get(node)->b.right, sec, off);
		if (v1.sec == PS_ABS && (v2.sec != PS_UNK || v2.fromsym)) {
			out = v2;
			out.u.ivalue += v1.u.ivalue;
			break;
		} else if ((v1.sec != PS_UNK || v1.fromsym)
		        && v2.sec == PS_ABS) {
			out = v1;
			out.u.ivalue += v2.u.ivalue;
			break;
		}
		break;
	case APN_DIFFERENCE:
		v1 = evalexpr(get(node)->a.left, sec, off);
		v2 = evalexpr(get(node)->b.right, sec, off);
		if (v1.sec == v2.sec && v1.sec != PS_UNK) {
			out.sec = PS_ABS;
			out.u.ivalue = (v1.baseoff + v1.u.ivalue)
			             - (v2.baseoff + v2.u.ivalue);
			break;
		} else if ((v1.sec != PS_UNK || v1.fromsym)
		           && v2.sec == PS_ABS) {
			if (v2.u.ivalue <= v1.u.ivalue || v1.sec == PS_UNK) {
				out = v1;
				out.u.ivalue -= v2.u.ivalue;
			} else {
				out.sec = v1.sec;
				out.u.ivalue =
					v1.baseoff + v1.u.ivalue - v2.u.ivalue;
			}
			break;
		}
		break;
	case APN_LESS:
	case APN_LESSEQ:
	case APN_EQUAL:
	case APN_UNEQUAL:
	case APN_GREATEREQ:
	case APN_GREATER:
		v1 = evalexpr(get(node)->a.left, sec, off);
		v2 = evalexpr(get(node)->b.right, sec, off);
		if (v1.sec == v2.sec && v1.sec != PS_UNK) {
			out.sec = PS_ABS;
			int first = (v1.baseoff + v1.u.ivalue);
			int second = (v2.baseoff + v2.u.ivalue);
			switch (get(node)->type) {
			case APN_LESS:
				out.u.ivalue = (first < second);
				break;
			case APN_LESSEQ:
				out.u.ivalue = (first <= second);
				break;
			case APN_EQUAL:
				out.u.ivalue = (first == second);
				break;
			case APN_UNEQUAL:
				out.u.ivalue = (first != second);
				break;
			case APN_GREATEREQ:
				out.u.ivalue = (first >= second);
				break;
			case APN_GREATER:
				out.u.ivalue = (first > second);
				break;
			default:
				break;
			}
			break;
		}
		break;
	case APN_BITSHL:
		v1 = evalexpr(get(node)->a.left, sec, off);
		v2 = evalexpr(get(node)->b.right, sec, off);
		if (v1.sec == PS_ABS && v2.sec == PS_ABS) {
			out.sec = PS_ABS;
			out.u.ivalue = v1.u.ivalue << v2.u.ivalue;
			break;
		}
		break;
	case APN_BITSHR:
		v1 = evalexpr(get(node)->a.left, sec, off);
		v2 = evalexpr(get(node)->b.right, sec, off);
		if (v1.sec == PS_ABS && v2.sec == PS_ABS) {
			out.sec = PS_ABS;
			out.u.ivalue = v1.u.ivalue >> v2.u.ivalue;
			break;
		}
		break;
	case APN_BITAND:
		v1 = evalexpr(get(node)->a.left, sec, off);
		v2 = evalexpr(get(node)->b.right, sec, off);
		if (v1.sec == PS_ABS && v2.sec == PS_ABS) {
			out.sec = PS_ABS;
			out.u.ivalue = v1.u.ivalue & v2.u.ivalue;
			break;
		}
		break;
	case APN_BITOR:
		v1 = evalexpr(get(node)->a.left, sec, off);
		v2 = evalexpr(get(node)->b.right, sec, off);
		if (v1.sec == PS_ABS && v2.sec == PS_ABS) {
			out.sec = PS_ABS;
			out.u.ivalue = v1.u.ivalue | v2.u.ivalue;
			break;
		}
		break;
	case APN_BITXOR:
		v1 = evalexpr(get(node)->a.left, sec, off);
		v2 = evalexpr(get(node)->b.right, sec, off);
		if (v1.sec == PS_ABS && v2.sec == PS_ABS) {
			out.sec = PS_ABS;
			out.u.ivalue = v1.u.ivalue ^ v2.u.ivalue;
			break;
		}
		break;
	default:
		break;
	}
	if (out.sec == PS_ABS) {
		struct node *n = get(node);
		n->type = APN_VALUE;
		n->c.ivalue = out.u.ivalue;
	}
	return out;
}

static int
evalregnumfunc(int args, int nargs)
{
	if (!args || get(args)->type != APN_ARGLIST) {
		evalerr("not an arglist", args);
		return -2;
	}
	if (nargs != 1) {
		evalerr("wrong number of arguments", args);
		return -2;
	}
	struct node const *arg = get(apn_arg(get(args), 0));
	if (!arg || arg->type != APN_ARG_REG) return -1;
	return arg->c.ivalue;
}

static struct value
evalstrlenfunc(int args, int nargs)
{
	struct value out = {PS_UNK};
	if (!args || get(args)->type != APN_ARGLIST) {
		evalerr("not an arglist", args);
		return out;
	}
	if (nargs != 1) {
		evalerr("wrong number of arguments", args);
		return out;
	}
	int arg = apn_arg(get(args), 0);
	if (!arg || get(arg)->type != APN_ARG_STRING) {
		evalerr("bad argument", args);
		return out;
	}
	out.sec = PS_ABS;
	out.u.ivalue = strlen(get_str(arg));
	return out;
}

static struct value
evalvalfunc(int args, int nargs,
            enum progsection sec, int off)
{
	struct value out = {PS_UNK};
	if (!args || get(args)->type != APN_ARGLIST) {
		evalerr("not an arglist", args);
		return out;
	}
	if (nargs != 1) {
		evalerr("wrong number of arguments to value function", args);
		return out;
	}
	int arg = apn_arg(get(args), 0);
	if (!arg || get(arg)->type != APN_ARG_EXPR || !get(arg)->b.evalue) {
		evalerr("bad argument to value function", arg);
		return out;
	}
	out = evalexpr(get(arg)->b.evalue, sec, off);
	return out;
}

static struct value
evalfuncroverride(int args, int nargs,
                  enum progsection sec, int off,
                  enum reloc roverride)
{
	struct value out = {PS_UNK};
	if (!args || get(args)->type != APN_ARGLIST) {
		evalerr("not an arglist", args);
		return out;
	}
	if (nargs != 1) {
		evalerr("wrong number of arguments to gX function", args);
		return out;
	}
	int arg = apn_arg(get(args), 0);
	if (!arg || get(arg)->type != APN_ARG_EXPR || !get(arg)->b.evalue) {
		evalerr("bad argument to gX function", arg);
		return out;
	}
	out = evalexpr(get(arg)->b.evalue, sec, off);
	out.roverride = roverride;
	return out;
}

static struct value
evalfunc(int node, enum progsection sec, int off)
{
	struct value out = {PS_UNK};
	if (!node || get(node)->type != APN_FUNCALL) return out;
	int args = get(node)->b.arglist;
	int nargs = apn_arglen(get(args));
	int n = get(node)->c.svalue.length + 1;
	char const *name = get(node)->c.svalue.content;
	if (!strncmp(name, "defined", n)) {
		if (nargs != 1) {
			evalerr("wrong number of arguments for defined(...)",
			        node);
			return out;
		}
		int arg = apn_arg(get(args), 0);
		if (!arg || get(arg)->type != APN_ARG_EXPR) {
			evalerr("bad argument for defined(...)", node);
			return out;
		}
		arg = get(arg)->b.evalue;
		if (!arg || get(arg)->type != APN_SYMBOL) {
			evalerr("bad argument for defined(...)", node);
			return out;
		}
		char const *s = get(arg)->c.svalue.content;
		int n = get(arg)->c.svalue.length + 1;
		out.sec = PS_ABS;
		out.u.ivalue = 0;
		for (int i = 0; i < state.syms.length; i++) {
			struct symbol sym =
				*(struct symbol *)da_get(state.syms, i);
			if (!strncmp(s, sym.name.content, n)) {
				out.u.ivalue = 1;
				break;
			}
		}
	} else if (!strncmp(name, "g0", n)) {
		return evalfuncroverride
			(args, nargs, sec, off, R_URSA_SSET8_0_NC);
	} else if (!strncmp(name, "g1", n)) {
		return evalfuncroverride
			(args, nargs, sec, off, R_URSA_SSET8_1_NC);
	} else if (!strncmp(name, "g2", n)) {
		return evalfuncroverride
			(args, nargs, sec, off, R_URSA_SSET8_2_NC);
	} else if (!strncmp(name, "g3", n)) {
		return evalfuncroverride
			(args, nargs, sec, off, R_URSA_SSET8_3_NC);
	} else if (!strncmp(name, "isreg", n)) {
		int x = evalregnumfunc(args, nargs);
		if (x == -2) return out;
		out.sec = PS_ABS;
		out.u.ivalue = (x >= 0);
	} else if (!strncmp(name, "len", n)) {
		return evalstrlenfunc(args, nargs);
	} else if (!strncmp(name, "regnum", n)) {
		int x = evalregnumfunc(args, nargs);
		if (x < 0) {
			evalerr("not a register", node);
			return out;
		}
		out.sec = PS_ABS;
		out.u.ivalue = x;
	} else if (!strncmp(name, "value", n)) {
		return evalvalfunc(args, nargs, sec, off);
	}
	return out;
}

static _Bool
evalglobalize(int node)
{
	if (!node) return 0;
	int args = get(node)->b.arglist;
	int nargs = apn_arglen(get(args));
	if (nargs != 1) {
		evalerr("wrong number of arguments to .global", node);
		return 0;
	}
	struct node const *arg = get(apn_arg(get(args), 0));
	if (!arg || arg->type != APN_ARG_EXPR
	    || !arg->b.evalue || get(arg->b.evalue)->type != APN_SYMBOL) {
		evalerr("bad argument to .global", node);
		return 0;
	}
	state.globals = ntsladd(state.globals,
	                        get(arg->b.evalue)->c.svalue.content);
	return state.globals;
}

static _Bool
evalif(int node)
{
	if (!node) return 0;
	int off = (state.cursection == PS_TEXT)?
	          state.text.length : state.data.length;
	struct value v = evalexpr(get(node)->b.evalue, state.cursection, off);
	if (v.sec != PS_ABS) {
		evalerr("failed to resolve absolute expression in .if", node);
		return 0;
	}
	if (v.u.ivalue) return eval(get(node)->c.cond.tpath);
	return eval(get(node)->c.cond.fpath);
}

static _Bool
evalinclude(int node)
{
	if (!node) return 1;
	int args = get(node)->b.arglist;
	int nargs = apn_arglen(get(args));
	if (nargs != 1) {
		evalerr("wrong number of arguments to .include", node);
		return 0;
	}
	int arg = apn_arg(get(args), 0);
	if (!arg || get(arg)->type != APN_ARG_STRING) {
		evalerr("bad argument to .include", node);
		return 0;
	}
	int prog = parse(get_str(arg), 1);
	if (!prog) {
		evalerr("failed to .include", node);
		return 0;
	}
	if (get(prog)->type == APN_NONE) { return 1; }
	/* got a nonempty program: install it in sequence */
	struct node *p = get(prog);
	while (p->a.next) { p = get(p->a.next); }
	p->a.next = get(node)->a.next; /* attach rest of program */
	*get(node) = *get(prog);
	return 1;
}

static _Bool
evalinstr(int node)
{
	if (!node) return 0;
	if (findmacro(get(node)->c.svalue) >= 0) return evalmacro(node);
	struct ins_desc desc = ins_desc(get(node));
	if (desc.rtype == R_URSA_ILLEGAL) {
		evalerr("illegal instruction", node);
		return 0;
	}
	int args = get(node)->b.arglist;
	int nargs = apn_arglen(get(args));
	int expr = 0;
	if (nargs > 0 && get_arg(args, 0)->type == APN_ARG_EXPR) {
		expr = get_arg(args, 0)->b.evalue;
	} else if (nargs > 0 && get_arg(args, 0)->type == APN_ARG_MEM) {
		expr = get_arg(args, 0)->b.off;
	} else if (nargs > 1 && get_arg(args, 1)->type == APN_ARG_EXPR) {
		expr = get_arg(args, 1)->b.evalue;
	} else if (nargs > 1 && get_arg(args, 1)->type == APN_ARG_MEM) {
		expr = get_arg(args, 1)->b.off;
	}
	if (expr) {
		struct fixup f = {
			desc.rtype,
			state.cursection,
			(state.cursection == PS_TEXT)?
				state.text.length : state.data.length,
			expr,
		};
		da_append(&state.fixups, &f);
	}
	struct dynarr *d =
		(state.cursection == PS_TEXT)? &state.text : &state.data;
	unsigned char x = desc.encoding & 0xFF;
	da_append(d, &x);
	x = (desc.encoding>>8) & 0xFF;
	da_append(d, &x);
	return 1;
}

static _Bool
evallabel(int node)
{
	if (!node) return 0;
	struct symbol sym = {
		get(node)->c.svalue,
		state.cursection,
		(state.cursection == PS_TEXT)?
			state.text.length : state.data.length,
		0,
		0, state.nexttype,
	};
	state.nexttype = STT_NOTYPE;
	int n = sym.name.length + 1;
	for (int i = 0; i < state.syms.length; i++) {
		struct symbol s = *(struct symbol *)da_get(state.syms, i);
		if (strncmp(s.name.content, sym.name.content, n)) continue;
		evalerr("duplicate symbol:", node);
		fprintf(stderr, "\t%s\n", sym.name.content);
		return 0;
	}
	return da_append(&state.syms, &sym);
}

static _Bool
evalmacro(int node)
{
	if (!node) return 0;
	if (state.macdepth >= 1000) {
		evalerr("macro nesting depth exceeded", node);
		return 0;
	}
	state.macdepth++;
	struct string name = get(node)->c.svalue;
	int mi = findmacro(name);
	if (mi < 0) {
		evalerr("not a macro", node);
		fprintf(stderr, "\t%s\n", name.content);
		return 0;
	}
	int defn = *(int*)da_get(state.macros, mi);
	if (!defn) return 0;
	int prog = apn_fillmargs(get(defn)->b.stream, get(node)->b.arglist);
	if (prog < 0 || !eval(prog)) {
		fprintf(stderr, "\twhile expanding %s at %d\n",
		        name.content, get(node)->line);
		state.macdepth--;
		return 0;
	};
	state.macdepth--;
	return 1;
}


/* only allow absolute expressions 0--4 */
static _Bool
evalp2align(int node)
{
	if (!node) return 0;
	int args = get(node)->b.arglist;
	int nargs = apn_arglen(get(args));
	if (nargs != 1) {
		evalerr("wrong number of arguments to .p2align", node);
		fprintf(stderr, "got %d from %d\n", nargs, args);
		return 0;
	}
	int arg = apn_arg(get(args), 0);
	if (!arg || get(arg)->type != APN_ARG_EXPR) {
		evalerr("bad argument to .p2align", node);
		return 0;
	}
	int off = (state.cursection == PS_TEXT)?
	          state.text.length : state.data.length;
	struct value v = evalexpr(get(arg)->b.evalue, state.cursection, off);
	if (v.sec != PS_ABS) {
		evalerr("bad absolute expression", node);
		return 0;
	}
	if (v.u.ivalue < 0 || v.u.ivalue > 12) {
		evalerr("alignment power must be between 0 and 12", node);
		return 0;
	}
	struct dynarr *d = NULL;
	int *align = NULL;
	switch (state.cursection) {
	case PS_TEXT:
		d = &state.text;
		align = &state.textalign;
		break;
	case PS_DATA:
		d = &state.data;
		align = &state.dataalign;
		break;
	default:
		return 0;
	}
	if (*align < 1<<v.u.ivalue) *align = 1<<v.u.ivalue;
	while (d->length % *align) { da_append(d, ""); }
	return 1;
}

static _Bool
evalsec(int node, enum progsection sec)
{
	if (!node) return 0;
	int args = get(node)->b.arglist;
	int nargs = apn_arglen(get(args));
	if (nargs != 0) {
		evalerr("section change takes no arguments", node);
		return 0;
	}
	if (sec != PS_TEXT && sec != PS_DATA) {
		evalerr("bad section change", node);
		return 0;
	}
	state.cursection = sec;
	return 1;
}

static _Bool
evalsizer(int node)
{
	if (!node) return 0;
	int args = get(node)->b.arglist;
	int nargs = apn_arglen(get(args));
	if (nargs != 2) {
		evalerr("wrong number of arguments to .size", node);
		return 0;
	}
	int arg = apn_arg(get(args), 0);
	if (!arg || get(arg)->type != APN_ARG_EXPR) {
		evalerr("bad argument to .size", node);
		return 0;
	}
	arg = get(arg)->b.evalue;
	if (!arg || get(arg)->type != APN_SYMBOL) {
		evalerr("bad argument to .size", node);
		return 0;
	}
	struct symbol *sym = NULL;
	char const *name = get(arg)->c.svalue.content;
	int n = get(arg)->c.svalue.length + 1;
	for (int i = 0; i < state.syms.length; i++) {
		struct symbol *s = (struct symbol *)da_get(state.syms, i);
		if (!s) continue;
		if (strncmp(s->name.content, name, n)) continue;
		sym = s;
		break;
	}
	if (!sym) {
		evalerr(".size sym,size must be after sym definition", node);
		return 0;
	}

	arg = apn_arg(get(args), 1);
	if (!arg || get(arg)->type != APN_ARG_EXPR) {
		evalerr("bad argument to .size", node);
		return 0;
	}
	int off = (state.cursection == PS_TEXT)?
	          state.text.length : state.data.length;
	struct value v = evalexpr(get(arg)->b.evalue, state.cursection, off);
	if (v.sec != PS_ABS) {
		evalerr("bad absolute expression", node);
		return 0;
	}
	if (v.u.ivalue < 0) {
		evalerr("negative size", node);
		return 0;
	}
	sym->size = v.u.ivalue;
	return 1;
}

static _Bool
evalsymbind(int node)
{
	if (!node) return 0;
	struct symbol sym = {
		get(node)->c.svalue,
		state.cursection,
		(state.cursection == PS_TEXT)?
			state.text.length : state.data.length,
		get(node)->b.evalue,
		0, state.nexttype,
	};
	state.nexttype = STT_NOTYPE;
	int n = sym.name.length + 1;
	for (int i = 0; i < state.syms.length; i++) {
		struct symbol s = *(struct symbol *)da_get(state.syms, i);
		if (strncmp(s.name.content, sym.name.content, n)) continue;
		evalerr("duplicate symbol:", node);
		fprintf(stderr, "\t%s\n", sym.name.content);
		return 0;
	}
	return da_append(&state.syms, &sym);
}

void
LL1_error(char const *s)
{
	fprintf(stderr, "%s:%d:%d: error: no parse\n",
	        LL1_fname, LL1_line, LL1_col);
	if (s) fprintf(stderr, "%s\n", s);
	LL1_fail = 1;
	//exit(1);
}

static void
printstate(void)
{
	printsec(".text", state.text, state.textalign);
	printsec(".data", state.data, state.dataalign);
	printf("syms:\n");
	printsyms(state.syms, state.globals);
	printf("fixups:\n");
	printfixups(state.fixups);
	printf("in: %s\n",
	       (state.cursection ==      0)? "UNK"   :
	       (state.cursection ==      1)? "TEXT"  :
	       (state.cursection ==      2)? "DATA"  :
	       (state.cursection == 0xFFF1)? "*ABS*" :
	       "????");
	printf("globals: %p\n", (void*)state.globals);
}

static void
printsec(char const *name, struct dynarr d, int align)
{
	printf("%s@%d :: %d/%d:", name, align, d.length, d.capacity);
	int i = 0;
	for (i = 0; i < d.length; i++) {
		if (!(i%16)) {
			if (i) printf("  |");
			for (int j = i - 16; i && j != i; j++) {
				unsigned char c =
					((unsigned char*)d.content)[j];
				printf("%c", (c >= 0x20 && c < 0x7F)? c : '.');
			}
			if (i) printf("|");
			printf("\n%04x ", i);
		}
		if (i && (i%16) == 8) printf(" ");
		printf(" %02x", ((unsigned char*)d.content)[i]);
	}
	for (int j = i; j%16; j++) {
		if ((j%16) == 8) printf(" ");
		printf("   ");
	}
	if (i%16) printf("  |");
	for (int j = i - (i%16); j < i; j++) {
		unsigned char c = ((unsigned char*)d.content)[j];
		printf("%c", (c >= 0x20 && c < 0x7F)? c : '.');
	}
	if (i%16) printf("|");
	printf("\n%04x\n", d.length);
}
static void
printsyms(struct dynarr d, struct ntsl const *gs)
{
	for (int i = 0; i < d.length; i++) {
		struct symbol s = *(struct symbol *)da_get(d, i);
		_Bool g = ntslfind(gs, s.name.content) >= 0;
		switch (s.section) {
		case PS_UNK:  printf("%c ", g?'U':'u'); break;
		case PS_TEXT: printf("%c ", g?'T':'t'); break;
		case PS_DATA: printf("%c ", g?'D':'d'); break;
		case PS_ABS:  printf("%c ", g?'A':'a'); break;
		default:      printf("? "); break;
		}
		printf("%04x ", s.offset);
		printf("%c ", s.expr? 'e' : ' ');
		printf("%s\n", s.name.content);
	}
}
static void
printfixups(struct dynarr d)
{
	for (int i = 0; i < d.length; i++) {
		struct fixup f = *(struct fixup *)da_get(d, i);
		printf("%c ", f.handled? 'H' : ' ');
		printf("%-20s",
		       (f.type == R_URSA_NONE)?       "R_URSA_NONE"       :
		       (f.type == R_URSA_ABS8)?       "R_URSA_ABS8"       :
		       (f.type == R_URSA_ABS16)?      "R_URSA_ABS16"      :
		       (f.type == R_URSA_ABS32)?      "R_URSA_ABS32"      :
		       (f.type == R_URSA_ALU4)?       "R_URSA_ALU4"       :
		       (f.type == R_URSA_SSET8)?      "R_URSA_SSET8"      :
		       (f.type == R_URSA_SSET8_0_NC)? "R_URSA_SSET8_0_NC" :
		       (f.type == R_URSA_SSET8_1_NC)? "R_URSA_SSET8_0_NC" :
		       (f.type == R_URSA_SSET8_2_NC)? "R_URSA_SSET8_0_NC" :
		       (f.type == R_URSA_SSET8_3_NC)? "R_URSA_SSET8_0_NC" :
		       (f.type == R_URSA_ABS5)?       "R_URSA_ABS5"       :
		       (f.type == R_URSA_BOFF3)?      "R_URSA_BOFF3"      :
		       (f.type == R_URSA_WOFF3)?      "R_URSA_WOFF3"      :
		       (f.type == R_URSA_PC8)?        "R_URSA_PC8"        :
		       "R_URSA_ILLEGAL");
		printf("%s:%04x ",
		       (f.section == PS_UNK)?  "*UNK*" :
		       (f.section == PS_ABS)?  "*ABS*" :
		       (f.section == PS_TEXT)? ".text" :
		       (f.section == PS_DATA)? ".data" : "?????",
		       f.offset);
		struct value v = evalexpr(f.expr, f.section, f.offset);
		if (v.fromsym) {
			printf("<%s+%lld>", v.symbol.content, v.u.ivalue);
		} else if (v.sec != PS_UNK) {
			printf("<%s+%lld>",
			       (v.sec == PS_UNK)?  "*UNK*" :
			       (v.sec == PS_ABS)?  "*ABS*" :
			       (v.sec == PS_TEXT)? ".text" :
			       (v.sec == PS_DATA)? ".data" : "?????",
			       v.u.ivalue);
		} else {
			printf("<*UNK*>");
		}
		printf("\n");
	}
}

static _Bool
makeelf(struct objfile *out)
{
	_Bool good = 1;
	_Bool needreltext = 0;
	_Bool needreldata = 0;
	if (!out) return 0;
	applyfixups(); /* doesn't hurt to do it twice */
	for (int i = 0; i < state.fixups.length; i++) {
		struct fixup *f = (struct fixup *)da_get(state.fixups, i);
		if (!f || f->handled) continue;
		if (f->section == PS_TEXT) needreltext = 1;
		if (f->section == PS_DATA) needreldata = 1;
	}
	*out = obj_new(ET_REL, EM_URSA, 0);

	int nullseci = obj_addsec(out, SHT_NULL, 0, 0);
	int textseci = obj_addsec(out,
		SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR,
		state.textalign
	);
	int reltextseci = 0;
	if (needreltext) {
		reltextseci = obj_addsec(out, SHT_REL, 0, 4);
	}
	int dataseci = obj_addsec(out,
		SHT_PROGBITS, SHF_ALLOC | SHF_WRITE,
		state.dataalign
	);
	int reldataseci = 0;
	if (needreldata) {
		reldataseci = obj_addsec(out, SHT_REL, 0, 4);
	}
	int bssseci = obj_addsec(out,
		SHT_NOBITS, SHF_ALLOC | SHF_WRITE, 1
	);
	int symtabi = obj_addsec(out, SHT_SYMTAB, 0, 4);
	int strtabi = obj_addsec(out, SHT_STRTAB, 0, 1);
	int shstrtabi = obj_addsec(out, SHT_STRTAB, 0, 1);

	struct section *nullsec = obj_sec(*out, nullseci);
	struct section *textsec = obj_sec(*out, textseci);
	struct section *reltextsec =
		needreltext? obj_sec(*out, reltextseci) : NULL;
	/* int nreltext = 0; */
	struct section *datasec = obj_sec(*out, dataseci);
	struct section *bsssec = obj_sec(*out, bssseci);
	struct section *reldatasec =
		needreldata? obj_sec(*out, reldataseci) : NULL;
	/* int nreldata = 0; */
	struct section *symtab = obj_sec(*out, symtabi);
	struct section *strtab = obj_sec(*out, strtabi);
	struct section *shstrtab = obj_sec(*out, shstrtabi);

	nullsec->h.sh_name = obj_findstr(shstrtab, "");
	if (needreltext) {
		reltextsec->h.sh_name = obj_findstr(shstrtab, ".rel.text");
		reltextsec->h.sh_entsize = sizeof(Elf32_Rel);
		reltextsec->h.sh_link = symtabi;
		reltextsec->h.sh_info = textseci;
	}
	textsec->h.sh_name = obj_findstr(shstrtab, ".text");
	if (needreldata) {
		reldatasec->h.sh_name = obj_findstr(shstrtab, ".rel.data");
		reldatasec->h.sh_entsize = sizeof(Elf32_Rel);
		reldatasec->h.sh_link = symtabi;
		reldatasec->h.sh_info = dataseci;
	}
	datasec->h.sh_name = obj_findstr(shstrtab, ".data");
	bsssec->h.sh_name = obj_findstr(shstrtab, ".bss");
	symtab->h.sh_name = obj_findstr(shstrtab, ".symtab");
	symtab->h.sh_link = strtabi;
	symtab->h.sh_entsize = sizeof(Elf32_Sym);
	strtab->h.sh_name = obj_findstr(shstrtab, ".strtab");
	shstrtab->h.sh_name = obj_findstr(shstrtab, ".shstrtab");

	struct ntsl *names = collectsyms(&symtab->h.sh_info);
	installsyms(symtab, strtab, textseci, dataseci, bssseci);

	/* install relocations */
	for (int i = 0; i < state.fixups.length; i++) {
		struct fixup *f = (struct fixup *)da_get(state.fixups, i);
		if (!f || f->handled) continue;
		struct value v = evalexpr(f->expr, f->section, f->offset);
		unsigned char *buf = (f->section == PS_TEXT)?
			state.text.content : state.data.content;
		buf += f->offset;
		struct section *relsec = (f->section == PS_TEXT)?
			reltextsec : reldatasec;
		int xadd = 0;
		if (f->type == R_URSA_PC8 && v.sec == PS_UNK) {
			xadd = f->offset;
		}
		if (!rel_apply(f->type, buf, f->offset, v.u.ivalue, xadd)) {
			evalerr("impossible relocation", f->expr);
			good = 0;
			break;
		}
		int symi = -1;
		if (v.fromsym) {
			symi = ntslfind(names, v.symbol.content);
		} else if (v.sec == PS_TEXT) {
			symi = 1;
		} else if (v.sec == PS_DATA) {
			symi = 2;
		}
		if (symi < 0) {
			evalerr("lost symbol", f->expr);
			good = 0;
			break;
		}
		enum reloc r = v.roverride? v.roverride : f->type;
		obj_addrel(relsec, f->offset, r, symi);
	}

	/* fill final content */
	for (int i = 0; i < state.text.length; i++) {
		unsigned char c = ((unsigned char*)state.text.content)[i];
		obj_sappend8(textsec, c);
	}
	for (int i = 0; i < state.data.length; i++) {
		unsigned char c = ((unsigned char*)state.data.content)[i];
		obj_sappend8(datasec, c);
	}

	ntslfree(names);
	obj_finalize(out, shstrtabi);
	return good;
}

static struct ntsl *
collectsyms(Elf32_Word *nlocals)
{
	Elf32_Word nlocalslocal = 0;
	if (!nlocals) nlocals = &nlocalslocal;
	struct ntsl *names = NULL;

	/* EVALUATE */

	for (int i = 0; i < state.syms.length; i++) {
		struct symbol *s = (struct symbol *)da_get(state.syms, i);
		if (!s->expr) continue; /* labels are good */
		struct value v = evalexpr(s->expr, s->section, s->offset);
		if (v.fromsym || v.sec == PS_UNK) {
			s->section = PS_UNK;
			continue;
		}
		s->section = v.sec;
		s->offset = v.u.ivalue;
		s->expr = 0;
	}

	/* EXTERNALS */

	/* prepend UNK */
	for (int i = state.fixups.length - 1; i >= 0; i--) {
		struct fixup *f = (struct fixup *)da_get(state.fixups, i);
		if (!f || f->handled) continue;
		struct value v = evalexpr(f->expr, f->section, f->offset);
		if (v.sec == PS_UNK && v.fromsym) {
			names = ntsladd(names, v.symbol.content);
		}
	}

	/* GLOBALS */

	/* prepend data globals */
	for (int i = state.syms.length - 1; i >= 0; i--) {
		struct symbol s = *(struct symbol *)da_get(state.syms, i);
		if (s.section != PS_DATA) continue;
		if (ntslfind(state.globals, s.name.content) == -1) continue;
		names = ntsladd(names, s.name.content);
	}
	/* prepend text globals */
	for (int i = state.syms.length - 1; i >= 0; i--) {
		struct symbol s = *(struct symbol *)da_get(state.syms, i);
		if (s.section != PS_TEXT) continue;
		if (ntslfind(state.globals, s.name.content) == -1) continue;
		names = ntsladd(names, s.name.content);
	}
	/* prepend absolute globals */
	for (int i = state.syms.length - 1; i >= 0; i--) {
		struct symbol s = *(struct symbol *)da_get(state.syms, i);
		if (s.section != PS_ABS) continue;
		if (ntslfind(state.globals, s.name.content) == -1) continue;
		names = ntsladd(names, s.name.content);
	}

	/* LOCALS */

	/* prepend data locals */
	for (int i = state.syms.length - 1; i >= 0; i--) {
		struct symbol s = *(struct symbol *)da_get(state.syms, i);
		if (s.section != PS_DATA) continue;
		if (ntslfind(state.globals, s.name.content) != -1) continue;
		names = ntsladd(names, s.name.content);
		(*nlocals)++;
	}
	/* prepend text locals */
	for (int i = state.syms.length - 1; i >= 0; i--) {
		struct symbol s = *(struct symbol *)da_get(state.syms, i);
		if (s.section != PS_TEXT) continue;
		if (ntslfind(state.globals, s.name.content) != -1) continue;
		names = ntsladd(names, s.name.content);
		(*nlocals)++;
	}
	/* don't actually prepend absolute locals */

	/* prepend NULL and one head for each section */
	for (int i = 0; i < 4; i++) {
		names = ntsladd(names, "");
		(*nlocals)++;
	}
	return names;
}

static _Bool
installsyms(struct section *symtab, struct section *strtab,
            int textseci, int dataseci, int bssseci)
{
	if (!symtab || !strtab) return 0;
	obj_addsym(symtab, strtab, "", 0, 0, 0, SHN_UNDEF);
	obj_addsym(symtab, strtab, "", 0, 0,
		Elf32_ST_INFO(STB_LOCAL, STT_SECTION), textseci);
	obj_addsym(symtab, strtab, "", 0, 0,
		Elf32_ST_INFO(STB_LOCAL, STT_SECTION), dataseci);
	obj_addsym(symtab, strtab, "", 0, 0,
		Elf32_ST_INFO(STB_LOCAL, STT_SECTION), bssseci);

	struct ntsl *added = NULL;

	/* LOCALS */
	
	/* append text locals */
	for (int i = 0; i < state.syms.length; i++) {
		struct symbol s = *(struct symbol *)da_get(state.syms, i);
		if (s.section != PS_TEXT) continue;
		if (ntslfind(state.globals, s.name.content) != -1) continue;
		added = ntsladd(added, s.name.content);
		obj_addsym(symtab, strtab, s.name.content, s.offset, s.size,
		           Elf32_ST_INFO(STB_LOCAL, s.type), textseci);
	}
	/* append data locals */
	for (int i = 0; i < state.syms.length; i++) {
		struct symbol s = *(struct symbol *)da_get(state.syms, i);
		if (s.section != PS_DATA) continue;
		if (ntslfind(state.globals, s.name.content) != -1) continue;
		added = ntsladd(added, s.name.content);
		obj_addsym(symtab, strtab, s.name.content, s.offset, s.size,
		           Elf32_ST_INFO(STB_LOCAL, s.type), dataseci);
	}

	/* GLOBALS */
	
	/* append absolute globals */
	for (int i = 0; i < state.syms.length; i++) {
		struct symbol s = *(struct symbol *)da_get(state.syms, i);
		if (s.section != PS_ABS) continue;
		if (ntslfind(state.globals, s.name.content) == -1) continue;
		added = ntsladd(added, s.name.content);
		obj_addsym(symtab, strtab, s.name.content, s.offset, s.size,
		           Elf32_ST_INFO(STB_GLOBAL, s.type), SHN_ABS);
	}
	/* append text globals */
	for (int i = 0; i < state.syms.length; i++) {
		struct symbol s = *(struct symbol *)da_get(state.syms, i);
		if (s.section != PS_TEXT) continue;
		if (ntslfind(state.globals, s.name.content) == -1) continue;
		added = ntsladd(added, s.name.content);
		obj_addsym(symtab, strtab, s.name.content, s.offset, s.size,
		           Elf32_ST_INFO(STB_GLOBAL, s.type), textseci);
	}
	/* append data globals */
	for (int i = 0; i < state.syms.length; i++) {
		struct symbol s = *(struct symbol *)da_get(state.syms, i);
		if (s.section != PS_DATA) continue;
		if (ntslfind(state.globals, s.name.content) == -1) continue;
		added = ntsladd(added, s.name.content);
		obj_addsym(symtab, strtab, s.name.content, s.offset, s.size,
		           Elf32_ST_INFO(STB_GLOBAL, s.type), dataseci);
	}

	/* SCREAM FOR THE UNKNOWABLE */

	for (int i = 0; i < state.syms.length; i++) {
		struct symbol s = *(struct symbol *)da_get(state.syms, i);
		if (s.expr) {
			fprintf(stderr,
			        "error: could not install symbol \"%s\"\n",
			        s.name.content);
			ntslfree(added);
			return 0;
		}
	}

	/* EXTERNALS */

	/* append UNK */
	for (int i = state.fixups.length - 1; i >= 0; i--) {
		struct fixup *f = (struct fixup *)da_get(state.fixups, i);
		if (!f || f->handled) continue;
		struct value v = evalexpr(f->expr, f->section, f->offset);
		if (v.sec == PS_UNK && v.fromsym) {
			if (ntslfind(added, v.symbol.content) != -1) continue;
			obj_addsym(symtab, strtab, v.symbol.content,
			           v.u.ivalue, 0,
			           Elf32_ST_INFO(STB_GLOBAL, STT_NOTYPE),
			           SHN_UNDEF);
			added = ntsladd(added, v.symbol.content);
		}
	}
	ntslfree(added);
	return 1;
}

static _Bool
addmacro(int node)
{
	if (!node || get(node)->type != APN_MACRO) return 0;
	struct string name = get(node)->c.svalue;
	if (findmacro(name) >= 0) {
		evalerr("attempting to override macro", node);
		fprintf(stderr, "\t%s\n", name.content);
		return 0;
	}
	return da_append(&state.macros, &node);
}

static int
findmacro(struct string str)
{
	for (int i = 0; i < state.macros.length; i++) {
		int n = *(int*)da_get(state.macros, i);
		if (!n) continue;
		struct string s = get(n)->c.svalue;
		if (!strncmp(str.content, s.content, s.length + 1)) return i;
	}
	return -1;
}

static void
evalerrdirective(int node)
{
	if (!node || get(node)->type != APN_INSTRUCTION) return;
	struct node *arglist = get(get(node)->b.arglist);
	int arg;
	switch (apn_arglen(arglist)) {
	case 0:
		evalerr(".error", node);
		break;
	case 1:
		arg = apn_arg(arglist, 0);
		if (!arg || get(arg)->type != APN_ARG_STRING) {
			evalerr("error processing .error", node);
			break;
		}
		evalerr(get_str(arg), node);
		break;
	default:
		evalerr("error processing .error", node);
		break;
	}
}

static _Bool
evalwarndirective(int node)
{
	if (!node || get(node)->type != APN_INSTRUCTION) return 0;
	struct node *arglist = get(get(node)->b.arglist);
	int arg;
	switch (apn_arglen(arglist)) {
	case 0:
		evalwarn(".warning", node);
		break;
	case 1:
		arg = apn_arg(arglist, 0);
		if (!arg || get(arg)->type != APN_ARG_STRING) {
			evalerr("error processing .warning", node);
			return 0;
		}
		evalwarn(get_str(arg), node);
		break;
	default:
		evalerr("error processing .warning", node);
		return 0;
	}
	return 1;
}

static struct node *
get(int i)
{
	if (!i) return NULL;
	return da_get(apn_arena, i);
}

static struct node *
get_arg(int i, int argi)
{
	return get(apn_arg(get(i), argi));
}

static struct node *
get_args(int i)
{
	return get(get(i)->b.arglist);
}

static char *
get_str(int i)
{
	return da_get(apn_carena, get(i)->c.ntstring);
}
