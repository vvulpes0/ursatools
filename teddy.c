#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "dynarr.h"
#include "version.h"

#define TOOLNAME "teddy"

enum teddy_flag {
	TF_PRINTHELP = 1,
	TF_PRINTVERSION = 2,
	TF_AUTORUN = 4,
	TF_AUTOQUIT = 8,
	TF_DIE = 2048,
};

enum break_action {
	BA_READ  = 1,
	BA_WRITE = 2,
	BA_EXEC  = 4,
};

enum region {
	RE_UNKNOWN,
	RE_TEXT,
	RE_DATA,
	RE_ABS,
};

enum symkind {
	SK_UNKNOWN,
	SK_FUNCTION,
	SK_OBJECT,
};

struct symbol {
	unsigned long address;
	unsigned long size;
	int region;
	int kind;
	int fnamei;
	int namei;
	_Bool global;
};

struct map {
	struct dynarr symbols;
	struct dynarr nameblock;
};

struct breakpoint {
	unsigned long location;
	int actions;
};

struct machine {
	struct map map;
	char *text;
	char *data;
	unsigned long textsize;
	unsigned long datasize;
	unsigned long registers[17]; /* r0 - rF, then sr */
	_Bool isloaded;
};

static void printhelp(FILE *file);
static void printversion(void);
static void repl(struct machine *, struct dynarr *, int, char **);

static void addbreak(struct machine const *, struct dynarr *, char **, int);
static void delbreak(struct dynarr *, char **);
static void disassemble(struct machine *, char **);
static void dump(struct machine *, char **);
static void listbreaks(struct machine const *, struct dynarr);
static _Bool load(struct machine *, char const *);
static _Bool memory(struct machine *, char **);
static void out(struct machine *, struct dynarr *);
static void over(struct machine *, struct dynarr *);
static void run(struct machine *, struct dynarr *);
static void showmap(struct machine *);
static _Bool showregs(struct machine *, char **);
static int step(struct machine *, struct dynarr *);
static void to(struct machine *, struct dynarr *, char **);
static _Bool writereg(struct machine *, char **);

static void help(char **);
static void helpbreakpoint(char **);
static void helpbreakpointdel(char **);
static void helpbreakpointlist(char **);
static void helpbreakpointset(char **);
static void helpcontinue(char **);
static void helpdisassemble(char **);
static void helpdump(char **);
static void helphelp(char **);
static void helpload(char **);
static void helpmap(char **);
static void helpmemory(char **);
static void helpmemoryread(char **);
static void helpmemorywrite(char **);
static void helpnext(char **);
static void helpout(char **);
static void helpregister(char **);
static void helpregisterread(char **);
static void helpregisterwrite(char **);
static void helpreset(char **);
static void helpquit(char **);
static void helpstep(char **);
static void helpsubcommands(void);
static void helpto(char **);
static void helpversion(char **);
static void helpwatchpoint(char **);
static void helpwatchpointdel(char **);
static void helpwatchpointlist(char **);
static void helpwatchpointset(char **);

static _Bool athalt(struct machine *);
static _Bool cond(struct machine *);
static void context(struct machine *);
static void emitdisassembly
	(struct machine const *, unsigned long, unsigned long);
static char const * error(void);
static _Bool evalexpr(struct machine const *, char **, unsigned long *);
static int getsymi(struct machine const *, char const *);
static void hexdump(struct machine *, int, unsigned long, unsigned long);
static int hexvalue(char);
static unsigned int instruction(struct machine const *, unsigned long);
static int locatefunc(struct machine const *, unsigned long);
static int locatesymi(struct machine const *, unsigned long, enum region);
static _Bool logiread(FILE *, char *, unsigned long);
static _Bool mapread(FILE *, struct map *);
static _Bool memread(struct machine const *, char **);
static _Bool memwrite(struct machine const *, char **);
static void needload(void);
static _Bool readsnum(char const *, int *);
static char const * regname(int);
static int regnum(char const *);
static void runto(struct machine *, struct dynarr *, _Bool, unsigned long);
static char ** words(char *);
static char const * warn(void);

static void
printhelp(FILE *file)
{
	fprintf(file,
	        "usage: " TOOLNAME
	        " [-hqrv?] [-d size] [-t size] [prefix]\n");
	fprintf(file,
	        "\t-d size  set data memory size (default 64K)\n");
	fprintf(file, "\t-h, -?   print this help and exit\n");
	fprintf(file, "\t-q       automatically quit when program halts\n");
	fprintf(file,
	        "\t-r       automatically run initially loaded program\n");
	fprintf(file,
	        "\t-t size  set instruction memory size (default 64K)\n");
	fprintf(file, "\t-v       print version and exit\n");
}

static void
printversion(void) {
	puts("teddy (URSA) " URSA_VERSION);
}

int
main(int argc, char *argv[])
{
	int flags = 0;
	int textsize = 64*1024;
	int datasize = 64*1024;

	_Bool process = 1;
	int retval = 0;
	FILE *helpfile = stdout;
	argv++; argc--;
	while (process && argc && *argv && **argv == '-') {
		char const *str = *(argv++); argc--;
		if (str[1] == '\0') {
			fprintf(stderr,
			        "error: %s: cannot use standard input\n",
			        TOOLNAME);
			return 1;
		}
		int i = 1;
		while (str && str[i]) {
			char c = str[i++];
			switch (c) {
			case '-':
				process = 0;
				if (str[i]) {
					flags |= TF_PRINTHELP;
					helpfile = stderr;
					retval = 1;
					i = strlen(str);
				}
				break;
			case 'd':
			case 't':
				if (str[i] == '\0') {
					str = *(argv++); argc--;
					i = 0;
				}
				if (!readsnum(str + i, (c == 'd')?
				                   &datasize : &textsize)) {
					fprintf(stderr,
					        TOOLNAME ": "
					        "error: "
					        "bad number: %s\n",
					        str? str : "(null)");
					retval = 1;
				}
				i = str? strlen(str) : 0;
				break;
			case 'h':
			case '?':
				flags |= TF_PRINTHELP;
				break;
			case 'r':
				flags |= TF_AUTORUN;
				break;
			case 'q':
				flags |= TF_AUTOQUIT;
				break;
			case 'v':
				flags |= TF_PRINTVERSION;
				break;
			default:
				flags |= TF_PRINTHELP;
				helpfile = stderr;
				retval = 1;
			}
		}
	}
	if (argc > 1) {
		flags |= TF_PRINTHELP;
		helpfile = stderr;
		retval = 1;
	}

	if (flags & TF_PRINTVERSION) {
		printversion();
		if (!(flags & TF_PRINTHELP)) return retval;
	}
	if (flags & TF_PRINTHELP) {
		printhelp(helpfile);
		return retval;
	}

	if (textsize <= 0 || (textsize&(textsize-1)) != 0) {
		fprintf(stderr,
		        "error: " TOOLNAME ": "
		        "bad text size %d\n", textsize);
		return 1;
	}
	if (datasize <= 0 || (datasize&(datasize-1)) != 0) {
		fprintf(stderr,
		        "error: " TOOLNAME ": "
		        "bad data size %d\n", datasize);
		return 1;
	}

	char *memories = malloc(textsize + datasize);
	struct machine m = {
		{da_new(sizeof(struct symbol)), da_new(1)},
		memories, memories + textsize,
		textsize, datasize,
		{0},
		0,
	};
	if (argc) {
		load(&m, *argv);
	}
	struct dynarr breaks = da_new(sizeof(struct breakpoint));
	if (flags&TF_AUTORUN) {
		if (m.isloaded) {
			run(&m, &breaks);
		} else {
			fprintf(stderr,
			        "%sautorun set but no program loaded\n",
			        warn());
		}
	}
	if (!m.isloaded || !(flags&TF_AUTOQUIT) || !athalt(&m)) {
		repl(&m, &breaks, flags, NULL);
	} else {
		char *w = "!";
		char *ws[] = {w, NULL};
		showregs(&m, ws);
	}
	da_free(m.map.symbols);
	da_free(m.map.nameblock);
	da_free(breaks);
	free(memories);
	return retval;
}

/* Repl ***************************************************************/

static void
repl(struct machine *m, struct dynarr *breaks, int flags, char **oldws)
{
	/* emit a prompt */
	fprintf(stderr, "\033[0;34mteddy>\033[0m ");

	/* read a line */
	struct dynarr line = da_new(1);
	char c = 0;
	do {
		c = getc(stdin);
		if (c != '\r' && !feof(stdin)) da_append(&line, &c);
	} while (c != '\n' && !feof(stdin));
	da_append(&line, "\0");

	/* split it into words */
	char *buf = (char *)line.content;
	char **ws = words(buf);
	if (!ws) {
		ws = oldws;
	} else {
		free(oldws);
	}
	da_free(line);

	if (!ws && !feof(stdin)) {
		repl(m, breaks, flags, ws);
		return;
	}
	/* got a command: do it */
	if (feof(stdin) || !strcmp(*ws, "quit") || !strcmp(*ws, "q")) {
		free(ws);
		return;
	} else if (!strcmp(*ws, "b")) {
		if (!ws[1]) {
			fprintf(stderr,
				"%susage: breakpoint set <location>\n",
				error());
		} else {
			addbreak(m, breaks, ws + 1, BA_EXEC);
		}
	} else if (!strcmp(*ws, "breakpoint")) {
		if (!ws[1]) {
			fprintf(stderr,
			        "%susage: breakpoint (set|delete|list) ...\n",
			        error());
		} else if (!strcmp(ws[1], "set")) {
			if (!ws[2]) {
				fprintf(stderr,
				        "%susage: breakpoint set "
				        "<location>\n",
				        error());
			} else {
				addbreak(m, breaks, ws + 2, BA_EXEC);
			}
		} else if (!strcmp(ws[1], "list")) {
			listbreaks(m, *breaks);
		} else if (!strcmp(ws[1], "delete")) {
			if (!ws[2]) {
				fprintf(stderr,
				        "%susage: breakpoint delete "
				        "<index>\n",
				        error());
			} else {
				delbreak(breaks, ws + 2);
			}
		} else {
			fprintf(stderr,
			        "%susage: breakpoint (set|delete|list) ...\n",
			        error());
		}
	} else if (!strcmp(*ws, "bl")) {
		listbreaks(m, *breaks);
	} else if (!strcmp(*ws, "continue") || !strcmp(*ws, "c")) {
		run(m, breaks);
	} else if (!strcmp(*ws, "d")) {
		if (!ws[1]) {
			fprintf(stderr,
				"%susage: breakpoint delete <index>\n",
				error());
		} else {
			delbreak(breaks, ws + 1);
		}
	} else if (!strcmp(*ws, "disassemble") || !strcmp(*ws, "di")) {
		disassemble(m, ws + 1);
	} else if (!strcmp(*ws, "dump")) {
		dump(m, ws + 1);
	} else if (  !strcmp(*ws, "help")
	          || !strcmp(*ws, "h")
	          || !strcmp(*ws, "?")) {
		help(ws + 1);
	} else if (!strcmp(*ws, "load") || !strcmp(*ws, "l")) {
		int i = 0;
		for (char **p = ws; *p; p++) i++;
		if (i != 2) {
			fprintf(stderr, "%susage: load prefix\n", error());
		} else {
			load(m, ws[1]);
			breaks->length = 0;
		}
	} else if (!strcmp(*ws, "map") || !strcmp(*ws, "m")) {
		showmap(m);
	} else if (!strcmp(*ws, "memory")) {
		memory(m, ws + 1);
	} else if (!strcmp(*ws, "next") || !strcmp(*ws, "n")) {
		if (m->isloaded) {
			over(m, breaks);
			context(m);
		} else {
			needload();
		}
	} else if (!strcmp(*ws, "out") || !strcmp(*ws, "o")) {
		if (m->isloaded) {
			out(m, breaks);
			context(m);
		} else {
			needload();
		}
	} else if (!strcmp(*ws, "register")) {
		if (!ws[1]) {
			fprintf(stderr, "%susage: register (read|write) ...\n",
			        error());
		} else if (!strcmp(ws[1], "read")) {
			showregs(m, ws + 2);
		} else if (!strcmp(ws[1], "write") && ws[2] && ws[3]) {
			writereg(m, ws + 2);
		} else {
			fprintf(stderr, "%susage: register (read|write) ...\n",
			        error());
		}
	} else if (!strcmp(*ws, "reset")) {
		for (int i = 0; i < 17; i++) {
			m->registers[i] = 0;
		}
	} else if (!strcmp(*ws, "rm")) {
		memread(m, ws + 1);
	} else if (!strcmp(*ws, "rr")) {
		showregs(m, ws + 1);
	} else if (!strcmp(*ws, "step") || !strcmp(*ws, "s")) {
		if (m->isloaded) {
			step(m, breaks);
			context(m);
		} else {
			needload();
		}
	} else if (!strcmp(*ws, "to")) {
		if (m->isloaded) {
			to(m, breaks, ws + 1);
			context(m);
		} else {
			needload();
		}
	} else if (!strcmp(*ws, "version")) {
		printversion();
	} else if (!strcmp(*ws, "w")) {
		if (!ws[1]) {
			fprintf(stderr,
				"%susage: watchpoint set <location>\n",
				error());
		} else {
			addbreak(m, breaks, ws + 1, BA_WRITE);
		}
	} else if (!strcmp(*ws, "watchpoint")) {
		if (!ws[1]) {
			fprintf(stderr,
			        "%susage: watchpoint (set|delete|list) ...\n",
			        error());
		} else if (!strcmp(ws[1], "set")) {
			if (!ws[2]) {
				fprintf(stderr,
				        "%susage: watchpoint set "
				        "<location>\n",
				        error());
			} else {
				addbreak(m, breaks, ws + 2, BA_WRITE);
			}
		} else if (!strcmp(ws[1], "list")) {
			listbreaks(m, *breaks);
		} else if (!strcmp(ws[1], "delete")) {
			if (!ws[2]) {
				fprintf(stderr,
				        "%susage: watchpoint delete "
				        "<index>\n",
				        error());
			} else {
				delbreak(breaks, ws + 2);
			}
		} else {
			fprintf(stderr,
			        "%susage: watchpoint (set|delete|list) ...\n",
			        error());
		}
	} else if (!strcmp(*ws, "wm")) {
		memwrite(m, ws + 1);
	} else if (!strcmp(*ws, "wr")) {
		writereg(m, ws + 1);
	} else if (!strcmp(*ws, "xd") || !strcmp(*ws, "xt")) {
		char *oldw0 = *ws;
		char *w = strcmp(*ws, "xd")? "text" : "data";
		ws[0] = w;
		dump(m, ws);
		ws[0] = oldw0;
	} else {
		fprintf(stderr, "%sunknown command \"%s\"\n", error(), *ws);
	}
	if ((flags&TF_AUTOQUIT) && m->isloaded && athalt(m)) {
		free(ws);
		char *w = "!";
		char *ww[] = {w, NULL};
		showregs(m, ww);
		return;
	}
	repl(m, breaks, flags, ws);
}

/* Actions ************************************************************/

static void
addbreak(struct machine const *m, struct dynarr *breaks, char **ws, int why)
{
	if (!m || !breaks) return;
	unsigned long out = 0;
	if (!evalexpr(m, ws, &out)) return;
	struct breakpoint b = {out, why};
	fprintf(stderr, "added breakpoint %d\n", breaks->length);
	da_append(breaks, &b);
}

static void
delbreak(struct dynarr *breaks, char **ws)
{
	if (!breaks || !ws || !*ws) return;
	char *endptr = NULL;
	unsigned long i = strtoul(*ws, &endptr, 0);
	if (!endptr || *endptr != '\0') {
		fprintf(stderr, "%sbad index \"%s\"\n", error(), *ws);
		return;
	}
	if (i < breaks->length) {
		struct breakpoint *b = (struct breakpoint *)da_get(*breaks, i);
		if (!b) return;
		if (b->actions) {
			fprintf(stderr, "deleted breakpoint %lu\n", i);
			b->actions = 0;
			return;
		}
	}
	fprintf(stderr, "%sno such breakpoint %lu\n", error(), i);
}

static void
disassemble(struct machine *m, char **ws)
{
	if (!m || !ws) return;
	if (!m->isloaded) { needload(); return; }
	/* default to current location with 10 instructions */
	unsigned long loc = m->registers[15];
	unsigned long size = 10;
	if (!*ws) {
		/* emit entire current function if found and no args */
		int symi = locatefunc(m, loc);
		if (symi >= 0) {
			struct symbol sym =
				*(struct symbol*)da_get(m->map.symbols, symi);
			loc = sym.address;
			if (sym.size) {
				/* sym size is bytes, want instructions */
				size = sym.size / 2;
			} else {
				size += m->registers[15] - loc;
			}
		}
	} else {
		char* arr[4] = {NULL, NULL, NULL, NULL};
		arr[0] = *ws;
		int used = 1;
		if (ws[1] && (ws[1][0] == '+' || ws[1][0] == '-')) {
			arr[1] = ws[1];
			used++;
			if (ws[2]) {
				arr[2] = ws[2];
				used++;
			}
		}
		if (!evalexpr(m, arr, &loc)) return;
		if (ws[used]) {
			if (!evalexpr(m, ws + used, &size)) return;
		} else {
			int symi = locatefunc(m, loc);
			if (symi >= 0) {
				struct symbol sym =
					*(struct symbol*)
					da_get(m->map.symbols, symi);
				if (sym.address == loc && sym.size != 0) {
					size = sym.size / 2;
				}
			}
		}
	}
	emitdisassembly(m, loc, size);
}

static void
dump(struct machine *m, char **ws)
{
	static char const * const usage
		= "%susage: dump <region> <location> <size>\n";
	if (!m || !ws) return;
	if (!m->isloaded) { needload(); return; }
	if (!*ws || !ws[1]) {
		fprintf(stderr, usage, error());
		return;
	}
	int region = RE_UNKNOWN;
	if (!strcmp(*ws, "text")) {
		region = RE_TEXT;
	} else if (!strcmp(*ws, "data")) {
		region = RE_DATA;
	} else {
		fprintf(stderr, usage, error());
		return;
	}
	ws++;
	/* default to current location with 10 instructions */
	unsigned long loc = 0;
	unsigned long size = 0;
	char* arr[4] = {NULL, NULL, NULL, NULL};
	arr[0] = *ws;
	int used = 1;
	if (ws[1] && (ws[1][0] == '+' || ws[1][0] == '-')) {
		arr[1] = ws[1];
		used++;
		if (ws[2]) {
			arr[2] = ws[2];
			used++;
		}
	}
	if (!evalexpr(m, arr, &loc)) return;
	if (!ws[used]) {
		fprintf(stderr, usage, error());
		return;
	}
	if (ws[used]) {
		if (!evalexpr(m, ws + used, &size)) return;
	}
	hexdump(m, region, loc, size);
}

static void
help(char **ws)
{
	if (!ws || !*ws) {
		puts(
"Commands:\n"
"  breakpoint   add, list, or remove breakpoints\n"
"  continue     run until halt\n"
"  disassemble  display human-readable code listing\n"
"  dump         show memory as hex dump\n"
"  help         list commands or give command details\n"
"  load         read in a program\n"
"  map          display symbol table\n"
"  memory       show or modify data memory contents\n"
"  next         step until the immediately following location\n"
"  out          step until return\n"
"  quit         exit the teddy debugger\n"
"  register     show or modify register(s)\n"
"  reset        restore initial system state\n"
"  step         execute one instruction\n"
"  to           run until a specified location is reached\n"
"  version      print the program version\n"
"  watchpoint   add, list, or remove watchpoints\n"
"\n"
"Shorthand:\n"
"  ?   help                     o   out\n"
"  b   breakpoint set           q   quit\n"
"  bl  breakpoint list          rm  memory read\n"
"  c   continue                 rr  register read\n"
"  d   breakpoint delete        s   step\n"
"  di  disassemble              w   watchpoint set\n"
"  h   help                     wm  memory write\n"
"  l   load                     wr  register write\n"
"  m   map                      xd  dump data\n"
"  n   next                     xt  dump text\n"
"\n"
"For more information about a command, use\n"
"  help <command>");
	} else if (  !strcmp(*ws, "help")
	          || !strcmp(*ws, "h")
	          || !strcmp(*ws, "?")) {
		helphelp(ws + 1);
	} else if (!strcmp(*ws, "breakpoint")) {
		helpbreakpoint(ws + 1);
	} else if (!strcmp(*ws, "b")) {
		helpbreakpointset(ws + 1);
	} else if (!strcmp(*ws, "bl")) {
		helpbreakpointlist(ws + 1);
	} else if (!strcmp(*ws, "continue") || !strcmp(*ws, "c")) {
		helpcontinue(ws + 1);
	} else if (!strcmp(*ws, "d")) {
		helpbreakpointdel(ws + 1);
	} else if (!strcmp(*ws, "disassemble") || !strcmp(*ws, "di")) {
		helpdisassemble(ws + 1);
	} else if (!strcmp(*ws, "dump")
	           || !strcmp(*ws, "xd")
	           || !strcmp(*ws, "xt")) {
		helpdump(ws + 1);
	} else if (!strcmp(*ws, "load") || !strcmp(*ws, "l")) {
		helpload(ws + 1);
	} else if (!strcmp(*ws, "map") || !strcmp(*ws, "m")) {
		helpmap(ws + 1);
	} else if (!strcmp(*ws, "memory")) {
		helpmemory(ws + 1);
	} else if (!strcmp(*ws, "next") || !strcmp(*ws, "n")) {
		helpnext(ws + 1);
	} else if (!strcmp(*ws, "out") || !strcmp(*ws, "o")) {
		helpout(ws + 1);
	} else if (!strcmp(*ws, "register")) {
		helpregister(ws + 1);
	} else if (!strcmp(*ws, "reset")) {
		helpreset(ws + 1);
	} else if (!strcmp(*ws, "rm")) {
		helpmemoryread(ws + 1);
	} else if (!strcmp(*ws, "rr")) {
		helpregisterread(ws + 1);
	} else if (!strcmp(*ws, "step") || !strcmp(*ws, "s")) {
		helpstep(ws + 1);
	} else if (!strcmp(*ws, "to")) {
		helpto(ws + 1);
	} else if (!strcmp(*ws, "version")) {
		helpversion(ws + 1);
	} else if (!strcmp(*ws, "w")) {
		helpwatchpointset(ws + 1);
	} else if (!strcmp(*ws, "watchpoint")) {
		helpwatchpoint(ws + 1);
	} else if (!strcmp(*ws, "wm")) {
		helpmemorywrite(ws + 1);
	} else if (!strcmp(*ws, "wr")) {
		helpregisterwrite(ws + 1);
	} else if (!strcmp(*ws, "quit") || !strcmp(*ws, "q")) {
		helpquit(ws + 1);
	} else {
		fprintf(stderr, "%sunknown command \"%s\"\n", error(), *ws);
	}
	puts("");
}

static void helpbreakpoint(char **ws)
{
	if (!ws || !*ws) {
		puts(
"Add, list or remove breakpoints\n"
"\n"
"Syntax: breakpoint (delete|list|set) ...\n"
"\n"
"Subcommands:\n"
"  delete  remove a breakpoint\n"
"  list    list breakpoints\n"
"  set     add a breakpoint");
		helpsubcommands();
	} else if (!strcmp(*ws, "delete")) {
		helpbreakpointdel(ws + 1);
	} else if (!strcmp(*ws, "list")) {
		helpbreakpointlist(ws + 1);
	} else if (!strcmp(*ws, "set")) {
		helpbreakpointset(ws + 1);
	} else {
		fprintf(stderr, "%sunknown subcommand \"%s\"\n",
		        error(), *ws);
	}
}

static void helpbreakpointdel(char **ws)
{
	puts(
"Remove a breakpoint\n"
"\n"
"Syntax: breakpoint delete <index>\n"
"\n"
"Remove the breakpoint at the given numeric index.");
}

static void helpbreakpointlist(char **ws)
{
	puts(
"List breakpoints\n"
"\n"
"Syntax: breakpoint list\n"
"\n"
"Display a list of active breakpoints. The output spans three columns:\n"
"the index, the location, and, potentially, a symbol or symbol plus offset\n"
"naming that location.");
}

static void helpbreakpointset(char **ws)
{
	puts(
"Add a breakpoint\n"
"\n"
"Syntax: breakpoint set <location>\n"
"\n"
"Add a new breakpoint at the smallest index that has not yet been used.\n"
"The location may be the name of a symbol, a numeric value, or a symbol\n"
"plus or minus a numeric value. It can come in any of the following forms;\n"
"the spaces around the operators are necessary.\n"
"\n"
"  <name>\n"
"  <number>\n"
"  <name> + <number>\n"
"  <name> - <number>\n"
"\n"
"The <name> may be qualified by a filename like <file:name>.\n"
"Using \".\" as a name refers to the current location.");
}

static void
helpcontinue(char **ws) {
	puts(
"Run until halt\n"
"\n"
"Syntax: continue\n"
"\n"
"Continue running instructions until a halt instruction like one of either\n"
"'b .' or 'mov pc, pc' is executed, or until a breakpoint is triggered,\n"
"whichever comes first.");
}

static void
helpdisassemble(char **ws)
{
	puts(
"Display human-readable code listing\n"
"\n"
"Syntax: disassemble [<location> [<count>]]\n"
"\n"
"Convert <count> instructions into assembly language, starting from \n"
"<location>, into human-readable code and print it. The <location> can be\n"
"an expression of the form accepted for setting breakpoints:\n"
"\n"
"  <name>             a symbol name\n"
"  <number>           an integer value\n"
"  <name> + <number>  a symbol with positive offset\n"
"  <name> - <number>  a symbol with negative offset\n"
"\n"
"Symbol names may be qualified by object, as <file:name>.\n"
"\n"
"If the <location> is unspecified, the default is to find and emit the\n"
"current function. If no symbol table is loaded, then the current address\n"
"is used instead. The default <count> is the length of the function if it\n"
"can be found, else 10.");
}

static void
helpdump(char **ws)
{
	puts(
"Show memory as hex dump\n"
"\n"
"Syntax: dump <region> <location> <size>\n"
"\n"
"Print the contents of memory in canonical hex dump format. The <region>\n"
"may be either \"text\" for instruction memory or \"data\" for data memory.\n"
"The <location> is an expression consisting of a symbol's <name>, a <number>,\n"
"or a combination of the form <name> + <number> or <name> - <number>.\n"
"In the latter cases, the spaces are necessary.\n"
"\n"
"No checks are performed to ensure that the region being dumped matches\n"
"the region of a named symbol.");
}

static void
helphelp(char **ws)
{
	puts("List commands or give command details");
	puts("");
	puts("Syntax: help [<command>]");
	return;
}

static void
helpload(char **ws)
{
	puts(
"Read in a program\n"
"\n"
"Syntax: load <prefix>\n"
"\n"
"Attempt to load the text segment of a program from \"<prefix>.lcode\"\n"
"and to load the data segment of that program from \"<prefix>.ldata\"\n"
"If either cannot be read for any reason, including if it does not exist,\n"
"then a warning is emitted and the associated memory region is zero-filled.\n"
"\n"
"Also, attempt to load symbols from \"<prefix>.map\". No warning is emitted\n"
"for a missing symbol file.");
}

static void
helpmap(char **ws)
{
	puts(
"Display symbol table\n"
"\n"
"Syntax: map\n"
"\n"
"Symbols are listed in a five-column format, including their region, type,\n"
"address, size, and name, respectively. If the abbreviation for the region\n"
"is lowercase, the symbol is local. If uppercase, it is global.\n"
"\n"
"Regions:\n"
"  A  absolute (not in memory)\n"
"  D  data memory\n"
"  T  text (instruction memory)\n"
"\n"
"Types:\n"
"  f  function\n"
"  o  object\n"
"  ?  unknown\n"
"\n"
"Symbols are grouped by their file of origin; the file is introduced by\n"
"a line starting with space and at-sign:\n"
" @ <filename>");
	return;
}

static void
helpmemory(char **ws)
{
	if (!ws || !*ws) {
		puts(
"Show or modify data memory contents\n"
"\n"
"Syntax: memory (read|write) ...\n"
"\n"
"Subcommands:\n"
"  read   show data memory contents\n"
"  write  modify data memory contents");
		helpsubcommands();
		return;
	}
	if (!strcmp(*ws, "read")) {
		helpmemoryread(ws + 1);
	} else if (!strcmp(*ws, "write")) {
		helpmemorywrite(ws + 1);
	} else {
		fprintf(stderr, "%sunknown subcommand \"%s\"\n",
		        error(), *ws);
	}
}

static void
helpmemoryread(char **ws)
{
	puts(
"Show data memory contents\n"
"\n"
"Syntax: memory read <format> <location>\n"
"\n"
"Display a value in memory according to the given <format>:\n"
"\n"
"  byte    Unsigned 8-bit decimal value   (b)\n"
"  hword   Unsigned 16-bit decimal value  (hw)\n"
"  word    Unsigned 32-bit decimal value  (w)\n"
"  sbyte   Signed 8-bit decimal value     (sb)\n"
"  shword  Signed 16-bit decimal value    (shw)\n"
"  sword   Signed 32-bit decimal value    (sw)\n"
"  xbyte   8-bit hexadecimal value        (xb)\n"
"  xhword  16-bit hexadecimal value       (xhw)\n"
"  xword   32-bit hexadecimal value       (xw)\n"
"\n"
"The <location> may be any of the following:\n"
"\n"
"  <name>             the name of a symbol\n"
"  <number>           an integer constant\n"
"  <name> + <number>  a symbol with positive offset\n"
"  <name> - <number>  a symbol with negative offset\n"
"\n"
"In the latter cases, the spaces around the operator are required.\n"
"For 16- and 32-bit values, the location is masked to align to\n"
"the appropriate boundary.");
}

static void
helpmemorywrite(char **ws)
{
	puts(
"Modify data memory contents\n"
"\n"
"Syntax: memory write <format> <location> <value>\n"
"\n"
"Overwrite a value in memory according to the given <format>:\n"
"\n"
"  byte   8 bits, one byte     (b)\n"
"  hword  16 bits, two bytes   (hw)\n"
"  word   32 bits, four bytes  (w)\n"
"\n"
"The <location> and new <value> may be any of the following:\n"
"\n"
"  <name>             the name of a symbol\n"
"  <number>           an integer constant\n"
"  <name> + <number>  a symbol with positive offset\n"
"  <name> - <number>  a symbol with negative offset\n"
"\n"
"In the latter cases, the spaces around the operator are required.\n"
"For 16 and 32-bit values, the location is masked to align to\n"
"the appropriate boundary.");
}

static void
helpreset(char **ws)
{
	puts("Restore initial system state");
	puts("");
	puts("Syntax: reset");
	puts("");
	puts("Set all register values to zero. To reinitialize data memory");
	puts("you should load the program again.");
}

static void
helpquit(char **ws)
{
	puts("Exit the teddy debugger");
	puts("");
	puts("Syntax: quit");
}

static void
helpnext(char **ws)
{
	puts(
"Step until the immediately following location\n"
"\n"
"Syntax: next\n"
"\n"
"Continue running instructions until one of the following events occurs:\n"
"\n"
"* The program counter reaches the next location in instruction memory\n"
"* A breakpoint is triggered, or\n"
"* A halt instruction like 'b .' or 'mov pc, pc' is executed.");
}

static void
helpout(char **ws)
{
	puts(
"Step until return\n"
"\n"
"Syntax: out\n"
"\n"
"Continue running instructions until one of the following events occurs:\n"
"\n"
"* Control reaches the address currently in register rp\n"
"* A breakpoint is triggered, or\n"
"* A halt instruction like 'b .' or 'mov pc, pc' is executed.");
}

static void
helpregister(char **ws)
{
	if (!ws || !*ws) {
		puts(
"Show or modify register(s)\n"
"\n"
"Syntax: register (read|write) ...\n"
"\n"
"Subcommands:\n"
"  read   show register values; if none specified, show all\n"
"  write  modify the value of a register");
		helpsubcommands();
		return;
	}
	if (!strcmp(*ws, "read")) {
		helpregisterread(ws + 1);
	} else if (!strcmp(*ws, "write")) {
		helpregisterwrite(ws + 1);
	} else {
		fprintf(stderr, "%sunknown subcommand \"%s\"\n",
		        error(), *ws);
	}
}

static void
helpregisterread(char **ws)
{
	puts(
"Show register values; if none specified, show all\n"
"\n"
"Syntax: register read [<name>] ...\n"
"\n"
"The following case-insensitive registers are accepted:\n"
"\n"
"  r0  r1  r2  r3  r4  r5  r6  r7  r8  r9  r10  rT  fp  sp  rp  pc  sr\n"
"\n"
"Some have accepted case-insensitive synonyms:\n"
"\n"
"  r10 = ra         fp = r12 = rc    rp = r14 = re\n"
"  rT = r11 = rb    sp = r13 = rd    pc = r15 = rf\n"
"\n"
"The original form is shown in the output. Additionally, two other shorthand\n"
"selectors are accepted. These are:\n"
"\n"
"  *  all registers\n"
"  !  all registers with nonzero value");
}

static void
helpregisterwrite(char **ws)
{
	puts(
"Modify the value of a register\n"
"\n"
"Syntax: register write <expression>\n"
"\n"
"The <expression> takes the same form as a valid location for the \n"
"\"breakpoint set\" command: it may be a <number>, a symbol's <name>,\n"
"a sum like <name> + <number> or a difference like <name> - <number>.\n"
"In the latter cases, the spaces are necessary. In general, the expression\n"
"cannot take a register's value, but \".\" has the value of pc.\n"
"\n"
"The following case-insensitive registers are accepted:\n"
"\n"
"  r0  r1  r2  r3  r4  r5  r6  r7  r8  r9  r10  rT  fp  sp  rp  pc  sr\n"
"\n"
"Some have accepted case-insensitive synonyms:\n"
"\n"
"  r10 = ra         fp = r12 = rc    rp = r14 = re\n"
"  rT = r11 = rb    sp = r13 = rd    pc = r15 = rf");
}

static void
helpstep(char **ws)
{
	puts("Execute one instruction");
	puts("");
	puts("Syntax: step");
}

static void
helpsubcommands(void)
{
	puts("");
	puts("For information about a subcommand, use");
	puts("  help <command> <subcommand>");
}

static void
helpto(char **ws)
{
	puts(
"Run until a specified location is reached\n"
"\n"
"Syntax: to <location>\n"
"\n"
"The <location> may be any of the following:\n"
"\n"
"  <name>             the name of a symbol\n"
"  <number>           an integer constant\n"
"  <name> + <number>  a symbol with positive offset\n"
"  <name> - <number>  a symbol with negative offset\n"
"\n"
"No check is made to ensure that the symbol is in the text segment.\n"
"If a breakpoint is triggered or if the system halts before reaching\n"
"the target, this takes priority and execution is paused.");
}
static void
helpversion(char **ws)
{
	puts("Print the program version");
	puts("");
	puts("Syntax: version");
}

static void helpwatchpoint(char **ws)
{
	if (!ws || !*ws) {
		puts(
"Add, list or remove watchpoints\n"
"\n"
"Syntax: watchpoint (delete|list|set) ...\n"
"\n"
"A watchpoint is a special kind of breakpoint that triggers after\n"
"a memory location is written.\n"
"\n"
"Subcommands:\n"
"  delete  remove a watchpoint\n"
"  list    list watchpoints\n"
"  set     add a watchpoint");
		helpsubcommands();
	} else if (!strcmp(*ws, "delete")) {
		helpwatchpointdel(ws + 1);
	} else if (!strcmp(*ws, "list")) {
		helpwatchpointlist(ws + 1);
	} else if (!strcmp(*ws, "set")) {
		helpwatchpointset(ws + 1);
	} else {
		fprintf(stderr, "%sunknown subcommand \"%s\"\n",
		        error(), *ws);
	}
}

static void helpwatchpointdel(char **ws)
{
	puts("Remove a watchpoint");
	puts("");
	puts("Syntax: watchpoint delete <index>");
	puts("");
	puts("Remove the watchpoint at the given numeric index.");
}

static void helpwatchpointlist(char **ws)
{
	puts(
"List watchpoints\n"
"\n"
"Syntax: watchpoint list\n"
"\n"
"Display a list of active watchpoints. The output spans three columns:\n"
"the index, the location, and, potentially, a symbol or symbol plus offset\n"
"naming that location.");
}

static void helpwatchpointset(char **ws)
{
	puts(
"Add a watchpoint\n"
"\n"
"Syntax: watchpoint set <location>\n"
"\n"
"Add a new watchpoint at the smallest index that has not yet been used.\n"
"The location may be the name of a symbol, a numeric value, or a symbol\n"
"plus or minus a numeric value. It can come in any of the following forms;\n"
"the spaces around the operators are necessary.\n"
"\n"
"  <name>\n"
"  <number>\n"
"  <name> + <number>\n"
"  <name> - <number>\n"
"\n"
"The <name> may be qualified by a filename like <file:name>.");
}

static void
listbreaks(struct machine const *m, struct dynarr breaks)
{
	if (!m) return;
	for (int i = 0; i < breaks.length; i++) {
		struct breakpoint b = *(struct breakpoint *)da_get(breaks, i);
		if (!b.actions) continue;
		printf("%8d:    %08lx", i, b.location);
		int symi = locatesymi(m, b.location,
		                      (b.actions&BA_EXEC)? RE_TEXT : RE_DATA);
		if (symi >= 0) {
			struct dynarr symbols = m->map.symbols;
			char *names = m->map.nameblock.content;
			struct symbol sym =
				*(struct symbol *)da_get(symbols, symi);
			printf("    (%s:%s",
			       names + sym.fnamei, names + sym.namei);
			if (sym.address == b.location) {
				printf(")");
			} else {
				printf(" + %lu)", b.location - sym.address);
			}
		}
		printf("\n");
	}
}

static _Bool
load(struct machine *m, char const *prefix)
{
	if (!prefix || !m) return 0;
	m->isloaded = 0;
	memset(m->text, 0, m->textsize);
	memset(m->data, 0, m->datasize);
	int n = strlen(prefix);
	char *buf = malloc(n + 7);
	if (!buf) return 0;

	/* load text */
	strncpy(buf, prefix, n + 1);
	strcat(buf, ".lcode");
	FILE *f = fopen(buf, "r");
	if (f) {
		_Bool status = logiread(f, m->text, m->textsize);
		fclose(f);
		if (!status) {
			free(buf);
			return 0;
		}
		m->isloaded = 1;
	} else {
		fprintf(stderr, "%sfailed to load text segment\n", warn());
		perror(NULL);
	}
	if (f) { fclose(f); f = NULL; }

	/* load data */
	strncpy(buf, prefix, n + 1);
	strcat(buf, ".ldata");
	f = fopen(buf, "r");
	if (f) {
		m->isloaded = 0;
		_Bool status = logiread(f, m->data, m->datasize);
		fclose(f);
		if (!status) {
			free(buf);
			return 0;
		}
		m->isloaded = 1;
	} else {
		fprintf(stderr, "%sfailed to load data segment\n", warn());
		perror(NULL);
	}
	if (m->isloaded) {
		fprintf(stderr, "loaded \"%s\"\n", prefix);
	}

	/* load map */
	strncpy(buf, prefix, n + 1);
	strcat(buf, ".map");
	f = fopen(buf, "r");
	if (f) {
		mapread(f, &m->map);
		fclose(f);
	}
	free(buf);
	return m->isloaded;
}

static void
showmap(struct machine *m)
{
	if (!m || !m->map.symbols.length || !m->map.nameblock.length) {
		fprintf(stderr, "%sno symbol table loaded\n", warn());
		return;
	}
	if (!m->isloaded) { needload(); return; }
	int lastfnamei = 0;
	struct map map = m->map;
	for (int i = 0; i < map.symbols.length; i++) {
		struct symbol sym = *(struct symbol *)da_get(map.symbols, i);
		if (sym.fnamei != lastfnamei) {
			lastfnamei = sym.fnamei;
			printf(" @ %s\n",
			       (char*)da_get(map.nameblock, sym.fnamei));
		}
		char regionchar = (sym.region == RE_ABS)?  'a' :
		                  (sym.region == RE_TEXT)? 't' :
		                  (sym.region == RE_DATA)? 'd' : '?';
		printf("%c %c %08lx %08lx %s\n",
		       (sym.global)? toupper(regionchar) : regionchar,
		       (sym.kind == SK_FUNCTION)? 'f' :
		       (sym.kind == SK_OBJECT)?   'o' : '?',
		       sym.address, sym.size,
		       (char*)da_get(map.nameblock, sym.namei));
	}
}

static _Bool
memory(struct machine *m, char **ws)
{
	if (!ws || !*ws) return 0;
	if (!strcmp(*ws, "read")) {
		return memread(m, ws + 1);
	} else if (!strcmp(*ws, "write")) {
		return memwrite(m, ws + 1);
	} else {
		fprintf(stderr, "%susage: memory (read|write) ...\n", error());
		return 0;
	}
	return 1;
}

static _Bool
showregs(struct machine *m, char **ws)
{
	if (!m || !ws) return 0;
	long which = *ws? 0 : 0x1FFFF;
	while (*ws) {
		if (!strcmp(*ws, "*")) {
			which = 0x1FFFF;
			ws++;
			continue;
		}
		if (!strcmp(*ws, "!")) {
			for (int i = 0; i < 16; i++) {
				if (m->registers[i] != 0) which |= 1<<i;
			}
			ws++;
			continue;
		}
		int r = regnum(*ws);
		char const *name = regname(r);
		if (!name) {
			fprintf(stderr, "%sbad register \"%s\"\n",
			        error(), *ws);
			return 0;
		}
		which |= 1<<r;
		ws++;
	}
	for (int i = 0; i < 17; i++, which>>=1) {
		if (!(which&1)) continue;
		unsigned long signval = m->registers[i];
		if (signval&0x80000000UL) {
			signval = -(((~signval) + 1)&0xFFFFFFFFUL);
		}
		_Bool ascii = m->registers[i] >= 0x20UL;
		ascii &= m->registers[i] < 0x7FUL;
		printf("\t%-3s    0x%08lx    %10lu    %11ld    %c%c%c",
		       regname(i), m->registers[i], m->registers[i],
		       (long)signval,
		       ascii? '\'' : ' ',
		       ascii? (char)m->registers[i] : ' ',
		       ascii? '\'' : ' ');
		if (i == 16) {
			printf("    ");
			printf("%c", (m->registers[16]&8)?'C':' ');
			printf("%c", (m->registers[16]&4)?'V':' ');
			printf("%c", (m->registers[16]&2)?'N':' ');
			printf("%c", (m->registers[16]&1)?'Z':' ');
		}
		printf("\n");
	}
	return 1;
}

static void
out(struct machine *m, struct dynarr *breaks)
{
	if (!m || !breaks) return;
	runto(m, breaks, 0, m->registers[14]);
}

static void
over(struct machine *m, struct dynarr *breaks)
{
	if (!m || !breaks) return;
	unsigned long target = (m->registers[15] + 2)&0xFFFFFFFFUL;
	runto(m, breaks, 0, target);
}

static void
run(struct machine *m, struct dynarr *breaks)
{
	runto(m, breaks, 1, 0);
}

static int
step(struct machine *m, struct dynarr *breaks)
{
	if (!m || !breaks) return -1;
	unsigned long instr = instruction(m, m->registers[15]);
	unsigned long origreg = 0;
	unsigned long source = 0;
	unsigned long long result = 0;
	unsigned int regd = 0;
	unsigned int regs = 0;
	int caught = -1;
	_Bool carry = 0;
	switch (instr>>12) {
	case 0:
		regd = (instr>>4)&0xF;
		source = (instr>>8UL)&0x7UL;
		source = (instr&0xFUL)<<(source<<2UL);
		result = m->registers[regd] & source;
		m->registers[regd] = (unsigned long)result&0xFFFFFFFFUL;
		if (!(instr&0x0800UL)) break;
		m->registers[16] &= ~0xFUL;
		m->registers[16] |= (result&0x80000000UL)? 2 : 0;
		m->registers[16] |= result? 0 : 1;
		break;
	case 1:
		regd = (instr>>4)&0xF;
		source = (instr>>8UL)&0x7UL;
		source = (instr&0xFUL)<<(source<<2UL);
		result = m->registers[regd] & ~source;
		m->registers[regd] = (unsigned long)result&0xFFFFFFFFUL;
		if (!(instr&0x0800UL)) break;
		m->registers[16] &= ~0xFUL;
		m->registers[16] |= (result&0x80000000UL)? 2 : 0;
		m->registers[16] |= result? 0 : 1;
		break;
	case 2:
		regd = (instr>>4)&0xF;
		source = (instr>>8UL)&0x7UL;
		source = (instr&0xFUL)<<(source<<2UL);
		result = m->registers[regd] | source;
		m->registers[regd] = (unsigned long)result&0xFFFFFFFFUL;
		if (!(instr&0x0800UL)) break;
		m->registers[16] &= ~0xFUL;
		m->registers[16] |= (result&0x80000000UL)? 2 : 0;
		m->registers[16] |= result? 0 : 1;
		break;
	case 3:
		regd = (instr>>4)&0xF;
		source = (instr>>8UL)&0x7UL;
		source = (instr&0xFUL)<<(source<<2UL);
		result = m->registers[regd] ^ source;
		m->registers[regd] = (unsigned long)result&0xFFFFFFFFUL;
		if (!(instr&0x0800UL)) break;
		m->registers[16] &= ~0xFUL;
		m->registers[16] |= (result&0x80000000UL)? 2 : 0;
		m->registers[16] |= result? 0 : 1;
		break;
	case 4:
	case 5:
	case 6:
	case 7:
		regd = (instr>>4)&0xF;
		source = (instr>>8UL)&0x7UL;
		source = (instr&0xFUL)<<(source<<2UL);
		if ((instr>>12)&1) source = ~source;
		origreg = m->registers[regd];
		result = (unsigned long long)origreg + source;
		if ((instr>>12) == 5) {
			result++;
		} else if ((instr>>12) == 6) {
			result += !!(m->registers[16]&8);
		} else if ((instr>>12) == 7) {
			result +=  !(m->registers[16]&8);
		}
		m->registers[regd] = (unsigned long)result&0xFFFFFFFFUL;
		if (!(instr&0x0800UL)) break;
		m->registers[16] &= ~0xFUL;
		if ((instr>>12)&1) {
			m->registers[16] |= (result>>32)? 0 : 8;
		} else {
			m->registers[16] |= (result>>32)? 8 : 0;
		}
		m->registers[16] |=
		    (   (origreg&0x80000000UL) == (source&0x80000000)
		     && (origreg&0x80000000UL) != (source&0x80000000)
		    )? 4 : 0;
		m->registers[16] |= (result&0x80000000UL)? 2 : 0;
		m->registers[16] |= result? 0 : 1;
		break;
	case 8:
		regd = (instr>>4)&0xF;
		regs = instr&0xF;
		origreg = m->registers[regd];
		source = m->registers[regs];
		switch ((instr>>8)&0xF) {
		case 0:
		case 8:
			result = origreg & source;
			m->registers[regd] = result&0xFFFFFFFFUL;
			if (!(instr&0x8000UL)) break;
			m->registers[16] &= ~0xF;
			m->registers[16] |= (result&0x80000000UL)? 2 : 0;
			m->registers[16] |= result? 0 : 1;
			break;
		case 1:
		case 9:
			result = origreg & ~source;
			m->registers[regd] = result&0xFFFFFFFFUL;
			if (!(instr&0x8000UL)) break;
			m->registers[16] &= ~0xF;
			m->registers[16] |= (result&0x80000000UL)? 2 : 0;
			m->registers[16] |= result? 0 : 1;
			break;
		case 2:
		case 10:
			result = origreg | source;
			m->registers[regd] = result&0xFFFFFFFFUL;
			if (!(instr&0x8000UL)) break;
			m->registers[16] &= ~0xF;
			m->registers[16] |= (result&0x80000000UL)? 2 : 0;
			m->registers[16] |= result? 0 : 1;
			break;
		case 3:
		case 11:
			result = origreg ^ source;
			m->registers[regd] = result&0xFFFFFFFFUL;
			if (!(instr&0x8000UL)) break;
			m->registers[16] &= ~0xF;
			m->registers[16] |= (result&0x80000000UL)? 2 : 0;
			m->registers[16] |= result? 0 : 1;
			break;
		case 4:
		case 12:
		case 5:
		case 13:
		case 6:
		case 14:
		case 7:
		case 15:
			if ((instr>>8)&1) source = ~source;
			result = (unsigned long long)origreg + source;
			if (((instr>>8)&0x7) == 5) {
				result++;
			} else if (((instr>>8)&0x7) == 6) {
				result += !!(m->registers[16]&8);
			} else if (((instr>>8)&0x7) == 7) {
				result +=  !(m->registers[16]&8);
			}
			m->registers[regd] = result&0xFFFFFFFFUL;
			if (!(instr&0x0800UL)) break;
			m->registers[16] &= ~0xFUL;
			if ((instr>>8)&1) {
				m->registers[16] |= (result>>32)? 0 : 8;
			} else {
				m->registers[16] |= (result>>32)? 8 : 0;
			}
			m->registers[16] |=
			    (   (origreg&0x80000000UL) == (source&0x80000000)
			     && (origreg&0x80000000UL) != (source&0x80000000)
			    )? 4 : 0;
			m->registers[16] |= (result&0x80000000UL)? 2 : 0;
			m->registers[16] |= result? 0 : 1;
			break;
		default:
			fprintf(stderr, "%sinternal error\n", error());
			return -1;
		}
		break;
	case 9:
		regd = (instr>>4)&0xF;
		origreg = m->registers[regd];
		switch ((instr>>8)&0x7) {
		case 0:
		case 4:
			source = m->registers[instr&0xF];
			if (instr&0x0400UL) {
				source  =  instr&0xF;
				source += (instr&0x0800UL)? 16 : 0;
			}
			if (source > 32) source = 32;
			for (int i = 0; i < source; i++) {
				carry = !!(m->registers[regd]&0x80000000UL);
				m->registers[regd] <<= 1;
			}
			m->registers[regd] &= 0xFFFFFFFFUL;
			result = m->registers[regd];
			if (!(instr&0x0C00UL)) break;
			m->registers[16] &= ~0xFUL;
			m->registers[16] |= carry? 8 : 0;
			m->registers[16] |= (result&0x80000000UL)? 2 : 0;
			m->registers[16] |= result? 0 : 1;
			break;
		case 1:
		case 5:
			source = m->registers[instr&0xF];
			if (instr&0x0400UL) {
				source  =  instr&0xF;
				source += (instr&0x0800UL)? 16 : 0;
			}
			if (source > 32) source = 32;
			for (int i = 0; i < source; i++) {
				carry = !!(m->registers[regd]&0x00000001UL);
				m->registers[regd] >>= 1;
			}
			m->registers[regd] &= 0xFFFFFFFFUL;
			result = m->registers[regd];
			if (!(instr&0x0C00UL)) break;
			m->registers[16] &= ~0xFUL;
			m->registers[16] |= carry? 8 : 0;
			m->registers[16] |= (result&0x80000000UL)? 2 : 0;
			m->registers[16] |= result? 0 : 1;
			break;
		case 2:
		case 6:
			source = m->registers[instr&0xF];
			if (instr&0x0400UL) {
				source  =  instr&0xF;
				source += (instr&0x0800UL)? 16 : 0;
			}
			if (source > 32) source = 32;
			for (int i = 0; i < source; i++) {
				carry = !!(m->registers[regd]&0x00000001UL);
				m->registers[regd] >>= 1;
				m->registers[regd] |=
				    (m->registers[regd]&0x40000000UL)<<1;
			}
			m->registers[regd] &= 0xFFFFFFFFUL;
			result = m->registers[regd];
			if (!(instr&0x0C00UL)) break;
			m->registers[16] &= ~0xFUL;
			m->registers[16] |= carry? 8 : 0;
			m->registers[16] |= (result&0x80000000UL)? 2 : 0;
			m->registers[16] |= result? 0 : 1;
			break;
		case 3:
			break;
		case 7:
			if (instr&0x08000000UL) {
				result  = (!!m->registers[16])<<31;
				result |= origreg>>1;
				result &= 0xFFFFFFFFUL;
				m->registers[16] &= ~0xF;
				m->registers[16] |= (origreg&1)? 8 : 0;
			} else {
				result  = !!m->registers[16];
				result |= origreg<<1;
				result &= 0xFFFFFFFFUL;
				m->registers[16] &= ~0xF;
				m->registers[16] |=
				    (origreg&0x80000000UL)? 8 : 0;
			}
			m->registers[regd] = result;
			m->registers[16] |= (result&0x80000000UL)? 2 : 0;
			m->registers[16] |= result? 0 : 1;
			break;
		default:
			fprintf(stderr, "%sinternal error\n", error());
			return -1;
		}
		break;
	case 10:
		regd = (instr>>4)&0xF;
		if (instr&0x0800UL) {
			m->registers[16] = m->registers[regd];
		} else {
			m->registers[regd] = m->registers[16];
		}
		break;
	case 11:
		regd = 15;
		source = instr&0xFF;
		if (source&0x80) source |= 0xFFFFFF00UL;
		if (!cond(m)) source = 1;
		m->registers[15] += source;
		m->registers[15] += source;
		m->registers[15] &= 0xFFFFFFFFUL;
		break;
	case 12:
		regd = (instr>>4)&0xF;
		regs = instr&0xF;
		origreg = m->registers[regd];
		source = m->registers[regs];
		if (cond(m)) {
			m->registers[regd] = source;
		} else if (regd == 15) {
			m->registers[regd] += 2;
		}
		break;
	case 13:
		regd = (instr>>4)&0xF;
		source = (instr>>8)&0x7;
		if (instr&0x0800UL) source<<=2;
		source += m->registers[instr&0xF];
		if (instr&0x0800UL) {
			source &= ~3;
			result = 0;
			result |= (unsigned char)
			          (m->data[(source+3)%m->datasize]);
			result <<= 8;
			result |= (unsigned char)
			          (m->data[(source+2)%m->datasize]);
			result <<= 8;
			result |= (unsigned char)
			          (m->data[(source+1)%m->datasize]);
			result <<= 8;
			result |= (unsigned char)
			          (m->data[(source+0)%m->datasize]);
			m->registers[regd] = result;
		} else {
			result = (unsigned char)m->data[source%m->datasize];
			result &= 0xFF;
			if (result&0x80) result |= 0xFFFFFF00UL;
			m->registers[regd] = result;
		}
		/* check watchpoints */
		for (int i = 0; i < breaks->length; i++) {
			struct breakpoint b =
				*(struct breakpoint *)da_get(*breaks, i);
			if (!(b.actions&BA_READ)) continue;
			if (instr&0x0800UL) b.location &= ~3;
			if (b.location == source) caught = i;
		}
		break;
	case 14:
		/* source/dest have reversed semantics in store */
		regd = (instr>>4)&0xF;
		source = (instr>>8)&0x7;
		if (instr&0x0800UL) source<<=2;
		source += m->registers[instr&0xF];
		result = m->registers[regd];
		if (instr&0x0800UL) {
			source &= ~3;
			m->data[(source+0)%m->datasize] = (result>> 0)&0xFF;
			m->data[(source+1)%m->datasize] = (result>> 8)&0xFF;
			m->data[(source+2)%m->datasize] = (result>>16)&0xFF;
			m->data[(source+3)%m->datasize] = (result>>24)&0xFF;
		} else {
			m->data[(source+0)%m->datasize] = (result>> 0)&0xFF;
		}
		/* check watchpoints */
		for (int i = 0; i < breaks->length; i++) {
			struct breakpoint b =
				*(struct breakpoint *)da_get(*breaks, i);
			if (!(b.actions&BA_WRITE)) continue;
			if (instr&0x0800UL) b.location &= ~3;
			if (b.location == source) caught = i;
		}
		break;
	case 15:
		regd = (instr>>4)&0xF;
		m->registers[regd] <<= 8;
		m->registers[regd] |= (instr>>4)&0xF0;
		m->registers[regd] |= instr&0xF;
		m->registers[regd] &= 0xFFFFFFFFUL;
		break;
	default:
		fprintf(stderr, "%sinternal error\n", error());
		return -1;
	}
	for (int i = 0; i < 16; i++) {
		if (m->registers[i] > 0xFFFFFFFFUL) {
			fprintf(stderr, "%soverhigh at insn %04lx\n",
			        warn(), instr);
		}
	}
	if (regd != 15) m->registers[15] += 2;
	for (int i = 0; i < breaks->length; i++) {
		struct breakpoint b =
			*(struct breakpoint *)da_get(*breaks, i);
		if (!(b.actions&BA_EXEC)) continue;
		if ((b.location&~1) == m->registers[15]) {
			return i;
		}
	}
	return caught;
}

static void
to(struct machine *m, struct dynarr *breaks, char **ws)
{
	unsigned long target = 0;
	if (!evalexpr(m, ws, &target)) return;
	fprintf(stderr, "%08lx\n", target);
	target &= 0xFFFFFFFFUL;
	runto(m, breaks, 0, target);
}

static _Bool
writereg(struct machine *m, char **ws)
{
	if (!m || !ws || !*ws) return 0;
	int regi = regnum(*ws);
	if (regi < 0 || regi > 16) {
		fprintf(stderr, "%sinvalid register name \"%s\"\n",
		        error(), *ws);
		return 0;
	}
	unsigned long value = 0;
	if (!evalexpr(m, ws + 1, &value)) return 0;
	m->registers[regi] = value&0xFFFFFFFFUL;
	return 1;
}


/* Helpers ************************************************************/
static _Bool
athalt(struct machine *m)
{
	if (!m) return 1;
	switch (instruction(m, m->registers[15]) & 0xF0FFU) {
	case 0xb000U: /* b . */
	case 0xc0ffU: /* mov pc, pc */
		return cond(m);
	default:
		break;
	}
	return 0;
}

static _Bool
cond(struct machine *m)
{
	if (!m) return 0;
	unsigned int instr = instruction(m, m->registers[15]);
	_Bool c = !!(m->registers[16]&8);
	_Bool v = !!(m->registers[16]&4);
	_Bool n = !!(m->registers[16]&2);
	_Bool z = !!(m->registers[16]&1);
	_Bool out = 0;
	switch ((instr>>8)&0x7) {
	case 0: out =  1;      break;
	case 1: out =  z;      break;
	case 2: out =  n;      break;
	case 3: out =  v;      break;
	case 4: out =  c;      break;
	case 5: out =  c|z;    break;
	case 6: out =  n^v;    break;
	case 7: out = (n^v)|z; break;
	default:
		fprintf(stderr, "%sinternal error\n", error());
		return 0;
	}
	if (instr&0x0800) out = !out;
	return out;
}

static void
context(struct machine *m)
{
	if (!m) return;
	unsigned long loc = m->registers[15];
	if (loc > 1) {
		char *ws[] = { ".", "-", "2", "10", NULL };
		disassemble(m, ws);
	} else {
		char *ws[] = { ".", "10", NULL };
		disassemble(m, ws);
	}
}

static void
emitdisassembly(
	struct machine const *m,
	unsigned long loc,
	unsigned long size)
{
	static char const * const regnames[] = {
		"r0", "r1", "r2" , "r3", "r4", "r5", "r6", "r7",
		"r8", "r9", "r10", "rT", "fp", "sp", "rp", "pc",
	};
	static char const * const arithinstrs[] = {
		"and", "clr", "ior", "xor",
		"add", "sub", "adx", "sbx",
	};
	static char const * const arithsinstrs[] = {
		"ands", "clrs", "iors", "xors",
		"adds", "subs", "adxs", "sbxs",
	};
	static char const * const shifts[] = {
		"lsl", "lsr", "asr", "???",
	};
	static char const * const sshifts[] = {
		"lsls", "lsrs", "asrs", "???s",
	};
	static char const * const bconds[] = {
		"b",   "bz",  "bmi", "bvs",
		"bcs", "bls", "blt", "ble",
		"bf",  "bnz", "bpl", "bvc",
		"bcc", "bhi", "bge", "bgt",
	};
	static char const * const movconds[] = {
		"mov",   "movz",  "movmi", "movvs",
		"movcs", "movls", "movlt", "movle",
		"movf",  "movnz", "movpl", "movvc",
		"movcc", "movhi", "movge", "movgt",
	};
	static char const * const formatr1 = "\033[1m%-7s\033[0m%s\n";
	static char const * const formatrr = "\033[1m%-7s\033[0m%s, %s\n";
	static char const * const formatru = "\033[1m%-7s\033[0m%s, %lu\n";
	static char const * const formatrx = "\033[1m%-7s\033[0m%s, 0x%lx\n";
	static char const * const formatx1 = "\033[1m%-7s\033[0m0x%04x\n";
	static char const * const formatb0 = "\033[1m%-7s\033[0m.\n";
	static char const * const formatbp = "\033[1m%-7s\033[0m. + %lu";
	static char const * const formatbm = "\033[1m%-7s\033[0m. - %lu";
	static char const * const formatsto
		= "\033[1m%-7s\033[0m[%s], %s\n";
	static char const * const formatstoof
		= "\033[1m%-7s\033[0m[%s,%lu], %s\n";
	static char const * const formatld
		= "\033[1m%-7s\033[0m%s, [%s]\n";
	static char const * const formatldof
		= "\033[1m%-7s\033[0m%s, [%s,%lu]\n";
	static char const * const comment1 = "\t; (%s:%s)\n";
	static char const * const commentp = "\t; (%s:%s + %lu)\n";
	static char const * const label
		= "%8s\033[0;34m%s:\033[0;39m ; %s:%s\n";
	if (!m) return;
	struct dynarr symbols = m->map.symbols;
	char *buf = m->map.nameblock.content;
	while (size) {
		unsigned int instr = instruction(m, loc);
		/* maybe print a label */
		int symi = locatesymi(m, loc, RE_TEXT);
		if (symi >= 0) {
			struct symbol sym = *(struct symbol *)
				da_get(symbols, symi);
			if (sym.address == loc) {
				printf(label, "",
				       buf + sym.namei,
				       buf + sym.fnamei,
				       buf + sym.namei);
			}
		}
		/* print opener */
		printf("\033[1;39m%6s\033[0;39m%08lx  %4s",
		       (loc == m->registers[15])? "->  " : "",
		       loc, "");
		/* print instruction */
		int regd = (instr>>4)&0xF;
		int regs = instr&0xF;
		_Bool setter = !!(instr&0x0800UL);
		unsigned int opc = (instr>>12)&0xF;
		unsigned long source = 0;
		unsigned long target = 0;
		if (opc == 9) setter |= !!(instr&0x0400UL);
		switch (opc) {
		case 0:
		case 1:
		case 2:
		case 3:
		case 4:
		case 5:
		case 6:
		case 7:
			source = (instr&0xFUL)<<((instr&0x0700UL)>>6);
			printf((source < 100)? formatru : formatrx,
			       (setter? arithsinstrs : arithinstrs)[opc],
			       regnames[regd], source);
			break;
		case 8:
			opc = (instr>>8)&0x7;
			printf(formatrr,
			       (setter? arithsinstrs : arithinstrs)[opc],
			       regnames[regd], regnames[regs]);
			break;
		case 9:
			source = ((instr&0x0800UL)>>7)|(instr&0xFUL);
			opc = (instr>>8)&0x7;
			if (opc == 3) {
				printf(formatx1, ".hword", instr);
			} else if (opc == 7) {
				printf(formatr1,
				       (instr&0x0800U)? "rrcs" : "rlcs",
				       regnames[regd]);
			} else if (instr&0x0400UL) {
				printf(formatru,
				       (setter? sshifts : shifts)[opc&3],
				       regnames[regd], source);
			} else {
				printf(formatrr,
				       (setter? sshifts : shifts)[opc&3],
				       regnames[regd], regnames[regs]);
			}
			break;
		case 10:
			printf(formatrr, "mov",
			       setter? "sr" : regnames[regd],
			       setter? regnames[regd] : "sr");
			break;
		case 11:
			opc = (instr>>8)&0xF;
			source = instr&0xFFUL;
			target = loc;
			if (!source) {
				printf(formatb0, bconds[opc]);
				break;
			}
			if (source&0x80UL) {
				source ^= 0xFF;
				source += 1;
				source *= 2;
				printf(formatbm, bconds[opc], source);
				target -= source;
			} else {
				source *= 2;
				printf(formatbp, bconds[opc], source);
				target += source;
			}
			symi = locatesymi(m, target, RE_TEXT);
			if (symi < 0) {
				printf("\n");
				break;
			} else {
				struct symbol sym = *(struct symbol *)
					da_get(symbols, symi);
				if (sym.address == target) {
					printf(comment1,
					       buf + sym.fnamei,
					       buf + sym.namei);
				} else {
					printf(commentp,
					       buf + sym.fnamei,
					       buf + sym.namei,
					       target - sym.address);
				}
			}
			break;
		case 12:
			printf(formatrr, movconds[(instr>>8)&0xF],
			       regnames[regd], regnames[regs]);
			break;
		case 13:
			source = (instr>>8)&0x7;
			if (setter && source) {
				printf(formatldof, "ld",
				       regnames[regd], regnames[regs],
				       source<<2);
			} else if (source) {
				printf(formatldof, "ldb",
				       regnames[regs], regnames[regd],
				       source);
			} else {
				printf(formatld, setter? "ld" : "ldb",
				       regnames[regd], regnames[regs]);
			}
			break;
		case 14:
			/* regs / regd have flipped semantics */
			source = (instr>>8)&0x7;
			if (setter && source) {
				printf(formatstoof, "sto",
				       regnames[regs], source<<2,
				       regnames[regd]);
			} else if (source) {
				printf(formatstoof, "stob",
				       regnames[regs], source,
				       regnames[regd]);
			} else {
				printf(formatsto, setter? "sto" : "stob",
				       regnames[regs], regnames[regd]);
			}
			break;
		case 15:
			source = ((instr&0x0F00UL)>>4)|(instr&0xF);
			printf(formatru, "sset", regnames[regd], source);
			break;
		default:
			fprintf(stderr, "%sinternal error\n", error());
			return;
		}
		loc+=2;
		size--;
	}
}

static char const *
error(void)
{
	return "\033[1;31merror: \033[0;39m";
}

static _Bool
evalexpr(struct machine const *m, char **ws, unsigned long *out)
{
	if (!m || !out) return 0;
	if (!ws || !*ws) {
		fprintf(stderr, "%sexpressionless expression\n", error());
		return 0;
	}
	char *endptr = NULL;
	*out = strtoul(*ws, &endptr, 0);
	if (!strcmp(*ws, ".")) {
		*out = m->registers[15];
	} else if (!endptr || *endptr != '\0') {
		int symi = getsymi(m, *ws);
		if (symi == -2) {
			fprintf(stderr, "%sambiguous symbol \"%s\"\n",
			        error(), *ws);
			return 0;
		} else if (symi == -1) {
			fprintf(stderr, "%scould not find symbol \"%s\"\n",
			        error(), *ws);
			return 0;
		}
		struct dynarr symbols = m->map.symbols;
		struct symbol sym = *(struct symbol *)da_get(symbols, symi);
		*out = sym.address;
	}
	*out &= 0xFFFFFFFFUL;
	if (!ws[1]) return 1;
	if (strlen(ws[1]) != 1) {
		fprintf(stderr, "%sbad operator \"%s\"\n", error(), ws[1]);
		return 0;
	}
	if (!ws[2]) {
		fprintf(stderr, "%smissing right operand\n", error());
		return 0;
	}
	if (ws[3]) {
		fprintf(stderr, "%soverlong expression\n", error());
		return 0;
	}
	unsigned long op2 = strtoul(ws[2], &endptr, 0);
	if (!strcmp(ws[2], ".")) {
		op2 = m->registers[15];
	} else if (!endptr || *endptr != '\0') {
		int symi = getsymi(m, ws[2]);
		if (symi == -2) {
			fprintf(stderr, "%sambiguous symbol \"%s\"\n",
			        error(), ws[2]);
			return 0;
		} else if (symi == -1) {
			fprintf(stderr, "%scould not find symbol \"%s\"\n",
			        error(), ws[2]);
			return 0;
		}
		struct dynarr symbols = m->map.symbols;
		struct symbol sym = *(struct symbol *)da_get(symbols, symi);
		op2 = sym.address;
	}
	switch (ws[1][0]) {
	case '+':
		*out += op2;
		*out &= 0xFFFFFFFFUL;
		break;
	case '-':
		*out -= op2;
		*out &= 0xFFFFFFFFUL;
		break;
	default:
		fprintf(stderr, "%sbad operator \"%s\"\n", error(), ws[1]);
		return 0;
	}
	return 1;
}

static int
getsymi(struct machine const *m, char const *w)
{
	if (!m || !w) return -1;
	if (!m->map.symbols.length || !m->map.nameblock.length) return -1;
	int n = strlen(w);
	char *buf = malloc(n + 1);
	if (!buf) return -1;
	strncpy(buf, w, n + 1);
	char *fname = NULL;
	char *name = buf;
	for (int i = 0; i < n; i++) {
		if (buf[i] == ':') {
			buf[i] = '\0';
			fname = buf;
			name = buf + i + 1;
		}
	}

	int out = -1;
	struct map map = m->map;
	for (int i = 0; i < map.symbols.length; i++) {
		struct symbol sym = *(struct symbol *)da_get(map.symbols, i);
		if (fname) {
			char *s = (char *)da_get(map.nameblock, sym.fnamei);
			if (strcmp(fname, s)) continue;
		}
		char *s = (char *)da_get(map.nameblock, sym.namei);
		if (strcmp(name, s)) continue;
		if (out == -1) {
			out = i;
		} else {
			out = -2;
		}
	}
	free(buf);
	return out;
}

static void
hexdump(struct machine *m, int region, unsigned long loc, unsigned long size)
{
	if (!m || !size) return;
	if (region != RE_TEXT && region != RE_DATA) return;
	char *buf = (region == RE_TEXT)? m->text : m->data;
	int bufsz = (region == RE_TEXT)? m->textsize : m->datasize;
	unsigned long i = 0;
	for (; i < size; i++) {
		if (i%16 == 0) {
			printf("%08lx", (loc + i)&0xFFFFFFFFUL);
		}
		if (i%8 == 0) printf(" ");
		printf(" %02x", (unsigned char)buf[(loc + i)%bufsz]);
		if (i%16 == 15 || i == size - 1) {
			int end = i%16;
			while (i%16 != 15) {
				printf("   ");
				if (i%16 == 8) printf(" ");
				i++;
			}
			printf("  |");
			for (int j = 0; j <= end; j++) {
				unsigned long x = loc + (i - 15) + j;
				char c = buf[x%bufsz];
				_Bool printable = (c >= 32);
				printable &= (c < 0x7F);
				printf("%c", printable? c : '.');
			}
			printf("|\n");
		}
	}
}

static int
hexvalue(char c)
{
	if (isdigit(c)) return c - '0';
	if (c == 'A' || c == 'a') return 10;
	if (c == 'B' || c == 'b') return 11;
	if (c == 'C' || c == 'c') return 12;
	if (c == 'D' || c == 'd') return 13;
	if (c == 'E' || c == 'e') return 14;
	if (c == 'F' || c == 'f') return 15;
	return -1;
}

static unsigned int
instruction(struct machine const *m, unsigned long addr)
{
	if (!m) return 0U;
	unsigned int instr = 0;
	addr = (addr & 0xFFFFFFFEUL);
	instr |= ((unsigned char)m->text[(addr + 0)%m->textsize])<<0;
	instr |= ((unsigned char)m->text[(addr + 1)%m->textsize])<<8;
	return instr;
}

/* find _function_ symbol in text region that contains loc
 * unsized symbols are considered to go on forever
 * largest symbol location less than or equal to loc is the winner */
static int
locatefunc(struct machine const *m, unsigned long loc)
{
	int out = -1;
	unsigned long bestloc = 0;
	if (!m) return out;
	struct dynarr symbols = m->map.symbols;
	for (int i = 0; i < symbols.length; i++) {
		struct symbol sym = *(struct symbol *)da_get(symbols, i);
		if (sym.region != RE_TEXT) continue;
		if (sym.kind != SK_FUNCTION) continue;
		if (sym.address > loc) continue;
		if (sym.size && sym.address + sym.size <= loc) continue;
		if (out != -1 && sym.address <= bestloc) continue;
		out = i;
		bestloc = sym.address;
	}
	return out;
}

/* find symbol that contains loc
 * unsized symbols are considered to go on forever
 * largest symbol location less than or equal to loc is the winner */
static int
locatesymi(struct machine const *m, unsigned long loc, enum region region)
{
	int out = -1;
	unsigned long bestloc = 0;
	if (!m) return out;
	struct dynarr symbols = m->map.symbols;
	for (int i = 0; i < symbols.length; i++) {
		struct symbol sym = *(struct symbol *)da_get(symbols, i);
		if (sym.region != region) continue;
		if (sym.address > loc) continue;
		if (out != -1 && sym.address <= bestloc) continue;
		out = i;
		bestloc = sym.address;
	}
	return out;
}

static _Bool
logiread(FILE *file, char *buf, unsigned long size)
{
	if (!file || !buf || !size) return 0;
	_Bool warned = 0;
	static char const header[] = "v3.0 hex bytes addressed little-endian";
	char c = 0;
	for (int i = 0; i < sizeof(header) - 1; i++) {
		c = getc(file);
		if (feof(file) || c != header[i]) {
			fprintf(stderr, "%sinvalid input header @ %d\n",
			        error(), i);
			fprintf(stderr, "be sure to use \"%s\"\n", header);
			return 0;
		}
	}
	do { c = getc(file); } while (!feof(file) && c != '\n');
	/* past header: lines now are "addr: unseparatedhexdigitpairs" */
	while (!feof(file)) {
		unsigned long addr = 0;
		c = '0';
		do {
			if (hexvalue(c) != -1) {
				addr = 16*addr + hexvalue(c);
			} else {
				fprintf(stderr,
				        "%sinvalid input file\n", error());
				return 0;
			}
			c = getc(file);
		} while (!feof(file) && c != ':');
		do { c = getc(file); } while (!feof(file) && isspace(c));
		while (!feof(file) && c != '\n') {
			unsigned long val = 0;
			if (hexvalue(c) != -1) {
				val = 16*val + hexvalue(c);
			} else {
				fprintf(stderr,
				        "%sinvalid input file\n", error());
				return 0;
			}
			c = getc(file);
			if (hexvalue(c) != -1) {
				val = 16*val + hexvalue(c);
			} else {
				fprintf(stderr,
				        "%sinvalid input file\n", error());
				return 0;
			}
			if (addr >= size && !warned) {
				fprintf(stderr,
				        "%saddress cycled at %lu\n",
				        warn(), addr);
			}
			buf[addr%size] = val;
			addr++;
			/* should never loop, but, reach EOL or next num */
			do { c = getc(file); } while (!feof(file) && c == ' ');
		}
	}
	return 1;
}

static _Bool
mapread(FILE *file, struct map *map)
{
	if (!file || !map) return 0;
	da_free(map->symbols);
	da_free(map->nameblock);
	map->symbols = da_new(sizeof(struct symbol));
	map->nameblock = da_new(1);
	da_append(&map->nameblock, "\0");
	int lastfname = 0;
	_Bool go = 1;

	do {
		/* read a line */
		struct dynarr line = da_new(1);
		char c = 0;
		do {
			c = getc(file);
			if (c != '\r' && !feof(file)) da_append(&line, &c);
		} while (c != '\n' && !feof(file));
		da_append(&line, "\0");
	
		char *buf = line.content;
		if (!feof(file) && line.length > 3 && buf[1] == '@') {
			lastfname = map->nameblock.length;
			for (int i = 3;
			     buf[i] != '\n' && buf[i] != '\r'
			     && i < line.length;
			     i++) {
				da_append(&map->nameblock, buf + i);
			}
			da_append(&map->nameblock, "\0");
		} else if (!feof(file) && line.length > 22 && buf[1] == ' ') {
			char *endptr;
			_Bool failed = 0;
			struct symbol sym = {0};
			sym.address = strtoul(buf + 4, &endptr, 16);
			failed |= (endptr != buf + 4 + 8);
			sym.size = strtoul(buf + 13, &endptr, 16);
			failed |= (endptr != buf + 13 + 8);
			sym.region = (tolower(buf[0]) == 't')? RE_TEXT :
				     (tolower(buf[0]) == 'd')? RE_DATA :
				     (tolower(buf[0]) == 'a')? RE_ABS  :
				     RE_UNKNOWN;
			sym.global = (buf[0] == toupper(buf[0]));
			sym.kind = (buf[2] == 'f')? SK_FUNCTION :
				   (buf[2] == 'o')? SK_OBJECT :
				   SK_UNKNOWN;
			sym.fnamei = lastfname;
			sym.namei = map->nameblock.length;
			for (int i = 22;
			     buf[i] != '\n' && buf[i] != '\r'
			     && i < line.length;
			     i++) {
				da_append(&map->nameblock, buf + i);
			}
			da_append(&map->nameblock, "\0");
			da_append(&map->symbols, &sym);
			if (failed) {
				fprintf(stderr, "%sbad line in symbol map\n",
				        warn());
				go = 0;
			}
		} else if (!feof(file)) {
			go = 0;
			fprintf(stderr, "%sbad line in symbol map\n", warn());
		}
		da_free(line);
	} while (go && !feof(file));
	return go;
}

static _Bool
memread(struct machine const *m, char **ws)
{
	if (!m) { return 0; }
	if (!m->isloaded) { needload(); return 0; }
	static char const usage[]
		= "%susage: memory read <format> <location>\n";
	if (!ws || !ws[0] || !ws[1]) {
		fprintf(stderr, usage, error());
		return 0;
	}
	unsigned long loc = 0;
	if (!evalexpr(m, ws + 1, &loc)) return 0;
	char *buf = m->data;
	unsigned long bvalue = (unsigned char)buf[loc%m->datasize];
	unsigned long hwvalue
		= (unsigned char)buf[(loc&0xFFFFFFFEUL)%m->datasize];
	hwvalue |= ((unsigned char)buf[((loc&0xFFFFFFFEUL)+1)%m->datasize])<<8;
	unsigned long wvalue
		= (unsigned char)buf[(loc&0xFFFFFFFCUL)%m->datasize];
	wvalue |= ((unsigned char)buf[((loc&0xFFFFFFFCUL)+1)%m->datasize])<<8;
	wvalue |= ((unsigned char)buf[((loc&0xFFFFFFFCUL)+2)%m->datasize])<<16;
	wvalue |= ((unsigned char)buf[((loc&0xFFFFFFFCUL)+3)%m->datasize])<<24;
	if (!strcmp(*ws, "byte") || !strcmp(*ws, "b")) {
		printf("\t%08lx:  %lu\n", loc&0xFFFFFFFFUL, bvalue);
	} else if (!strcmp(*ws, "hword") || !strcmp(*ws, "hw")) {
		printf("\t%08lx:  %lu\n", loc&0xFFFFFFFEUL, hwvalue);
	} else if (!strcmp(*ws, "word") || !strcmp(*ws, "w")) {
		printf("\t%08lx:  %lu\n", loc&0xFFFFFFFCUL, wvalue);
	} else if (!strcmp(*ws, "sbyte") || !strcmp(*ws, "sb")) {
		if (bvalue&0x80UL) bvalue = ((~0UL)&(~0x7FUL))|bvalue;
		printf("\t%08lx:  %ld\n", loc&0xFFFFFFFFUL, bvalue);
	} else if (!strcmp(*ws, "shword") || !strcmp(*ws, "shw")) {
		if (hwvalue&0x8000UL) hwvalue = ((~0UL)&(~0x7FFFUL))|hwvalue;
		printf("\t%08lx:  %ld\n", loc&0xFFFFFFFEUL, hwvalue);
	} else if (!strcmp(*ws, "sword") || !strcmp(*ws, "sw")) {
		if (wvalue&0x80000000UL) {
			wvalue = ((~0UL)&(~0x7FFFFFFFUL))|wvalue;
		}
		printf("\t%08lx:  %ld\n", loc&0xFFFFFFFCUL, wvalue);
	} else if (!strcmp(*ws, "xbyte") || !strcmp(*ws, "xb")) {
		printf("\t%08lx:  0x%02lx\n", loc&0xFFFFFFFFUL, bvalue);
	} else if (!strcmp(*ws, "xhword") || !strcmp(*ws, "xhw")) {
		printf("\t%08lx:  0x%04lx\n", loc&0xFFFFFFFEUL, hwvalue);
	} else if (!strcmp(*ws, "xword") || !strcmp(*ws, "xw")) {
		printf("\t%08lx:  0x%08lx\n", loc&0xFFFFFFFCUL, wvalue);
	} else {
		fprintf(stderr, usage, error());
		return 0;
	}
	return 1;
}

static _Bool
memwrite(struct machine const *m, char **ws)
{
	if (!m) { return 0; }
	if (!m->isloaded) { needload(); return 0; }
	static char const usage[]
		= "%susage: memory write <format> <location> <value>\n";
	if (!ws || !ws[0] || !ws[1]) {
		fprintf(stderr, usage, error());
		return 0;
	}
	char *arr[] = {ws[1], NULL, NULL, NULL};
	int used = 2;
	if (!strcmp(ws[1], "+") || !strcmp(ws[1], "-")) {
		arr[1] = ws[2];
		arr[2] = ws[3];
		used += 4;
	}
	unsigned long loc = 0;
	if (!evalexpr(m, arr, &loc)) return 0;
	unsigned long value = 0;
	if (!evalexpr(m, ws + used, &value)) return 0;
	char *buf = m->data;
	if (!strcmp(*ws, "byte") || !strcmp(*ws, "b")) {
		buf[loc%m->datasize] = value&0xFF;
	} else if (!strcmp(*ws, "hword") || !strcmp(*ws, "hw")) {
		loc &= 0xFFFFFFFEUL;
		buf[(loc + 0)%m->datasize] =  value     &0xFF;
		buf[(loc + 1)%m->datasize] = (value>> 8)&0xFF;
	} else if (!strcmp(*ws, "word") || !strcmp(*ws, "w")) {
		loc &= 0xFFFFFFFEUL;
		buf[(loc + 0)%m->datasize] =  value     &0xFF;
		buf[(loc + 1)%m->datasize] = (value>> 8)&0xFF;
	} else {
		fprintf(stderr, usage, error());
		return 0;
	}
	return 1;
}

static void
needload(void)
{
	fprintf(stderr, "%scommand requires a loaded program\n", error());
}

static _Bool
readsnum(char const *s, int *v)
{
	int base = 10;
	int i = 0;
	_Bool negate = 0;
	if (!s || !*s || !v) return 0;
	int out = 0;
	while (s[0] == '-') {
		negate = !negate;
		s++;
	}
	if (s[0] == '0') {
		if (s[1] == 'x') {
			base = 16;
			s += 2;
		} else if (s[1] == 'b') {
			base = 2;
			s += 2;
		}
	}
	while (s[i]) {
		char c = s[i++];
		if (isdigit(c) && base > (c - '0')) {
			out *= base;
			out += c - '0';
		} else if (hexvalue(c) != -1 && base == 16) {
			out *= base;
			out += hexvalue(c);
		} else if (c == 'K' || c == 'k') {
			if (s[i]) return 0;
			out *= 1024;
		} else if (c == 'M' || c == 'm') {
			if (s[i]) return 0;
			out *= 1024 * 1024;
		} else if (c == 'G' || c == 'g') {
			if (s[i]) return 0;
			out *= 1024 * 1024 * 1024;
		}
	}
	*v = negate? -out : out;
	return 1;
}

static char const *
regname(int n)
{
	static char const * const names[] = {
		"r0",  "r1",  "r2",  "r3",
		"r4",  "r5",  "r6",  "r7",
		"r8",  "r9",  "r10", "rT",
		"fp",  "sp",  "rp",  "pc",
		"sr"
	};
	if (n < 0 || n > 16) return NULL;
	return names[n];
}

static int
regnum(char const *s)
{
	if (!s || !*s) return -1;
	if (tolower(*s) == 'r') {
		if (s[1] == '\0') return -1;
		if (tolower(s[1]) == 'p' && s[2] == '\0') return 14;
		if (tolower(s[1]) == 't' && s[2] == '\0') return 11;
		if (hexvalue(s[1]) < 0) return -1;
		if (s[2] == '\0') return hexvalue(s[1]);
		if (s[1] != '1') return -1;
		if (s[3] != '\0') return -1;
		return 10 + hexvalue(s[2]);
	}
	if (tolower(s[0]) == 'f' && tolower(s[1]) == 'p' && s[2] == '\0') {
		return 12;
	}
	if (tolower(s[0]) == 's' && tolower(s[1]) == 'p' && s[2] == '\0') {
		return 13;
	}
	if (tolower(s[0]) == 'p' && tolower(s[1]) == 'c' && s[2] == '\0') {
		return 15;
	}
	if (tolower(s[0]) == 's' && tolower(s[1]) == 'r' && s[2] == '\0') {
		return 16;
	}
	return -1;
}

static void
runto(struct machine *m, struct dynarr *breaks, _Bool go, unsigned long target)
{
	if (!m || !breaks) return;
	if (!m->isloaded) { needload(); return; }
	target &= 0xFFFFFFFEUL;
	do {
		int bp = step(m, breaks);
		if (bp >= 0) {
			fprintf(stderr, "stopped at breakpoint %d\n", bp);
			context(m);
			return;
		}
	} while (!athalt(m)
	         && (go || (m->registers[15]&0xFFFFFFFEUL) != target));
}

static char **
words(char *s)
{
	if (!s) return NULL;
	while (*s && isspace(*s)) s++;
	if (!*s) return NULL;
	char const *p = s;
	int ws = 0;
	int incls = 0;
	do {
		ws++;
		while (*p && !isspace(*p)) {incls++; p++;}
		incls++;
		while (*p &&  isspace(*p)) p++;
	} while (*p);
	/* ensure a nice multiple */
	if (incls%sizeof(char*)) {
		incls += sizeof(char*) - (incls%sizeof(char*));
	}
	/* one entry per word, one for null, and string buffer attached */
	char **out = malloc((ws + 1)*sizeof(*out) + incls);
	char *strbuf = (char*)&out[ws + 1];
	int i = 0;
	do {
		out[i++] = strbuf;
		while (*s && !isspace(*s)) *(strbuf++) = *(s++);
		*(strbuf++) = '\0';
		while (*s &&  isspace(*s)) s++;
	} while (*s);
	out[i++] = NULL;
	return out;
}

static char const *
warn(void)
{
	return "\033[1;35mwarning:\033[0;39m ";
}
