#ifndef OBJECT_H
#define OBJECT_H

#include "elf32.h"


/**********************************************************************
 * Types
 **********************************************************************/

struct section {
	Elf32_Shdr h;
	int capacity;
	char *content;
};

struct objfile {
	Elf32_Ehdr h;
	struct section *sections;
};


/**********************************************************************
 * Constructors
 **********************************************************************/

/**
 * Constructs an object file representation.
 * @param type     object type, see elf32.h.
 * @param machine  target architecture, see elf32.h.
 * @param entry    address of entry point; 0 if type is {@code ET_REL}.
 * @return the newly created object file representation
 */
struct objfile obj_new(Elf32_Half type, Elf32_Half machine, Elf32_Addr entry);

/**
 * Constructs an object file representation from the given file.
 * The contents are an owned copy of the file contents.
 * The file is read to exhaustion but not closed.
 * @param out   the object file representation to initialize
 * @param file  the readable file to initialize from
 * @return whether initialization was successful
 */
_Bool obj_load(struct objfile *out, FILE *file);


/**********************************************************************
 * Destructors
 **********************************************************************/

/**
 * Releases all memory owned by this object.
 * The object file representation is a value type and is not itself freed.
 * @param obj  this object
 */
void obj_free(struct objfile obj);


/**********************************************************************
 * Functions
 **********************************************************************/

/**
 * Adds a relocation entry to this section, which should be of type SHT_REL.
 * This section should specify in its {@code sh_info} field
 * the target section for relocation entries.
 * In objects of type {@code ET_REL}, {@code off} is a section offset.
 * In objects of type {@code ET_EXEC}, it should be the VMA of the target.
 * @param sec   this section
 * @param off   the location at which relocation should occur
 * @param type  the relocation type number
 * @param ind   the relevant index into the symbol table
 * @return whether insertion was successful
 */
_Bool obj_addrel(struct section *sec, Elf32_Addr off,
                 unsigned char type, long symi);

/**
 * Appends a new section to the section list of this object.
 * All pointers that were obtained by prior calls
 * to {@code obj_sec} or {@code obj_secn} are invalidated.
 * The first section added should always be a null section,
 * with all parameters zero.
 * For all other sections, ensure that {@code align}
 * is a power of two greater than or equal to one.
 * @param obj    this object
 * @param type   target section type, see {@code enum ELF_SHT} in elf32.h
 * @param flags  target section flags, see {@code enum ELF_SHF} in elf32.h
 * @param align  number that section address must be a multiple of
 * @return the section index of the newly added section
 */
int obj_addsec(struct objfile *obj,
               Elf32_Word type, Elf32_Word flags, Elf32_Word align);

/**
 * Adds an entry to this symbol table.
 * In objects of type {@code ET_REL},
 * the value of the symbol should be its offset from the base of the section.
 * In objects of type {@code ET_EXEC}, it should be the VMA of the symbol.
 * Symbols in section {@code SHN_ABS} are not affected by relocation.
 * Symbols in section {@code SHN_UNDEF} must be found in other objects.
 * @param symtab  this symbol table
 * @param strtab  associated string table
 * @param name    name of the symbol to add; {@code ""} for section symbols
 * @param value   value of the symbol
 * @param size    number of bytes occupied by the symbol, 0 if unknown
 * @param info    binding and visibility, see {@code ELF32_ST_INFO} in elf32.h
 * @param secnum  index of section in which the symbol resides
 * @return the index of the newly added symbol; -1 on failure.
 */
int obj_addsym(struct section *symtab,
               struct section *strtab,
               char const *name,
               Elf32_Word value,
               Elf32_Word size,
               unsigned char info,
               Elf32_Half secnum);

/**
 * Writes this object out to the given file.
 * @param obj   this object
 * @param file  target file
 * @return the number of bytes written; -1 on failure.
 */
int obj_emit(struct objfile obj, FILE *file);

/**
 * Completes the header of this object.
 * @param obj         this object
 * @param e_shstrndx  section index of string table for section headers
 * @return whether the finalization was successful
 */
_Bool obj_finalize(struct objfile *obj, Elf32_Half e_shstrndx);

/**
 * Retrieves an index in this table where the given string can be found.
 * If the target string does not already exist in the table, it is inserted.
 * @param strtab  this table
 * @param str     the string to find
 * @return the location of the target string; -1 if either parameter NULL
 */
int obj_findstr(struct section *strtab, char const *str);

/**
 * Retrieves the relocation in this section at the given index.
 * The section should have type {@code SHT_REL} or {@code SHT_RELA};
 * in the former case, the {@code addend} field returned shall be zero.
 * @param section  this section
 * @param i        the target index
 * @return an {@code Elf32_Rela} structure describing the target relocation
 */
Elf32_Rela obj_getrela(struct section *r, int i);

/**
 * Retrieves the symbol in this section at the given index.
 * This section should have type {@code SHT_SYMTAB}
 * and entry size sufficient to hold an {@code Elf32_Sym} object.
 * @param symtab  this section
 * @param i       the target index
 * @return the target symbol
 */
Elf32_Sym obj_getsym(struct section *symtab, int i);

/**
 * Appends the given 8-bit value to this section.
 * That the value fits in this size is not checked.
 * @param sec    this section
 * @param value  the value
 * @return whether the operation succeeded.
 */
_Bool obj_sappend8(struct section *sec, int value);

/**
 * Appends the given 16-bit little-endian value to this section.
 * That the value fits in this size is not checked.
 * @param sec    this section
 * @param value  the value
 * @return whether the operation succeeded.
 */
_Bool obj_sappend16(struct section *sec, int value);

/**
 * Appends the given 32-bit little-endian value to this section.
 * That the value fits in this size is not checked.
 * @param sec    this section
 * @param value  the value
 * @return whether the operation succeeded.
 */
_Bool obj_sappend32(struct section *sec, long value);

/**
 * Retrieves the section with the given index.
 * The resulting pointer is valid only if the target section exists.
 * It is invalidated if a section is later added with obj_addsec.
 * @param obj  this object
 * @param n    the desired index
 * @return a pointer to the target section.
 */
struct section *obj_sec(struct objfile obj, int n);

/**
 * Retrieves the first section with the given name.
 * The resulting pointer is valid only if the target section exists.
 * It is invalidated if a section is later added with obj_addsec.
 * @param obj   this object
 * @param name  the desired section
 * @return a pointer to the target section; NULL if not found
 */
struct section *obj_secn(struct objfile obj, char const *name);

/**
 * Replaces the symbol in this section at the given index.
 * This section should have type {@code SHT_SYMTAB}
 * and entry size sufficient to hold an {@code Elf32_Sym} object.
 * @param symtab  this section
 * @param i       the target index
 * @param sym     the new symbol
 */
void obj_setsym(struct section *symtab, int i, Elf32_Sym sym);

#endif
