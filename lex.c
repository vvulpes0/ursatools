#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "lex.h"

static FILE *file;
extern int LL1_line;
extern int LL1_col;

static void advance(void);
static void eatline(void);
static struct token read_char(void);
static struct token read_num(long long, int);
static struct token read_symbol(void);
static struct token read_quoted_string(void);

_Bool
alx_usefile(char const *filename)
{
	if (file) fclose(file);
	file = fopen(filename, "r");
	LL1_line = 1;
	LL1_col = 0;
	return file;
}

_Bool
alx_usestdin(void)
{
	file = stdin;
	LL1_line = 1;
	LL1_col = 0;
	return file;
}

void
alx_close(void)
{
	if (file) fclose(file);
	file = NULL;
}

struct token
alx_next(void)
{
	struct token out = {ALX_EOF};
	char c;
	char b;
	if (!file) return out;
	/* advance until next token; die if EOF */
	advance();
	if (feof(file)) return out;
	/* at start of token */
	c = getc(file); LL1_col++;
	if (feof(file)) return out;
	out.type = ALX_ILLCHAR;
	switch (c) {
	case '\n':
		LL1_col = 0;
		LL1_line++;
		out.type = '\n';
		break;
	case ';':
		eatline();
		out.type = '\n';
		break;
	case '<':
	case '>':
		b = getc(file); LL1_col++;
		out.type = c;
		if (feof(file)) break;
		if (b == c) {
			out.type = (c == '<')? ALX_LSHIFT : ALX_RSHIFT;
		} else if (b == '=') {
			out.type = (c == '<')? ALX_LEQ : ALX_GEQ;
		} else if (c == '<' && b == '>') {
			out.type = ALX_NEQ;
		} else {
			ungetc(b, file); LL1_col--;
		}
		break;
	case '"':
		out = read_quoted_string();
		break;
	case '#':
		out = read_num(0, 10);
		out.type = ALX_MARG;
		break;
	case '\'':
		out = read_char();
		break;
	case '%':
	case '&':
	case '(':
	case ')':
	case '*':
	case '+':
	case ',':
	case '-':
	case '/':
	case ':':
	case '=':
	case '[':
	case ']':
	case '^':
	case '|':
	case '~':
		out.type = c;
		break;
	case '0':
		out = read_num(0, 0);
		break;
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		out = read_num(c - '0', 10);
		break;
	default:
		if (isalpha(c) || c == '.' || c == '_')
		{
			ungetc(c, file); LL1_col--;
			out = read_symbol();
		}
		break;
	}
	return out;
}

static void
advance(void)
{
	char c;
	do {
		c = getc(file); LL1_col++;
	} while (!feof(file)
	         && (c == ' ' || c == '\t' || c == '\f' || c == '\r'));
	if (!feof(file)) { ungetc(c, file); }
}

static void
eatline(void)
{
	char c;
	do { c = getc(file); LL1_col++; } while (!feof(file) && c != '\n');
	if (!feof(file)) { LL1_col = 0; LL1_line++; }
}

static _Bool
ishex(int c)
{
	if (c == 'a' || c == 'A') return 1;
	if (c == 'b' || c == 'B') return 1;
	if (c == 'c' || c == 'C') return 1;
	if (c == 'd' || c == 'D') return 1;
	if (c == 'e' || c == 'E') return 1;
	if (c == 'f' || c == 'F') return 1;
	return isdigit(c);
}

static struct token
read_char(void)
{
	struct token out = {ALX_NUM, {0}};
	char c = getc(file); LL1_col++;
	char c2 = getc(file); LL1_col++;
	out.u.num = c;
	if (c == '\\') {
		switch (c2) {
		case 'A':
		case 'a':
			out.u.num = '\a';
			break;
		case 'B':
		case 'b':
			out.u.num = '\b';
			break;
		case 'F':
		case 'f':
			out.u.num = '\f';
			break;
		case 'N':
		case 'n':
			out.u.num = '\n';
			break;
		case 'R':
		case 'r':
			out.u.num = '\r';
			break;
		case 'T':
		case 't':
			out.u.num = '\t';
			break;
		case 'V':
		case 'v':
			out.u.num = '\v';
			break;
		default:
			out.u.num = c2;
			break;
		}
		char c3 = getc(file); LL1_col++;
		if (c3 != '\'') return (struct token){ ALX_ILLCHAR };
		return out;
	}
	if (c2 != '\'') return (struct token){ ALX_ILLCHAR };
	return out;
}

static struct token
read_num(long long value, int base)
{
	struct token out = {ALX_NUM, {value}};
	char c = getc(file); LL1_col++;
	if (feof(file)) return out;
	switch (c) {
	case 'x':
	case 'X':
		if (base == 0) {
			base = 16;
			char b = getc(file);
			if (!feof(file)) { ungetc(b, file); LL1_col--; }
			if (!ishex(b)) return (struct token){ ALX_ILLCHAR };
		} else {
			ungetc(c, file); LL1_col--;
			return out;
		}
		break;
	case 'b':
	case 'B':
		if (base == 0) {
			base = 2;
			char b = getc(file);
			if (!feof(file)) { ungetc(b, file); LL1_col--; }
			if (b != '0' && b != '1') {
				return (struct token){ ALX_ILLCHAR };
			}
		} else {
			ungetc(c, file); LL1_col--;
			return out;
		}
		break;
	case ' ':
		if (base == 0) { return out; }
		break;
	case '0':
	case '1':
	case '2':
	case '3':
	case '4':
	case '5':
	case '6':
	case '7':
	case '8':
	case '9':
		if (base != 10) {
			fprintf(stderr, "leading zeros disallowed\n");
			return (struct token){ ALX_ILLCHAR };
		}
		out.u.num *= base;
		out.u.num += c - '0';
		break;
	default:
		ungetc(c, file); LL1_col--;
		return out;
	}
	do {
		c = getc(file); LL1_col++;
		if (c == ' ') {
			; /* read over space */
		} else if (c == '0' || c == '1') {
			out.u.num *= base;
			out.u.num += c - '0';
		} else if ('0' <= c && c < '0' + base && c <= '9') {
			out.u.num *= base;
			out.u.num += c - '0';
		} else if (base == 16 && ('A' <= c && c <= 'F')) {
			out.u.num *= base;
			out.u.num += c - 'A' + 10;
		} else if (base == 16 && ('a' <= c && c <= 'f')) {
			out.u.num *= base;
			out.u.num += c - 'a' + 10;
		} else {
			if (!feof(file)) { ungetc(c, file); LL1_col--; }
			return out;
		}
	} while (!feof(file));
	return out;
}

static struct token
read_symbol(void)
{
	struct token out = {ALX_SYMBOL};
	char c = getc(file); LL1_col++;
	if (!isalpha(c) && c != '.' && c != '_') {
		out.type = ALX_ILLCHAR;
		return out;
	}
	do {
		out.u.svalue.content[out.u.svalue.length++] = c;
		if (out.u.svalue.length == TOKEN_LENGTH - 1) {
			return out; /* lol just break it */
		}
		c = getc(file); LL1_col++;
	} while (!feof(file)
	         && (isalnum(c) || c == '.' || c == '_' || c == '$'));
	if (!feof(file)) {
		ungetc(c, file);
		LL1_col--;
	}
	int n = out.u.svalue.length + 1;
	if (!strncmp(out.u.svalue.content, ".if", n)) {
		out.type = ALX_IF;
		return out;
	} else if (!strncmp(out.u.svalue.content, ".else", n)) {
		out.type = ALX_ELSE;
		return out;
	} else if (!strncmp(out.u.svalue.content, ".endif", n)) {
		out.type = ALX_ENDIF;
		return out;
	} else if (!strncmp(out.u.svalue.content, ".endm", n)) {
		out.type = ALX_ENDM;
		return out;
	} else if (!strncmp(out.u.svalue.content, ".macro", n)) {
		out.type = ALX_MACRO;
		return out;
	}
	if (out.u.svalue.length == 2) {
		char c0 = tolower(out.u.svalue.content[0]);
		char c1 = tolower(out.u.svalue.content[1]);
		switch (c0) {
		case 'f':
			if (c1 == 'p') {
				out.type = ALX_REG;
				out.u.num = 12;
			}
		case 'p':
			if (c1 == 'c') {
				out.type = ALX_REG;
				out.u.num = 15;
			}
			break;
		case 'r':
			if (isdigit(c1)) {
				out.type = ALX_REG;
				out.u.num = c1 - '0';
				break;
			}
			switch (c1) {
			case 'a':
			case 'b':
			case 'c':
			case 'd':
			case 'e':
			case 'f':
				out.type = ALX_REG;
				out.u.num = c1 - 'a' + 10;
				break;
			case 'p':
				out.type = ALX_REG;
				out.u.num = 14;
				break;
			case 't':
				out.type = ALX_REG;
				out.u.num = 11;
				break;
			default:
				break;
			}
			break;
		case 's':
			if (c1 == 'p') {
				out.type = ALX_REG;
				out.u.num = 13;
			}
			break;
		default:
			break;
		}
	}
	if (out.u.svalue.length == 3) do {
		if (tolower(out.u.svalue.content[0]) != 'r') break;
		if (out.u.svalue.content[1] != '1') break;
		char c2 = tolower(out.u.svalue.content[2]);
		if (c2 < '0' || c2 > '5') break;
		out.type = ALX_REG;
		out.u.num = c2 - '0' + 10;
	} while (0);
	return out;
}

static struct token
read_quoted_string(void)
{
	int bufc = 8;
	char *buf = malloc(bufc);
	int bufi = 0;
	if (!buf) return (struct token){ ALX_ILLCHAR };
	do {
		char c = getc(file); LL1_col++;
		char b;
		if (c == '"') break;
		switch (c) {
		case '\\':
			b = getc(file); LL1_col++;
			switch (b) {
			case 'A': case 'a': c = '\a'; break;
			case 'B': case 'b': c = '\b'; break;
			case 'F': case 'f': c = '\f'; break;
			case 'N': case 'n': c = '\n'; break;
			case 'R': case 'r': c = '\r'; break;
			case 'T': case 't': c = '\t'; break;
			case 'V': case 'v': c = '\v'; break;
			default:            c = b   ; break;
			}
			break;
		default:
			break;
		}
		if (feof(file) || c == '\n') {
			free(buf);
			return (struct token){ ALX_ILLCHAR };
		}
		if (bufi == bufc) {
			char *t = realloc(buf, 2*bufc);
			if (!t) {
				free(buf);
				return (struct token){ ALX_ILLCHAR };
			}
			buf = t;
			bufc *= 2;
		}
		buf[bufi++] = c;
	} while (1);
	if (bufi == bufc) {
		char *t = realloc(buf, 2*bufc);
		if (!t) {
			free(buf);
			return (struct token){ ALX_ILLCHAR };
		}
		buf = t;
		bufc *= 2;
	}
	buf[bufi++] = 0;
	struct token out = { ALX_QSTRING };
	out.u.ntstring = buf;
	return out;
}

void
print_token(struct token tok)
{
	switch (tok.type) {
	case ALX_EOF:
		puts("ALX_EOF");
		break;
	case ALX_ILLCHAR:
		puts("ALX_ILLCHAR");
		break;
	case ALX_NUM:
		printf("ALX_NUM      %lld\n", tok.u.num);
		break;
	case ALX_QSTRING:
		printf("ALX_QSTRING  %s\n", tok.u.ntstring);
		break;
	case ALX_SYMBOL:
		printf("ALX_SYMBOL   %s\n", tok.u.svalue.content);
		break;
	case ALX_REG:
		printf("ALX_REG      R%lld\n", tok.u.num);
		break;
	default:
		if (tok.type >= 0x20) {
			printf("'%c'\n", tok.type);
		} else {
			printf("'\\x%02x'\n", tok.type);
		}
	}
}
