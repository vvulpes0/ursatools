#ifndef TOKEN_H
#define TOKEN_H
#include "common.h"

/**********************************************************************
 * Types
 **********************************************************************/

enum token_type {
	ALX_EOF = 0x100, /* nothing to read */
	ALX_ILLCHAR, /* read an illegal character */
	ALX_NUM, /* numbers! bin (0b), hex (0x), or dec */
	ALX_QSTRING, /* "..." */
	ALX_SYMBOL, /* name, .whatever, mnemonic, whatever */
	ALX_REG, /* r0-r15, ra-rf, rt, rp, pc, sr; case-insensitive */
	ALX_LSHIFT, /* '<<' */
	ALX_RSHIFT, /* '>>' */
	ALX_IF, /* make .if, .else, .endif keywords separate tokens */
	ALX_ELSE,
	ALX_ENDIF,
	ALX_MACRO,
	ALX_ENDM,
	ALX_MARG,
	ALX_LEQ,
	ALX_GEQ,
	ALX_NEQ,
};

struct token {
	int type;
	union {
		long long num;
		struct string svalue;
		char *ntstring;
	} u;
};

/**********************************************************************
 * Functions
 **********************************************************************/

/**
 * Initializes the tokenizer to use the file with the given path.
 * @param fp  the file path
 * @return whether initialization was successful
 */
_Bool alx_usefile(char const *fp);

/**
 * Initializes the tokenizer to use the standard input.
 * @return whether initialization was successful
 */
_Bool alx_usestdin(void);

/**
 * Releases resources held by the tokenizer.
 */
void alx_close(void);

/**
 * Yields the next token in the stream, if any.
 * An invalid token has {@code ALX_ILLCHAR} as its {@code type}.
 * @return the next token
 */
struct token alx_next(void);
#endif
