#include <stdlib.h>
#include "dynarr.h"
#include "lex.h"
#include "node.h"

static inline struct node
complete_binop(struct node _1, struct node _2) {
	if (_2.type == APN_NONE) return _1;
	struct node * target = &_2;
	while (target->a.left) {
		target = (struct node *)da_get(apn_arena, target->a.left);
	}
	target->a.left = apn_retain(&_1);
	if (!target->a.left) LL1_error(NULL);
	return _2;
}
static inline struct node
collect_binop(enum node_type t, struct node _2, struct node _3) {
	struct node new_node = {t, LL1_fname, LL1_line - (LL1_col == 0)};
	new_node.b.right = apn_retain(&_2);
	if (!new_node.b.right) LL1_error(NULL);
	return complete_binop(new_node, _3);
}
%type struct node
%ttype struct token
%pos fname line
%%
Program ::= '\n' Program
	_0 = _2;
Program ::= Statement Programf
	_0 = _1;
	if (_2.type != APN_NONE) {
		struct node *tail = &_0;
		int go = 1;
		int prog = apn_retain(&_2);
		if (!prog) LL1_error(NULL);
		while (tail->a.next && go) {
			go = 0;
			switch (tail->type) {
			case APN_INSTRUCTION:
			case APN_LABEL:
			case APN_SYMBIND:
			case APN_CONDITIONAL:
			case APN_MACRO:
				tail = (struct node *)da_get(
					apn_arena, tail->a.next);
				go = 1;
				break;
			default:
				LL1_error("bad statement type");
				break;
			}
		}
		if (go) {
			switch (tail->type) {
			case APN_INSTRUCTION:
			case APN_LABEL:
			case APN_SYMBIND:
			case APN_CONDITIONAL:
			case APN_MACRO:
				tail->a.next = prog;
				break;
			default:
				LL1_error("bad statement type");
				break;
			}
		}
	}
Program ::=
	_0.type = APN_NONE;
Programf ::= '\n' Program
	_0 = _2;
Programf ::=
	_0.type = APN_NONE;
Statement ::= ALX_MACRO ALX_SYMBOL '\n' Program ALX_ENDM
	_0.type = APN_MACRO;
	_0.line = _4.line;
	_0.c.svalue = _2.u.svalue;
	_0.b.stream = apn_retain(&_4);
	if (!_0.b.stream) LL1_error(NULL);
Statement ::= ALX_IF Expr '\n' Program IfStmtf
	_0.type = APN_CONDITIONAL;
	_0.line = _2.line;
	_0.b.evalue = apn_retain(&_2);
	_0.c.cond.tpath = apn_retain(&_4);
	_0.c.cond.fpath = apn_retain(&_5);
	if (!_0.b.evalue) LL1_error(NULL);
	if (!_0.c.cond.tpath) LL1_error(NULL);
	if (!_0.c.cond.fpath) LL1_error(NULL);
IfStmtf ::= ALX_ELSE '\n' Program ALX_ENDIF
	_0 = _3;
IfStmtf ::= ALX_ENDIF
	_0.type = APN_NONE;
Statement ::= ALX_SYMBOL Statementf
	switch (_2.type) {
	case APN_LABEL:
	case APN_INSTRUCTION:
	case APN_SYMBIND:
		_2.c.svalue = _1.u.svalue;
		break;
	default:
		LL1_error("bad Statementf");
		break;
	}
	_0 = _2;
Statementf ::= '=' Expr
	_0.type = APN_SYMBIND;
	_0.b.evalue = apn_retain(&_2);
	if (!_0.b.evalue) LL1_error(NULL);
Statementf ::= ':' Labelf
	_0.type = APN_LABEL;
	if (_2.type != APN_NONE) {
		_0.a.next = apn_retain(&_2);
		if (!_0.a.next) LL1_error(NULL);
	}
Statementf ::= Arglist
	_0.type = APN_INSTRUCTION;
	if (_1.type != APN_NONE) {
		_0.b.arglist = apn_retain(&_1);
		if (!_0.b.arglist) LL1_error(NULL);
	}
Labelf ::= ALX_SYMBOL Arglist
	_0.type = APN_INSTRUCTION;
	_0.c.svalue = _1.u.svalue;
	if (_2.type != APN_NONE) {
		_0.b.arglist = apn_retain(&_2);
		if (!_0.b.arglist) LL1_error(NULL);
	}
Labelf ::=
	_0.type = APN_NONE;
Arglist ::=
	_0.type = APN_NONE;
Arglist ::= Arg Arglistf
	_0.type = APN_ARGLIST;
	if (_2.type == APN_NONE) {
		_0.a.next = 0;
	} else {
		_0.a.next = apn_retain(&_2);
		if (!_0.a.next) LL1_error(NULL);
	}
	_0.b.arg = apn_retain(&_1);
	if (!_0.b.arg) LL1_error(NULL);
Arglistf ::= ',' Arg Arglistf
	_0.type = APN_ARGLIST;
	if (_3.type == APN_NONE) {
		_0.a.next = 0;
	} else {
		_0.a.next = apn_retain(&_3);
		if (!_0.a.next) LL1_error(NULL);
	}
	_0.b.arg = apn_retain(&_2);
	if (!_0.b.arg) LL1_error(NULL);
Arglistf ::=
	_0.type = APN_NONE;
Arg ::= ALX_QSTRING
	_0.type = APN_ARG_STRING;
	/* steal and release it */
	_0.c.ntstring = apn_retainstr(_1.u.ntstring);
	if (!_0.c.ntstring) LL1_error("BAD QSTRING");
	free(_1.u.ntstring);
Arg ::= ALX_MARG
	_0.type = APN_MARG;
	_0.c.ivalue = _1.u.num;
Arg ::= Expr
	_0.type = APN_ARG_EXPR;
	_0.b.evalue = apn_retain(&_1);
	if (!_0.b.evalue) LL1_error(NULL);
Arg ::= ALX_REG
	_0.type = APN_ARG_REG;
	_0.c.reg = _1.u.num;
Arg ::= '[' ALX_REG ArgCloser
	_0.type = APN_ARG_MEM;
	_0.c.reg = _2.u.num;
	if (_3.type == APN_NONE) {
		_0.b.off = 0;
	} else {
		_0.b.off = apn_retain(&_3);
		if (!_0.b.off) LL1_error(NULL);
	}
ArgCloser ::= ']'
	_0.type = APN_NONE;
ArgCloser ::= ',' Expr ']'
	_0 = _2;
Expr ::= CompareOp
	_0 = _1;
CompareOp ::= OrOp CompareOpf
	_0 = complete_binop(_1, _2);
CompareOpf ::= '<' OrOp
	_0 = collect_binop(APN_LESS, _2, (struct node){APN_NONE});
CompareOpf ::= ALX_LEQ OrOp
	_0 = collect_binop(APN_LESSEQ, _2, (struct node){APN_NONE});
CompareOpf ::= '=' OrOp
	_0 = collect_binop(APN_EQUAL, _2, (struct node){APN_NONE});
CompareOpf ::= ALX_NEQ OrOp
	_0 = collect_binop(APN_UNEQUAL, _2, (struct node){APN_NONE});
CompareOpf ::= ALX_GEQ OrOp
	_0 = collect_binop(APN_GREATEREQ, _2, (struct node){APN_NONE});
CompareOpf ::= '>' OrOp
	_0 = collect_binop(APN_GREATER, _2, (struct node){APN_NONE});
CompareOpf ::=
	_0.type = APN_NONE;
OrOp ::= AndOp OrOpf
	_0 = complete_binop(_1, _2);
OrOpf ::= '|' AndOp OrOpf
	_0 = collect_binop(APN_BITOR, _2, _3);
OrOpf ::= '^' AndOp OrOpf
	_0 = collect_binop(APN_BITXOR, _2, _3);
OrOpf ::=
	_0.type = APN_NONE;
AndOp ::= ShiftOp AndOpf
	_0 = complete_binop(_1, _2);
AndOpf ::= '&' ShiftOp AndOpf
	_0 = collect_binop(APN_BITAND, _2, _3);
AndOpf ::=
	_0.type = APN_NONE;
ShiftOp ::= AddOp ShiftOpf
	_0 = complete_binop(_1, _2);
ShiftOpf ::= ALX_LSHIFT AddOp ShiftOpf
	_0 = collect_binop(APN_BITSHL, _2, _3);
ShiftOpf ::= ALX_RSHIFT AddOp ShiftOpf
	_0 = collect_binop(APN_BITSHR, _2, _3);
ShiftOpf ::=
	_0.type = APN_NONE;
AddOp ::= MulOp AddOpf
	_0 = complete_binop(_1, _2);
AddOpf ::= '+' MulOp AddOpf
	_0 = collect_binop(APN_SUM, _2, _3);
AddOpf ::= '-' MulOp AddOpf
	_0 = collect_binop(APN_DIFFERENCE, _2, _3);
AddOpf ::=
	_0.type = APN_NONE;
MulOp ::= Value MulOpf
	_0 = complete_binop(_1, _2);
MulOpf ::= '*' Value MulOpf
	_0 = collect_binop(APN_PRODUCT, _2, _3);
MulOpf ::= '%' Value MulOpf
	_0 = collect_binop(APN_REMAINDER, _2, _3);
MulOpf ::= '/' Value MulOpf
	_0 = collect_binop(APN_QUOTIENT, _2, _3);
MulOpf ::=
	_0.type = APN_NONE;
Value ::= ALX_SYMBOL MaybeCall
	if (_2.type == APN_NONE) {
		_0.type = APN_SYMBOL;
		_0.c.svalue = _1.u.svalue;
	} else {
		_0.type = APN_FUNCALL;
		_0.c.svalue = _1.u.svalue;
		_0.b.arglist = apn_retain(&_2);
		if (!_0.b.arglist) LL1_error(NULL);
	}
MaybeCall ::= '(' Arglist ')'
	_0 = _2;
MaybeCall ::=
	_0.type = APN_NONE;
Value ::= ALX_NUM
	_0.type = APN_VALUE;
	_0.c.ivalue = _1.u.num;
Value ::= '(' Expr ')'
	_0 = _2;
Value ::= '-' Value
	_0.type = APN_DIFFERENCE;
	{
		struct node left = {0};
		left.type = APN_VALUE;
		left.c.ivalue = 0;
		_0.a.left = apn_retain(&left);
	}
	_0.b.right = apn_retain(&_2);
	if (!_0.a.left || !_0.b.right) LL1_error(NULL);
Value ::= '~' Value
	_0.type = APN_BITXOR;
	{
		struct node left = {0};
		left.type = APN_VALUE;
		left.c.ivalue = -1;
		_0.a.left = apn_retain(&left);
	}
	_0.b.right = apn_retain(&_2);
	if (!_0.a.left || !_0.b.right) LL1_error(NULL);
