#ifndef RELOC_H
#define RELOC_H


/**********************************************************************
 * Types
 **********************************************************************/

enum reloc {
	R_URSA_NONE,             /* not used */
	R_URSA_ABS8,             /* .byte */
	R_URSA_ABS16,            /* .hword */
	R_URSA_ABS32,            /* .word */
	R_URSA_ALU4,             /* basic instructions */
	R_URSA_SSET8,            /* sset - value in a byte */
	R_URSA_SSET8_0_NC,       /* sset - larger value ...X byte */
	R_URSA_SSET8_1_NC,       /* sset - larger value ..X. byte */
	R_URSA_SSET8_2_NC,       /* sset - larger value .X.. byte */
	R_URSA_SSET8_3_NC,       /* sset - larger value X... byte */
	R_URSA_ABS5,             /* shifts */
	R_URSA_BOFF3,            /* offset in byte-size load/store */
	R_URSA_WOFF3,            /* offset in word-size load/store */
	R_URSA_PC8,              /* b[cc] */
	R_URSA_ILLEGAL = 0xDEAD, /* out of range :: indicates failure */
};


/**********************************************************************
 * Functions
 **********************************************************************/

/**
 * Applies this relocation on the given buffer.
 * The base addend is extracted, then a new value is computed
 * as required for this relocation from the supplied target location,
 * symbol value, and extended addend.
 * Note that operation takes place at the beginning of the buffer;
 * in particular it is <b>not</b> offset by {@code place}.
 * Relocation may fail if the computed result cannot be stored
 * according to this relocation.
 * @param r      this relocation
 * @param buf    the buffer
 * @param place  address of fixup location
 * @param value  symbol value
 * @param xadd   extended addend
 * @return whether relocation was successful
 */
_Bool rel_apply(enum reloc r, unsigned char *buf,
                long long place, long long value, long long xadd);

/**
 * Retrieves the base addend for this relocation from the given buffer.
 * There must be sufficient space to read ahead as many bytes
 * as are used by this relocation.
 * @param r    this relocation
 * @param buf  the buffer
 * @return the stored value, transformed as required by this relocation
 */
long long rel_extract(enum reloc r, unsigned char const *buf);

#endif
