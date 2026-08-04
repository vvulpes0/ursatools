#include <string.h>

#include "dynarr.h"
#include "instr.h"
#include "node.h"
#include "reloc.h"

enum iargtype {
	INS_NONE, /* arg not present */
	INS_REG,
	INS_SREG,
	INS_EXPR,
	INS_MEM,
	INS_ILLEGAL = 0xDEAD, /* arg bad type */
};
struct instr {
	char const *name;
	enum iargtype arg1type;
	enum iargtype arg2type;
	unsigned short base;
	enum reloc rtype;
};


static enum iargtype ins_type(struct node *);

static struct instr const instrs[] = {
{ "ior" ,  INS_REG,  INS_EXPR, 0x2000, R_URSA_ALU4  },
{ "iors",  INS_REG,  INS_EXPR, 0x2800, R_URSA_ALU4  },
{ "clr" ,  INS_REG,  INS_EXPR, 0x1000, R_URSA_ALU4  },
{ "clrs",  INS_REG,  INS_EXPR, 0x1800, R_URSA_ALU4  },
{ "add" ,  INS_REG,  INS_EXPR, 0x4000, R_URSA_ALU4  },
{ "adds",  INS_REG,  INS_EXPR, 0x4800, R_URSA_ALU4  },
{ "sub" ,  INS_REG,  INS_EXPR, 0x5000, R_URSA_ALU4  },
{ "subs",  INS_REG,  INS_EXPR, 0x5800, R_URSA_ALU4  },
{ "adx" ,  INS_REG,  INS_EXPR, 0x6000, R_URSA_ALU4  },
{ "adxs",  INS_REG,  INS_EXPR, 0x6800, R_URSA_ALU4  },
{ "sbx" ,  INS_REG,  INS_EXPR, 0x7000, R_URSA_ALU4  },
{ "sbxs",  INS_REG,  INS_EXPR, 0x7800, R_URSA_ALU4  },
{ "and" ,  INS_REG,  INS_EXPR, 0x0000, R_URSA_ALU4  },
{ "ands",  INS_REG,  INS_EXPR, 0x0800, R_URSA_ALU4  },
{ "xor" ,  INS_REG,  INS_EXPR, 0x3000, R_URSA_ALU4  },
{ "xors",  INS_REG,  INS_EXPR, 0x3800, R_URSA_ALU4  },

{ "ior" ,  INS_REG,  INS_REG,  0x8200, R_URSA_NONE  },
{ "iors",  INS_REG,  INS_REG,  0x8A00, R_URSA_NONE  },
{ "clr" ,  INS_REG,  INS_REG,  0x8100, R_URSA_NONE  },
{ "clrs",  INS_REG,  INS_REG,  0x8900, R_URSA_NONE  },
{ "add" ,  INS_REG,  INS_REG,  0x8400, R_URSA_NONE  },
{ "adds",  INS_REG,  INS_REG,  0x8C00, R_URSA_NONE  },
{ "sub" ,  INS_REG,  INS_REG,  0x8500, R_URSA_NONE  },
{ "subs",  INS_REG,  INS_REG,  0x8D00, R_URSA_NONE  },
{ "adx" ,  INS_REG,  INS_REG,  0x8600, R_URSA_NONE  },
{ "adxs",  INS_REG,  INS_REG,  0x8E00, R_URSA_NONE  },
{ "sbx" ,  INS_REG,  INS_REG,  0x8700, R_URSA_NONE  },
{ "sbxs",  INS_REG,  INS_REG,  0x8F00, R_URSA_NONE  },
{ "and" ,  INS_REG,  INS_REG,  0x8000, R_URSA_NONE  },
{ "ands",  INS_REG,  INS_REG,  0x8800, R_URSA_NONE  },
{ "xor" ,  INS_REG,  INS_REG,  0x8300, R_URSA_NONE  },
{ "xors",  INS_REG,  INS_REG,  0x8B00, R_URSA_NONE  },

{ "lsl",   INS_REG,  INS_REG,  0x9000, R_URSA_NONE  },
{ "lsls",  INS_REG,  INS_REG,  0x9800, R_URSA_NONE  },
{ "lsr",   INS_REG,  INS_REG,  0x9100, R_URSA_NONE  },
{ "lsrs",  INS_REG,  INS_REG,  0x9900, R_URSA_NONE  },
{ "asr",   INS_REG,  INS_REG,  0x9200, R_URSA_NONE  },
{ "asrs",  INS_REG,  INS_REG,  0x9A00, R_URSA_NONE  },
{ "lsls",  INS_REG,  INS_EXPR, 0x9400, R_URSA_ABS5  },
{ "lsrs",  INS_REG,  INS_EXPR, 0x9500, R_URSA_ABS5  },
{ "asrs",  INS_REG,  INS_EXPR, 0x9600, R_URSA_ABS5  },
{ "rlcs",  INS_REG,  INS_NONE, 0x9700, R_URSA_NONE  },
{ "rrcs",  INS_REG,  INS_NONE, 0x9F00, R_URSA_NONE  },

{ "mov",   INS_REG,  INS_SREG, 0xA00F, R_URSA_NONE  },
{ "mov",   INS_SREG, INS_REG,  0xA80F, R_URSA_NONE  },

{ "b",     INS_EXPR, INS_NONE, 0xB000, R_URSA_PC8   },
{ "beq",   INS_EXPR, INS_NONE, 0xB100, R_URSA_PC8   },
{ "bz",    INS_EXPR, INS_NONE, 0xB100, R_URSA_PC8   },
{ "bmi",   INS_EXPR, INS_NONE, 0xB200, R_URSA_PC8   },
{ "bvs",   INS_EXPR, INS_NONE, 0xB300, R_URSA_PC8   },
{ "bcs",   INS_EXPR, INS_NONE, 0xB400, R_URSA_PC8   },
{ "blo",   INS_EXPR, INS_NONE, 0xB400, R_URSA_PC8   },
{ "bls",   INS_EXPR, INS_NONE, 0xB500, R_URSA_PC8   },
{ "blt",   INS_EXPR, INS_NONE, 0xB600, R_URSA_PC8   },
{ "ble",   INS_EXPR, INS_NONE, 0xB700, R_URSA_PC8   },
{ "bf",    INS_EXPR, INS_NONE, 0xB800, R_URSA_PC8   },
{ "bne",   INS_EXPR, INS_NONE, 0xB900, R_URSA_PC8   },
{ "bnz",   INS_EXPR, INS_NONE, 0xB900, R_URSA_PC8   },
{ "bpl",   INS_EXPR, INS_NONE, 0xBA00, R_URSA_PC8   },
{ "bvc",   INS_EXPR, INS_NONE, 0xBB00, R_URSA_PC8   },
{ "bcc",   INS_EXPR, INS_NONE, 0xBC00, R_URSA_PC8   },
{ "bhs",   INS_EXPR, INS_NONE, 0xBC00, R_URSA_PC8   },
{ "bhi",   INS_EXPR, INS_NONE, 0xBD00, R_URSA_PC8   },
{ "bge",   INS_EXPR, INS_NONE, 0xBE00, R_URSA_PC8   },
{ "bgt",   INS_EXPR, INS_NONE, 0xBF00, R_URSA_PC8   },

{ "mov",   INS_REG,  INS_REG,  0xC000, R_URSA_NONE  },
{ "moveq", INS_REG,  INS_REG,  0xC100, R_URSA_NONE  },
{ "movz",  INS_REG,  INS_REG,  0xC100, R_URSA_NONE  },
{ "movmi", INS_REG,  INS_REG,  0xC200, R_URSA_NONE  },
{ "movvs", INS_REG,  INS_REG,  0xC300, R_URSA_NONE  },
{ "movcs", INS_REG,  INS_REG,  0xC400, R_URSA_NONE  },
{ "movlo", INS_REG,  INS_REG,  0xC400, R_URSA_NONE  },
{ "movls", INS_REG,  INS_REG,  0xC500, R_URSA_NONE  },
{ "movlt", INS_REG,  INS_REG,  0xC600, R_URSA_NONE  },
{ "movle", INS_REG,  INS_REG,  0xC700, R_URSA_NONE  },
{ "movf",  INS_REG,  INS_REG,  0xC800, R_URSA_NONE  },
{ "movne", INS_REG,  INS_REG,  0xC900, R_URSA_NONE  },
{ "movnz", INS_REG,  INS_REG,  0xC900, R_URSA_NONE  },
{ "movpl", INS_REG,  INS_REG,  0xCA00, R_URSA_NONE  },
{ "movvc", INS_REG,  INS_REG,  0xCB00, R_URSA_NONE  },
{ "movcc", INS_REG,  INS_REG,  0xCC00, R_URSA_NONE  },
{ "movhs", INS_REG,  INS_REG,  0xCC00, R_URSA_NONE  },
{ "movhi", INS_REG,  INS_REG,  0xCD00, R_URSA_NONE  },
{ "movge", INS_REG,  INS_REG,  0xCE00, R_URSA_NONE  },
{ "movgt", INS_REG,  INS_REG,  0xCF00, R_URSA_NONE  },

{ "ld",    INS_REG,  INS_MEM,  0xD800, R_URSA_WOFF3 },
{ "ldb",   INS_REG,  INS_MEM,  0xD000, R_URSA_BOFF3 },
{ "sto",   INS_MEM,  INS_REG,  0xE800, R_URSA_WOFF3 },
{ "stob",  INS_MEM,  INS_REG,  0xE000, R_URSA_BOFF3 },

{ "sset",  INS_REG,  INS_EXPR, 0xF000, R_URSA_SSET8 },
};

static struct node *
get(int i)
{
	if (!i) return NULL;
	return da_get(apn_arena, i);
}

static enum iargtype
ins_type(struct node *node)
{
	if (!node) return INS_NONE;
	switch (node->type) {
	case APN_ARG_EXPR: return INS_EXPR;
	case APN_ARG_MEM:  return (node->c.reg < 16)? INS_MEM : INS_ILLEGAL;
	case APN_ARG_REG:  return (node->c.reg < 16)? INS_REG : INS_SREG;
	default: return INS_ILLEGAL;
	}
	/* how did we get here? */
	return INS_ILLEGAL;
}

struct ins_desc
ins_desc(struct node const *node)
{
	struct ins_desc out = {R_URSA_ILLEGAL};
	if (!node) return out;
	struct node *args = get(node->b.arglist);
	if (!args || args->type != APN_ARGLIST) return out;
	int nargs = apn_arglen(args);
	if (nargs < 0 || nargs > 2) return out;
	enum iargtype t1 = ins_type(get(apn_arg(args, 0)));
	enum iargtype t2 = ins_type(get(apn_arg(args, 1)));
	char const *name = node->c.svalue.content;
	int n = node->c.svalue.length + 1;
	for (int i = 0; i < sizeof(instrs)/sizeof(*instrs); i++) {
		if (strncmp(name, instrs[i].name, n)) continue;
		if (instrs[i].arg1type != t1) continue;
		if (instrs[i].arg2type != t2) continue;
		out.rtype = instrs[i].rtype;
		out.encoding = instrs[i].base;
		int ra = (t1 == INS_REG || t1 == INS_MEM)?
			get(apn_arg(args, 0))->c.reg : 0;
		int rb = (t2 == INS_REG || t2 == INS_MEM)?
			get(apn_arg(args, 1))->c.reg : 0;
		switch (out.encoding) {
		case 0xA80F: /* sr is first, rA is second */
			ra = rb;
			rb = 0;
			break;
		default:
			if ((out.encoding & 0xF000) == 0xE000) {
				/* [rB,X] is first; rA is second */
				ra ^= rb;
				rb ^= ra;
				ra ^= rb;
			}
			break;
		}
		out.encoding |= (ra<<4)|rb;
		break;
	}
	return out;
}
