#include "reloc.h"
struct node;

/***********************************************************************
 * Types
 ***********************************************************************/

struct ins_desc {
	enum reloc rtype;
	unsigned short encoding;
};

/***********************************************************************
 * Functions
 ***********************************************************************/
/**
 * Attempts to find an instruction compatible with the given tree node,
 * which should have type {@code APN_INSTRUCTION}. If no appropriate
 * instruction is found, then the {@code rtype} field of the result
 * will be {@code R_URSA_ILLEGAL}.
 * @param node  the instruction's tree node
 * @return the description of an appropriate instruction
 */
struct ins_desc ins_desc(struct node const *node);
