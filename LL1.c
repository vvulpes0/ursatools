#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LINEWIDTH     256
#define MAXRULES      1024
#define RULEITEMS     64
#define STRINGTABC    4096
#define SETSIZE       (STRINGTABC + 256)
#define SETCHARS      ((SETSIZE + CHAR_BIT - 1)/CHAR_BIT)
#define FOLLOWEND     0x100
/* lexer should emit FOLLOWEND for EOF */
#define PREFIX        "LL1_"
#define ERRFUNCALL    (PREFIX "error(NULL);")
#define VALSTACK      (PREFIX "vstack")
#define VALSTACKCAP   (PREFIX "vstackc")
#define VALSTACKI     (PREFIX "vstacki")
#define VALSTACKITEM  ("union " PREFIX "vstack_item")
#define VALPUSH       (PREFIX "vpush")
#define INSTACK       (PREFIX "istack")
#define INSTACKCAP    (PREFIX "istackc")
#define INSTACKI      (PREFIX "istacki")
#define INSTACKITEM   ("struct " PREFIX "istack_item")
#define INPUSH        (PREFIX "ipush")

struct item {
	int type; /* 0 = direct char, 1 = name */
	int value; /* for 0: char value. for 1: stringtab index */
};
struct rule {
	int nt;
	int linenum;
	struct item items[RULEITEMS];
	int itemsi;
};

/* global state */
static FILE *infile;
static char linebuf[LINEWIDTH];
static char typename[LINEWIDTH];
static char typename_tok[LINEWIDTH];
static char posvars[3][LINEWIDTH];
static char stringtab[STRINGTABC];
static int stringtabi;
/* rules */
static struct rule rules[MAXRULES];
static int rulesi;
static int nonterminals[MAXRULES];
static int nonterminalsi;
static int terminals[SETSIZE];
static int terminalsi;
static char firsts[MAXRULES][SETCHARS];
static char follows[MAXRULES][SETCHARS];
static char table[MAXRULES][SETSIZE];

/* functions */
static void addrule(int);
static int addstr(char const *);
static void addterm(int);
static void conflict(int, int);
static _Bool handle_directive(void);
static void leave(int);
static int lineget(FILE *infile);
static void mkfirsts(void);
static void mkfollows(void);
static void mktable(void);
static void mktabler(int);
static int nti(int);
static _Bool seta(char *, unsigned int);
static _Bool seti(char *, unsigned int);
static _Bool setr(char *, unsigned int);
static _Bool setu(char *, char const *);
static int ti(int);
static _Bool updatefirsts(struct rule);
static _Bool updatefollows(struct rule);
static _Bool updatefollowsx(struct rule, int i);

static void
addrule(int linenum)
{
	char *b = linebuf;
	char *e = linebuf;
	_Bool wasdone = 0;
	_Bool found = 0;
	struct rule r = {0};
	while (*e && !isspace(*e)) { e++; }
	if (!*e) {
		fprintf(stderr, "error: end of rule before \"::=\"\n");
		leave(EXIT_FAILURE);
	}
	*e = '\0';
	r.nt = addstr(b);
	for (int i = 0; !found && i < nonterminalsi; i++) {
		if (nonterminals[i] == r.nt) found = 1;
	}
	if (!found) {
		if (nonterminalsi == MAXRULES) {
			fprintf(stderr, "error: too many nonterminals\n");
			leave(EXIT_FAILURE);
		}
		nonterminals[nonterminalsi++] = r.nt;
	}
	r.linenum = linenum;
	b = e + 1;
	while (*b && isspace(*b)) b++;
	if (strncmp(b, "::=", 3) || (b[3] && !isspace(b[3]))) {
		fprintf(stderr, "error: missing \"::=\" in rule\n");
		fprintf(stderr, "linebuf: %s\n", linebuf);
		fprintf(stderr, "b: %s\n", b);
		leave(EXIT_FAILURE);
	}
	b += 3;
	while (*b) {
		while (isspace(*b)) { b++; }
		e = b;
		while (*e && !isspace(*e)) { e++; }
		wasdone = !*e;
		*e = '\0';
		if (b == e) {
			; /* do nothing */
		} else if (*b == '\'') {
			if (b[1] != '\0' && b[1] != '\\'
			    && b[2] == '\'' && e == b + 3) {
				r.items[r.itemsi++] = (struct item){0, b[1]};
			} else if (b[1] == '\\' && b[2] != '\0'
			           && b[3] == '\'' && e == b + 4) {
				int x;
				switch (b[2]) {
				case 'a': x = '\a'; break;
				case 'b': x = '\b'; break;
				case 'f': x = '\f'; break;
				case 'n': x = '\n'; break;
				case 'r': x = '\r'; break;
				case 't': x = '\t'; break;
				case 'v': x = '\v'; break;
				default: x = b[2]; break;
				}
				r.items[r.itemsi++] = (struct item){0, x};
			} else {
				fprintf(stderr,
				        "error: invalid character constant %s",
				        b);
				leave(EXIT_FAILURE);
			}
		} else {
			r.items[r.itemsi++] = (struct item){1, addstr(b)};
		}
		b = e + (wasdone? 0 : 1);
	}
	rules[rulesi++] = r;
}

static int
addstr(char const *s)
{
	int n = strlen(s);
	for (int i = 0; i < stringtabi - n; i++) {
		if (!strncmp(s, stringtab + i, n + 1)) return i;
	}
	if (stringtabi + n >= STRINGTABC) {
		fprintf(stderr, "error: string table full\n");
		leave(EXIT_FAILURE);
	}
	strncpy(stringtab + stringtabi, s, n + 1);
	stringtabi += n + 1;
	return stringtabi - n - 1;
}

static void
addterm(int t)
{
	for (int i = 0; i < terminalsi; i++) {
		if (terminals[i] == t) return;
	}
	if (terminalsi == SETSIZE) return;
	terminals[terminalsi++] = t;
}

static void
conflict(int nt, int t)
{
	if (t > 256) {
		fprintf(stderr, "error: conflict for %s:%s\n",
		        stringtab + nt, stringtab + t - 256);
		leave(EXIT_FAILURE);
	}
	fprintf(stderr,
		(t >= 0x20 && t < 0x7F)
		? "error: conflict for %s:'%c'\n"
		: "error: conflict for %s:'\\x%02x'\n",
		stringtab + nt, t);
	leave(EXIT_FAILURE);
}

static _Bool
handle_directive(void)
{
	char *b = NULL;
	int n = 0;
	if (!strncmp(linebuf, "%%", 3)) {
		return 1;
	}
	if (!strncmp(linebuf, "%type", 5) && isspace(linebuf[5])) {
		b = linebuf + 6;
		while (isspace(*b)) b++;
		strncpy(typename, b, sizeof(typename));
		return 0;
	}
	if (!strncmp(linebuf, "%ttype", 6) && isspace(linebuf[6])) {
		b = linebuf + 7;
		while (isspace(*b)) b++;
		strncpy(typename_tok, b, sizeof(typename_tok));
		return 0;
	}
	if (strncmp(linebuf, "%pos", 4) || !isspace(linebuf[4])) {
		fprintf(stderr, "error: bad var line: %s\n", linebuf);
		leave(EXIT_FAILURE);
	}
	b = linebuf + 4;
	while (isspace(*b)) b++;
	for (int i = 0; i < 3 && *b; i++) {
		n = 0;
		while (!isspace(b[n]) && b[n]) { n++; }
		strncpy(posvars[i], b, n);
		posvars[i][n] = '\0';
		b += n;
		while (isspace(*b)) b++;
	}
	return 0;
}

static void
leave(int exitcode)
{
	if (infile) { fclose(infile); infile = NULL; }
	exit(exitcode);
}

static int
lineget(FILE *f)
{
	int n = 0;
	_Bool held = 0;
	linebuf[0] = '\0';
	char c = fgetc(f);
	while (c != '\n' && !feof(f)) {
		if (c == '\r') {
			held = 1;
		} else {
			if (n + 2 >= sizeof(linebuf)) {
				fprintf(stderr, "error: overlong line\n");
				leave(EXIT_FAILURE);
			}
			if (held) {
				linebuf[n++] = '\r';
				held = 0;
			}
			linebuf[n++] = c;
		}
		c = fgetc(f);
	}
	linebuf[n] = '\0';
	return n;
}

static void
mkfirsts(void)
{
	_Bool changed;
	do {
		changed = 0;
		for (int i = 0; i < rulesi; i++) {
			changed |= updatefirsts(rules[i]);
		}
	} while (changed);
}

static void
mkfollows(void)
{
	_Bool changed;
	seta(follows[0], FOLLOWEND);
	addterm(FOLLOWEND);
	do {
		changed = 0;
		for (int i = 0; i < rulesi; i++) {
			changed |= updatefollows(rules[i]);
		}
	} while (changed);
}

static void
mktable(void)
{
	for (int nt = 0; nt < nonterminalsi; nt++) {
		for (int t = 0; t < terminalsi; t++) {
			table[nt][t] = -1;
		}
	}
	for (int i = 0; i < rulesi; i++) mktabler(i);
}

static void
mktabler(int ri)
{
	struct rule r = rules[ri];
	int n = nti(r.nt);
	int v;
	int vi;
	for (int i = 0; i < r.itemsi; i++) {
		v = r.items[i].value;
		if (r.items[i].type == 0) {
			vi = ti(v);
			if (table[n][vi] != -1) conflict(r.nt, v);
			table[n][vi] = ri;
			return;
		}
		int nx = nti(v);
		vi = ti(v + 256);
		if (nx < 0) {
			v = r.items[i].value;
			if (table[n][vi] != -1) conflict(r.nt, v + 256);
			table[n][vi] = ri;
			return;
		}
		for (int t = 0; t < terminalsi; t++) {
			if (seti(firsts[nx], terminals[t])) {
				if (table[n][t] != -1) {
					conflict(r.nt, terminals[t]);
				}
				table[n][t] = ri;
			}
		}
		if (!seti(firsts[nx], 0)) return;
	}
	for (int t = 0; t < terminalsi; t++) {
		if (seti(follows[n], terminals[t])) {
			if (table[n][t] != -1) {
				conflict(r.nt, terminals[t]);
			}
			table[n][t] = ri;
		}
	}
}

static int
nti(int stri)
{
	for (int i = 0; i < nonterminalsi; i++) {
		if (nonterminals[i] == stri) return i;
	}
	return -1;
}

static _Bool
seta(char *s, unsigned int n)
{
	int i = 1<<(n%CHAR_BIT);
	_Bool out;
	s += n/CHAR_BIT;
	if (n >= SETSIZE) return 0; /* just not doing it */
	out = ((*s&i) == 0);
	*s |= i;
	return out;
}

static _Bool
seti(char *s, unsigned int n)
{
	int i = 1<<(n%CHAR_BIT);
	s += n/CHAR_BIT;
	if (n >= SETSIZE) return 0; /* just not doing it */
	return ((*s&i) != 0);
}

static _Bool
setr(char *s, unsigned int n)
{
	int i = 1<<(n%CHAR_BIT);
	_Bool out;
	s += n/CHAR_BIT;
	if (n >= SETSIZE) return 0; /* just not doing it */
	out = ((*s&i) != 0);
	*s &= ~i;
	return out;
}

static _Bool
setu(char *s, char const *t)
{
	_Bool changed = 0;
	for (int i = 0; i < SETCHARS; i++) {
		changed |= ((t[i]&~s[i]) != 0);
		s[i] |= t[i];
	}
	return changed;
}

static int
ti(int stri)
{
	for (int i = 0; i < terminalsi; i++) {
		if (terminals[i] == stri) return i;
	}
	return -1;
}

static _Bool
updatefirsts(struct rule r)
{
	int n = nti(r.nt);
	int nx;
	_Bool changed = 0;
	for (int i = 0; i < r.itemsi; i++) {
		if (r.items[i].type == 0) {
			changed |= seta(firsts[n], r.items[i].value);
			return changed;
		}
		nx = nti(r.items[i].value);
		if (nx < 0) {
			changed |= seta(firsts[n], r.items[i].value + 256);
			return changed;
		}
		if (!seti(firsts[nx], 0)) {
			changed |= setu(firsts[n], firsts[nx]);
			return changed;
		}
		setr(firsts[nx], 0);
		changed |= setu(firsts[n], firsts[nx]);
		seta(firsts[nx], 0);
	}
	return changed | seta(firsts[n], 0);
}

static _Bool
updatefollows(struct rule r)
{
	_Bool changed = 0;
	for (int i = 0; i < r.itemsi; i++) {
		changed |= updatefollowsx(r, i);
	}
	return changed;
}

static _Bool
updatefollowsx(struct rule r, int i)
{
	_Bool changed = 0;
	int n = nti(r.nt);
	int nx;
	if (i < 0 || i >= r.itemsi) return changed;
	if (r.items[i].type == 0) {
		addterm(r.items[i].value);
		return changed;
	}
	nx = nti(r.items[i].value);
	if (nx < 0) {
		addterm(r.items[i].value + 256);
		return changed;
	}
	for (int j = i + 1; j < r.itemsi; j++) {
		int ny;
		if (r.items[j].type == 0) {
			changed |= seta(follows[nx], r.items[j].value);
			return changed;
		}
		ny = nti(r.items[j].value);
		if (ny < 0) {
			changed |= seta(follows[nx], r.items[j].value + 256);
			return changed;
		}
		if (!seti(firsts[ny], 0)) {
			changed |= setu(follows[nx], firsts[ny]);
			return changed;
		}
		setr(firsts[ny], 0);
		changed |= setu(follows[nx], firsts[ny]);
		seta(firsts[ny], 0);
	}
	return changed | setu(follows[nx], follows[n]);
}

int
main(int argc, char **argv)
{
	char *fname = NULL;
	int prey = 0;
	int linenum = 0;
	int outline = 1;
	long rulesstart = 0;
	_Bool go = 1;

	if (argc != 2) {
		fprintf(stderr, "usage: LL1 file.y\n");
		leave(EXIT_FAILURE);
	}
	infile = fopen(argv[1], "r");
	if (!infile) {
		perror(argv[1]);
		leave(EXIT_FAILURE);
	}
	fname = argv[1];
	for (char *s = fname; *s; s++) {
		if (*s == '/') fname = s + 1;
	}
	prey = strlen(fname) - 2;
	if (prey >= 0) {
		if (fname[prey] != '.' || fname[prey + 1] != 'y') prey += 2;
	} else {
		prey += 2;
	}

	/* preamble and variables */
	printf("void %serror(char const *);\n", PREFIX); outline++;
	printf("char const *%sfname;\n", PREFIX); outline++;
	printf("int %sline;\n", PREFIX); outline++;
	printf("int %scol;\n", PREFIX); outline++;
	printf("_Bool %sfail;\n", PREFIX); outline++;
	addstr(""); /* prevent raw 0/256 terminals */
	do {
		lineget(infile); linenum++;
		if (linebuf[0] == '%') {
			go = !handle_directive();
		} else {
			puts(linebuf); outline++;
		}
	} while (go && !feof(infile));
	do {
		lineget(infile); linenum++;
	} while ((!*linebuf || isspace(*linebuf)) && !feof(infile));
	rulesstart = ftell(infile);

	/* gather rule information */
	do {
		addrule(linenum);
		do {
			lineget(infile); linenum++;
		} while (isspace(*linebuf) && !feof(infile));
	} while (!feof(infile));
	mkfirsts();
	mkfollows();
	mktable();

	/* make valstack */
	printf("%s { %s node; %s token; };\n",
	       VALSTACKITEM, typename, typename_tok); outline++;
	printf("static %s *%s;\n", VALSTACKITEM, VALSTACK); outline++;
	printf("static int %s;\n", VALSTACKCAP); outline++;
	printf("static int %s;\n", VALSTACKI); outline++;
	printf("static void %s(%s x) {\n", VALPUSH, VALSTACKITEM); outline++;
	printf("\tif (!%s) {\n", VALSTACK); outline++;
	printf("\t\t%s = 16;\n", VALSTACKCAP); outline++;
	printf("\t\t%s = malloc(%s*sizeof(*%s));\n",
	       VALSTACK, VALSTACKCAP, VALSTACK); outline++;
	printf("\t}\n"); outline++;
	printf("\tif (!%s) {\n", VALSTACK); outline++;
	printf("\t\t%s = 0;\n", VALSTACKCAP); outline++;
	printf("\t\t%s\n", ERRFUNCALL); outline++;
	printf("\t}\n"); outline++;
	printf("\tif (%s >= %s) {\n", VALSTACKI, VALSTACKCAP); outline++;
	printf("\t\t%s *t = realloc(\n", VALSTACKITEM); outline++;
	printf("\t\t\t%s, 2*%s*sizeof(*%s));\n",
	       VALSTACK, VALSTACKCAP, VALSTACK); outline++;
	printf("\t\tif (!t) %s\n", ERRFUNCALL); outline++;
	printf("\t\t%s = t;\n", VALSTACK); outline++;
	printf("\t\t%s *= 2;\n", VALSTACKCAP); outline++;
	printf("\t}\n"); outline++;
	printf("\t%s[%s++] = x;\n}\n\n", VALSTACK, VALSTACKI); outline += 3;

	/* make action function */
	fseek(infile, rulesstart, SEEK_SET);
	printf("static void %sact(int %sn) {\n", PREFIX, PREFIX); outline++;
	printf("\t%s _0 = {0};\n", typename); outline++;
	printf("\tswitch (%sn) {\n", PREFIX); outline++;
	for (int i = 0; i < rulesi; i++) {
		struct rule r = rules[i];
		printf("\tcase %d:\n\t{\n", i); outline += 2;
		printf("\t\t%s -= %d;\n", VALSTACKI, r.itemsi); outline++;
		printf("\t\t%s *%sbase = %s + %s;\n",
		       VALSTACKITEM, PREFIX, VALSTACK, VALSTACKI); outline++;
		printf("\t\t(void)%sbase;\n", PREFIX); outline++;
		for (int j = 0; j < r.itemsi; j++) {
			_Bool isNT = (r.items[j].type == 1);
			if (isNT) isNT = (nti(r.items[j].value) >= 0);
			printf("\t\t%s _%d = %sbase[%d].%s;\n",
			       isNT? typename : typename_tok,
			       j + 1, PREFIX, j,
			       isNT? "node" : "token"); outline++;
		}
		for (int j = 0; j < r.itemsi; j++) {
			printf("\t\t(void)_%d;\n", j + 1); outline++;
		}
		printf("#line %d \"%s\"\n", r.linenum + 1, fname); outline++;
		lineget(infile);
		while((!*linebuf || isspace(*linebuf)) && !feof(infile)) {
			printf("\t%s\n", linebuf); outline++;
			lineget(infile);
		}
		printf("#line %d \"%.*s.c\"\n", outline, prey, fname);
		outline++;
		printf("\t\tbreak;\n\t}\n"); outline += 2;
	}
	printf("\tdefault:\n\t\t%s\n\t\tbreak;\n\t}\n", ERRFUNCALL);
	outline += 4;
	printf("\t%s out = {0};\n", VALSTACKITEM); outline++;
	if (posvars[0][0]) {
		printf("\tif (!_0.%s)\n", posvars[0]);
		printf("\t\t_0.%s = %sfname;\n", posvars[0], PREFIX);
		outline += 2;
	}
	if (posvars[1][0]) {
		printf("\tif (!_0.%s) {\n", posvars[1]);
		printf("\t\t_0.%s = %sline;\n", posvars[1], PREFIX);
		printf("\t\tif (!%scol) _0.%s--;\n", PREFIX, posvars[1]);
		printf("\t}\n");
		outline += 4;
	}
	if (posvars[2][0]) {
		printf("\tif(!_0.%s)\n", posvars[2]);
		printf("\t\t_0.%s = %scol;\n", posvars[2], PREFIX);
		outline += 2;
	}
	printf("\tout.node = _0;\n"); outline++;
	printf("\t%s(out);\n}\n\n", VALPUSH); outline += 3;

	/* make enum for nonterminals */
	printf("enum %snonterminal {\n", PREFIX);
	for (int i = 0; i < nonterminalsi; i++) {
		printf("\t%sNT_%s,\n", PREFIX, stringtab + nonterminals[i]);
	}
	printf("};\n"); outline += nonterminalsi + 2;

	/* make parser types and data */
	printf("/* kind: 0=token, 1=nonterminal, 2=action */\n"); outline++;
	printf("%s { int kind; int value; };\n", INSTACKITEM); outline++;
	printf("static %s *%s;\n", INSTACKITEM, INSTACK); outline++;
	printf("static int %s;\n", INSTACKCAP); outline++;
	printf("static int %s;\n", INSTACKI); outline++;
	printf("static void %s(%s x) {\n", INPUSH, INSTACKITEM); outline++;
	printf("\tif (!%s) {\n", INSTACK); outline++;
	printf("\t\t%s = 16;\n", INSTACKCAP); outline++;
	printf("\t\t%s = malloc(%s*sizeof(*%s));\n",
	       INSTACK, INSTACKCAP, INSTACK); outline++;
	printf("\t}\n"); outline++;
	printf("\tif (!%s) {\n", INSTACK); outline++;
	printf("\t\t%s = 0;\n", INSTACKCAP); outline++;
	printf("\t\t%s\n", ERRFUNCALL); outline++;
	printf("\t}\n"); outline++;
	printf("\tif (%s >= %s) {\n", INSTACKI, INSTACKCAP); outline++;
	printf("\t\t%s *t = realloc(\n", INSTACKITEM); outline++;
	printf("\t\t\t%s, 2*%s*sizeof(*%s));\n",
	       INSTACK, INSTACKCAP, INSTACK); outline++;
	printf("\t\tif (!t) %s\n", ERRFUNCALL); outline++;
	printf("\t\t%s = t;\n", INSTACK); outline++;
	printf("\t\t%s *= 2;\n", INSTACKCAP); outline++;
	printf("\t}\n"); outline++;
	printf("\t%s[%s++] = x;\n}\n\n", INSTACK, INSTACKI); outline += 3;

	/* make parser */
	printf("_Bool %sparse(%s (*next)(void), %s *out) {\n",
	       PREFIX, typename_tok, typename); outline++;
	printf("\tif (!out) return 0;\n"); outline++;
	printf("\t%s = 0;\n", INSTACKI); outline++;
	printf("\t%s((%s){0, %d});\n", INPUSH, INSTACKITEM, FOLLOWEND);
	printf("\t%s((%s){1, 0});\n", INPUSH, INSTACKITEM); outline+= 2;
	printf("\t%s = 0;\n", VALSTACKI); outline++;
	printf("\t%s lab = next();\n", typename_tok); outline++;
	printf("\twhile (%s && !%sfail) {\n", INSTACKI, PREFIX); outline++;
	printf("\t\t%s x = %s[--%s];\n", INSTACKITEM, INSTACK, INSTACKI);
	printf("\t\tif (x.kind == 0) {\n"); outline += 2;
	printf("\t\t\tif (x.value != lab.type) %s\n", ERRFUNCALL); outline++;
	printf("\t\t\tif (lab.type != %d) {\n", FOLLOWEND); outline++;
	printf("\t\t\t\t%s tok = {0};\n", VALSTACKITEM); outline++;
	printf("\t\t\t\ttok.token = lab;\n"); outline++;
	printf("\t\t\t\t%s(tok);\n", VALPUSH); outline++;
	printf("\t\t\t\tlab = next();\n"); outline++;
	printf("\t\t\t}\n"); outline++;
	printf("\t\t} else if (x.kind == 1) {\n"); outline++;
	printf("\t\t\tswitch (x.value) {\n"); outline++;
	for (int i = 0; i < nonterminalsi; i++) {
		printf("\t\t\tcase %sNT_%s:\n", PREFIX,
		       stringtab + nonterminals[i]); outline++;
		printf("\t\t\t\tswitch (lab.type) {\n"); outline++;
		for (int t = 0; t < terminalsi; t++) {
			int ri = table[i][t];
			struct rule r;
			if (ri < 0) continue;
			if (terminals[t] <= 256) {
				printf("\t\t\t\tcase 0x%02x:\n",
				       terminals[t]);
			} else {
				printf("\t\t\t\tcase %s:\n",
				       stringtab + terminals[t] - 256);
			}
			printf("\t\t\t\t\t%s((%s){2,%d});\n",
			       INPUSH, INSTACKITEM, ri);
			r = rules[ri];
			for (int k = r.itemsi - 1; k >= 0; k--) {
				int v = r.items[k].value;
				printf("\t\t\t\t\t%s", INPUSH);
				printf("((%s){", INSTACKITEM);
				if (r.items[k].type == 0) {
					printf("0,0x%02x});\n", v);
				} else if (nti(v) < 0) {
					printf("0,%s});\n", stringtab + v);
				} else {
					printf("1,%sNT_%s});\n",
					       PREFIX, stringtab + v);
				}
			}
			printf("\t\t\t\t\tbreak;\n"); outline += r.itemsi + 3;
		}
		printf("\t\t\t\tdefault:\n"); outline++;
		printf("\t\t\t\t\t%s\n", ERRFUNCALL); outline++;
		printf("\t\t\t\t\tbreak;\n"); outline++;
		printf("\t\t\t\t}\n"); outline++;
		printf("\t\t\t\tbreak;\n"); outline++;
	}
	printf("\t\t\tdefault:\n"); outline++;
	printf("\t\t\t\t%s\n", ERRFUNCALL); outline++;
	printf("\t\t\t\tbreak;\n\t\t\t}\n"); outline += 2;
	printf("\t\t} else if (x.kind == 2) {\n"); outline++;
	printf("\t\t\t%sact(x.value);\n", PREFIX); outline++;
	printf("\t\t} else {\n\t\t\t%s\n\t\t}\n", ERRFUNCALL); outline += 3;
	printf("\t}\n"); outline++;
	printf("\tif (%sfail) {\n", PREFIX); outline++;
	printf("\t\tfree(%s);\n", VALSTACK); outline++;
	printf("\t\t%s = NULL;\n", VALSTACK); outline++;
	printf("\t\t%s = %s = 0;\n", VALSTACKI, VALSTACKCAP); outline++;
	printf("\t\tfree(%s);\n", INSTACK); outline++;
	printf("\t\t%s = NULL;\n", INSTACK); outline++;
	printf("\t\t%s = %s = 0;\n", INSTACKI, INSTACKCAP); outline++;
	printf("\t\treturn 0;\n\t}\n"); outline += 2;
	printf("\t*out = %s[0].node;\n", VALSTACK); outline++;
	printf("\tfree(%s);\n", VALSTACK); outline++;
	printf("\t%s = NULL;\n", VALSTACK); outline++;
	printf("\t%s = %s = 0;\n", VALSTACKI, VALSTACKCAP); outline++;
	printf("\tfree(%s);\n", INSTACK); outline++;
	printf("\t%s = NULL;\n", INSTACK); outline++;
	printf("\t%s = %s = 0;\n", INSTACKI, INSTACKCAP); outline++;
	printf("\treturn 1;\n}\n"); outline += 2;
	leave(EXIT_SUCCESS);
}
