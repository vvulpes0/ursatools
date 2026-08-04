#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "object.h"

#include "dynarr.h"

static int emit8(int value, FILE *file);
static int emit16(int value, FILE *file);
static int emit32(int value, FILE *file);
static int emit_Ehdr(Elf32_Ehdr h, FILE *file);
static int emit_Shdr(Elf32_Shdr h, FILE *file);
static int readaddr(Elf32_Addr *out, unsigned char const *buf);
static int readhalf(Elf32_Half *out, unsigned char const *buf);
static int readoff(Elf32_Off *out, unsigned char const *buf);
static int readsword(Elf32_Sword *out, unsigned char const *buf);
static int readuchr(unsigned char *out, unsigned char const *buf);
static int readword(Elf32_Word *out, unsigned char const *buf);
static int readehdr(Elf32_Ehdr *h, unsigned char const *buf, int len);
static int readsec(struct section *out, unsigned char const *buf,
                   int shoff, int len);
static int writeaddr(Elf32_Addr *in, unsigned char *buf);
static int writehalf(Elf32_Half *in, unsigned char *buf);
static int writeuchr(unsigned char *in, unsigned char *buf);
static int writeword(Elf32_Word *in, unsigned char *buf);


/* CONSTRUCTORS *******************************************************/
struct objfile
obj_new(Elf32_Half type,
        Elf32_Half machine,
        Elf32_Addr entry)
{
	struct objfile obj = {(Elf32_Ehdr){0}, NULL};
	obj.h.e_ident[0] = 0x7F;
	obj.h.e_ident[1] = 'E';
	obj.h.e_ident[2] = 'L';
	obj.h.e_ident[3] = 'F';
	obj.h.e_ident[4] = ELFCLASS32;
	obj.h.e_ident[5] = ELFDATA2LSB;
	obj.h.e_ident[6] = EV_CURRENT;
	obj.h.e_type = type;
	obj.h.e_machine = machine;
	obj.h.e_version = EV_CURRENT;
	obj.h.e_entry = entry;
	obj.h.e_ehsize = sizeof(obj.h);
	return obj;
}

/* DESTRUCTORS ********************************************************/
void
obj_free(struct objfile obj)
{
	if (!obj.sections) return;
	for (int i = 0; i < obj.h.e_shnum; i++) {
		free(obj_sec(obj, i)->content);
		obj_sec(obj, i)->content = NULL;
	}
	free(obj.sections);
}

/* PUBLIC FUNCTIONS ***************************************************/
int
obj_addsec(struct objfile *obj,
           Elf32_Word sh_type,
           Elf32_Word sh_flags,
           Elf32_Word sh_addralign)
{
	if (!obj) return -1;
	struct section *seclist = realloc(
		obj->sections,
		(obj->h.e_shnum + 1) * sizeof(*seclist)
	);
	if (!seclist) return -1;
	obj->sections = seclist;
	obj->sections[obj->h.e_shnum++] = (struct section){0};
	struct section *s = obj->sections + obj->h.e_shnum - 1;
	s->h.sh_type = sh_type;
	s->h.sh_flags = sh_flags;
	s->h.sh_addralign = sh_addralign;
	return obj->h.e_shnum - 1;
}

_Bool
obj_addrel(struct section *sec, Elf32_Addr off, unsigned char type, long symi)
{
	return obj_sappend32(sec, off)
	       && obj_sappend32(sec, Elf32_R_INFO(symi, type));
}

int
obj_addsym(struct section *symtab,
           struct section *strtab,
           char const *name,
           Elf32_Word value,
           Elf32_Word size,
           unsigned char info,
           Elf32_Half secnum)
{
	if (!symtab || !strtab || !name) return -1;
	int namei = obj_findstr(strtab, name);
	obj_sappend32(symtab, namei);
	obj_sappend32(symtab, value);
	obj_sappend32(symtab, size);
	obj_sappend8(symtab, info);
	obj_sappend8(symtab, 0);
	obj_sappend16(symtab, secnum);
	return namei;
}

int
obj_emit(struct objfile obj, FILE *file)
{
	int pos = 0;
	pos += emit_Ehdr(obj.h, file);
	for (int i = 0; i < obj.h.e_shnum; i++) {
		struct section *s = obj_sec(obj, i);
		if (!s) return -1;
		if (s->h.sh_addralign) {
			int align = s->h.sh_addralign;
			while (align && pos%align) pos += emit8(0, file);
		}
		int sz = s->h.sh_size;
		if (!sz) continue;
		if (s->h.sh_type != SHT_NOBITS) {
			if (!fwrite(s->content, sz, 1, file)) return -1;
		}
		pos += sz;
	}
	while (pos < obj.h.e_shoff) pos += emit8(0, file);
	for (int i = 0; i < obj.h.e_shnum; i++) {
		struct section *s = obj_sec(obj, i);
		if (!s) return -1;
		pos += emit_Shdr(s->h, file);
	}
	return pos;
}

_Bool
obj_finalize(struct objfile *obj, Elf32_Half e_shstrndx)
{
	if (!obj) return 0;
	int pos = obj->h.e_ehsize;
	if (obj->h.e_shnum == 0) return 1;
	obj->h.e_shstrndx = e_shstrndx;
	obj->h.e_shentsize = sizeof(Elf32_Shdr);
	/* first section header MUST be all-bits zero, no touchy */
	for (int i = 1; i < obj->h.e_shnum; i++) {
		int align = obj->sections[i].h.sh_addralign;
		if (align && pos%align) pos += align - pos%align;
		obj->sections[i].h.sh_off = pos;
		pos += obj->sections[i].h.sh_size;
	}
	int align = 4;
	if (pos%align) pos += align - pos%align;
	obj->h.e_shoff = pos;
	return 1;
}

int
obj_findstr(struct section *strtab, char const *str)
{
	if (!strtab || !str) return -1;
	int len = strlen(str) + 1;
	for (int i = 0; strtab->content && i < strtab->h.sh_size; i++) {
		if (strncmp(str, strtab->content + i, len) == 0) {
			return i;
		}
	}
	int out = strtab->h.sh_size;
	for (int i = 0; i <= strlen(str); i++) {
		obj_sappend8(strtab, str[i]);
	}
	return out;
}

Elf32_Rela
obj_getrela(struct section *r, int i)
{
	Elf32_Rela out = {0};
	if (!r || !r->content) return out;
	if (!r->h.sh_entsize) return out;
	if ((i+1) * r->h.sh_entsize > r->h.sh_size) return out;
	int off = i*r->h.sh_entsize;
	unsigned char *buf = (unsigned char *)r->content;
	off += readaddr(&out.r_offset, buf + off);
	off += readword(&out.r_info, buf + off);
	if (r->h.sh_type == SHT_RELA) {
		off += readsword(&out.r_addend, buf + off);
	}
	return out;
}

Elf32_Sym
obj_getsym(struct section *symtab, int i)
{
	Elf32_Sym out = {0};
	if (!symtab || !symtab->content) return out;
	if (symtab->h.sh_entsize < sizeof(Elf32_Sym)) return out;
	if (symtab->h.sh_size / symtab->h.sh_entsize < i + 1) return out;
	int off = i * symtab->h.sh_entsize;
	unsigned char *buf = (unsigned char *)symtab->content;
	off += readword(&out.st_name,  buf + off);
	off += readaddr(&out.st_value, buf + off);
	off += readword(&out.st_size,  buf + off);
	off += readuchr(&out.st_info,  buf + off);
	off += readuchr(&out.st_other, buf + off);
	off += readhalf(&out.st_shndx, buf + off);
	return out;
}

_Bool
obj_load(struct objfile *out, FILE *file)
{
	if (!out || !file) return 0;
	struct dynarr mfile = da_new(1);
	char c = getc(file);
	while (!feof(file)) {
		da_append(&mfile, &c);
		c = getc(file);
	}

	int hsize = readehdr(&out->h, mfile.content, mfile.length);
	Elf32_Ehdr *h = &out->h;
	_Bool valid = 1;
	if (hsize < 0 || hsize > h->e_ehsize) valid = 0;
	if (memcmp(h->e_ident, "\x7F" "ELF", 4)) valid = 0;
	if (h->e_shoff + h->e_shentsize*h->e_shnum > mfile.length) valid = 0;
	if (h->e_shstrndx && h->e_shstrndx >= h->e_shnum) valid = 0;
	if (!valid) { da_free(mfile); return 0; }

	if (!h->e_shnum) { out->sections = NULL; da_free(mfile); return 1; }
	if (h->e_shentsize < sizeof(Elf32_Shdr)) { da_free(mfile); return 1; }
	out->sections = malloc(h->e_shnum * sizeof(struct section));
	if (!out->sections) { da_free(mfile); return 0; }

	for (int i = 0; i < h->e_shnum; i++) {
		out->sections[i] = (struct section){0};
		out->sections[i].content = NULL;
	}
	for (int i = 0; i < h->e_shnum; i++) {
		int off = h->e_shoff + i*h->e_shentsize;
		int sz = readsec(out->sections + i,
		                 mfile.content,
		                 off,
		                 mfile.length);
		if (sz < 0) {
			obj_free(*out);
			out->sections = NULL;
			da_free(mfile);
			return 0;
		}
	}

	da_free(mfile);
	return 1;
}

_Bool
obj_sappend8(struct section *sec, int value)
{
	if (!sec) return 0;
	if (sec->h.sh_size == sec->capacity) {
		int cap = sec->capacity? 2*sec->capacity : 16;
		char *buf = realloc(sec->content, cap);
		if (!buf) return 0;
		sec->content = buf;
		sec->capacity = cap;
	}
	sec->content[sec->h.sh_size++] = (char)value;
	return 1;
}
_Bool
obj_sappend16(struct section *sec, int value)
{
	return obj_sappend8(sec, value)
	    && obj_sappend8(sec, value>>8);
}
_Bool
obj_sappend32(struct section *sec, long value)
{
	return obj_sappend8(sec, value)
	    && obj_sappend8(sec, value>>8)
	    && obj_sappend8(sec, value>>16)
	    && obj_sappend8(sec, value>>24);
}

struct section *
obj_sec(struct objfile obj, int n)
{
	return &obj.sections[n];
}

struct section *
obj_secn(struct objfile obj, char const *name)
{
	int n = strlen(name) + 1;
	int shstrtabi = obj.h.e_shstrndx;
	if (shstrtabi >= obj.h.e_shnum) return NULL;
	struct section *shstrtab = obj_sec(obj, shstrtabi);
	for (int i = 0; i < obj.h.e_shnum; i++) {
		struct section *s = obj_sec(obj, i);
		if (!strncmp(shstrtab->content + s->h.sh_name, name, n)) {
			return s;
		}
	}
	return NULL;
}

void
obj_setsym(struct section *symtab, int i, Elf32_Sym sym)
{
	if (!symtab || !symtab->content) return;
	if (symtab->h.sh_entsize < sizeof(Elf32_Sym)) return;
	if (symtab->h.sh_size / symtab->h.sh_entsize < i + 1) return;
	int off = i * symtab->h.sh_entsize;
	unsigned char *buf = (unsigned char *)symtab->content;
	off += writeword(&sym.st_name,  buf + off);
	off += writeaddr(&sym.st_value, buf + off);
	off += writeword(&sym.st_size,  buf + off);
	off += writeuchr(&sym.st_info,  buf + off);
	off += writeuchr(&sym.st_other, buf + off);
	off += writehalf(&sym.st_shndx, buf + off);
}

/* PRIVATE FUNCTIONS **************************************************/
static int
emit8(int value, FILE *file)
{
	putc(value&0xFF, file);
	return 1;
}
static int
emit16(int value, FILE *file)
{
	emit8(value, file);
	emit8(value>>8, file);
	return 2;
}
static int
emit32(int value, FILE *file)
{
	emit16(value, file);
	emit16(value>>16, file);
	return 4;
}

static int
emit_Ehdr(Elf32_Ehdr h, FILE *file)
{
	int i = 0;
	for (i = 0; i < EI_NIDENT; i++) emit8(h.e_ident[i], file);
	i += emit16(h.e_type, file);
	i += emit16(h.e_machine, file);
	i += emit32(h.e_version, file);
	i += emit32(h.e_entry, file);
	i += emit32(h.e_phoff, file);
	i += emit32(h.e_shoff, file);
	i += emit32(h.e_flags, file);
	i += emit16(h.e_ehsize, file);
	i += emit16(h.e_phentsize, file);
	i += emit16(h.e_phnum, file);
	i += emit16(h.e_shentsize, file);
	i += emit16(h.e_shnum, file);
	i += emit16(h.e_shstrndx, file);
	while (i < h.e_ehsize) i += emit8(0, file);
	return i;
}
static int
emit_Shdr(Elf32_Shdr h, FILE *file)
{
	int i = 0;
	i += emit32(h.sh_name, file);
	i += emit32(h.sh_type, file);
	i += emit32(h.sh_flags, file);
	i += emit32(h.sh_addr, file);
	i += emit32(h.sh_off, file);
	i += emit32(h.sh_size, file);
	i += emit32(h.sh_link, file);
	i += emit32(h.sh_info, file);
	i += emit32(h.sh_addralign, file);
	i += emit32(h.sh_entsize, file);
	return i;
}

static int
readaddr(Elf32_Addr *out, unsigned char const *buf)
{
	return readword(out, buf);
}

static int
readehdr(Elf32_Ehdr *h, unsigned char const *buf, int len)
{
	if (!h || !buf || len < 0x34) return -1;
	int i = 0;
	for (; i < EI_NIDENT; i++) h->e_ident[i] = buf[i];
	i += readhalf(&h->e_type,      buf + i);
	i += readhalf(&h->e_machine,   buf + i);
	i += readword(&h->e_version,   buf + i);
	i += readaddr(&h->e_entry,     buf + i);
	i += readoff (&h->e_phoff,     buf + i);
	i += readoff (&h->e_shoff,     buf + i);
	i += readword(&h->e_flags,     buf + i);
	i += readhalf(&h->e_ehsize,    buf + i);
	i += readhalf(&h->e_phentsize, buf + i);
	i += readhalf(&h->e_phnum,     buf + i);
	i += readhalf(&h->e_shentsize, buf + i);
	i += readhalf(&h->e_shnum,     buf + i);
	i += readhalf(&h->e_shstrndx,  buf + i);
	return i;
}

static int
readhalf(Elf32_Half *out, unsigned char const *buf)
{
	if (!out || !buf) return -1;
	*out = buf[0] + (buf[1]<<8);
	return 2;
}

static int
readoff(Elf32_Off *out, unsigned char const *buf)
{
	return readword(out, buf);
}

static int
readsec(struct section *out, unsigned char const *buf, int shoff, int length)
{
	if (!out || !buf || shoff + sizeof(Elf32_Shdr) > length) return -1;
	int i = 0;
	i += readword(&out->h.sh_name,      buf + shoff + i);
	i += readword(&out->h.sh_type,      buf + shoff + i);
	i += readword(&out->h.sh_flags,     buf + shoff + i);
	i += readaddr(&out->h.sh_addr,      buf + shoff + i);
	i += readoff (&out->h.sh_off,       buf + shoff + i);
	i += readword(&out->h.sh_size,      buf + shoff + i);
	i += readword(&out->h.sh_link,      buf + shoff + i);
	i += readword(&out->h.sh_info,      buf + shoff + i);
	i += readword(&out->h.sh_addralign, buf + shoff + i);
	i += readword(&out->h.sh_entsize,   buf + shoff + i);
	out->capacity = 0;
	out->content = NULL;
	if (out->h.sh_type != SHT_NOBITS) {
		if (out->h.sh_off + out->h.sh_size > length) return -1;
	}
	if (out->h.sh_size < 0) return -1;
	if (out->h.sh_size == 0) return i;
	if (out->h.sh_type == SHT_NOBITS) {
		out->content = calloc(out->h.sh_size, 1);
		if (!out->content) return -1;
	} else {
		out->content = malloc(out->h.sh_size);
		if (!out->content) return -1;
		out->capacity = out->h.sh_size;
		memcpy(out->content, buf + out->h.sh_off, out->capacity);
	}
	return i;
}

static int
readsword(Elf32_Sword *out, unsigned char const *buf)
{
	if (!out || !buf) return -1;
	unsigned long long p
		= buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24);
	if (p < 0x80000000ULL) {
		*out = (Elf32_Sword)(p&0xFFFFFFFF);
	} else {
		p = ~p + 1;
		*out = -(Elf32_Sword)(p&0xFFFFFFFF);
	}
	return 4;
}

static int
readuchr(unsigned char *out, unsigned char const *buf)
{
	if (!out || !buf) return -1;
	*out = buf[0];
	return 1;
}

static int
readword(Elf32_Word *out, unsigned char const *buf)
{
	if (!out || !buf) return -1;
	*out = buf[0] + (buf[1]<<8) + (buf[2]<<16) + (buf[3]<<24);
	return 4;
}

static int
writeaddr(Elf32_Addr *in, unsigned char *buf)
{
	return writeword(in, buf);
}

static int
writehalf(Elf32_Half *in, unsigned char *buf)
{
	if (!in || !buf) return -1;
	*(buf++) =  *in    &0xFF;
	*(buf++) = (*in>>8)&0xFF;
	return 2;
}

static int
writeuchr(unsigned char *in, unsigned char *buf)
{
	if (!in || !buf) return -1;
	*buf = *in;
	return 1;
}

static int
writeword(Elf32_Word *in, unsigned char *buf)
{
	if (!in || !buf) return -1;
	*(buf++) =  *in     &0xFF;
	*(buf++) = (*in>> 8)&0xFF;
	*(buf++) = (*in>>16)&0xFF;
	*(buf++) = (*in>>24)&0xFF;
	return 4;
}
