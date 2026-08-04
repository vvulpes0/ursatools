#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "dynarr.h"
#include "object.h"
#include "reloc.h"
#include "version.h"

#define DATA_BASE 0x0000
#define ALLOC_TEXT_SIZE (64*1024)
#define ALLOC_DATA_SIZE (64*1024)

struct defloc {
	int obji;
	int symi;
};
enum starlinkflags {
	SL_PRINTHELP = 1,
	SL_PRINTVERSION = 2,
	SL_EMITTEXT = 4,
	SL_EMITDATA = 8,
	SL_EMITLOGISIM = 16,
	SL_EMITMAP = 32,
	SL_DIE = 2048,
};

static struct dynarr loadobjs(int argc, char * const *argv);
static _Bool checkglobals(struct dynarr d, char * const *argv);
static void layout(struct dynarr objs, struct dynarr *t, struct dynarr *d);
static _Bool relocate(struct dynarr objs, struct dynarr *t, struct dynarr *d);
static _Bool emitlogisim(
	char const *prefix, struct dynarr text, struct dynarr data, int flags);
static void emitmap(char const *prefix, char **argv, struct dynarr d);

static struct defloc findglobaldef(struct dynarr, char const *, int);
static void cleanup_objs(struct dynarr *d);
static _Bool applyrels1(struct dynarr objs, struct objfile obj,
                        struct section *sec,
                        struct dynarr *t, struct dynarr *d);
static void emitlogisimblock(struct dynarr s, FILE *file, int size);
static void emitmapsym(FILE *out, Elf32_Sym sym, struct objfile obj);
static Elf32_Sym evaluate(
	struct dynarr d,
	struct objfile obj,
	struct section *symtab,
	struct section *strtab,
	int i);

static void
printhelp(FILE *file) {
	fprintf(file,
	"usage: starlink [-DdhMmTtv?] [-o outprefix] file...\n");
	fprintf(file, "\t-D            do not emit .data segment\n");
	fprintf(file, "\t-d            emit .data segment (the default)\n");
	fprintf(file, "\t-h, -?        print this help and exit\n");
	fprintf(file, "\t-o outprefix  use outprefix as base name\n");
	fprintf(file, "\t-M            do not emit symbol map (the default)\n");
	fprintf(file, "\t-m            emit symbol map\n");
	fprintf(file, "\t-T            do not emit .text segment\n");
	fprintf(file, "\t-t            emit .text segment (the default)\n");
	fprintf(file, "\t-v            print version and exit\n");
}
int
main(int argc, char *argv[])
{
	int retval = 0;
	argv++; argc--;

	FILE *helpfile = stdout;
	int flags = SL_EMITTEXT | SL_EMITDATA;
	_Bool process = 1;
	char const *outprefix = "out";
	while (process && argc && *argv && **argv == '-') {
		char const *str = *(argv++); argc--;
		if (str[1] == '\0') {
			fprintf(stderr, "starlink: error: ");
			fprintf(stderr, "cannot use standard input\n");
			return 1;
		}
		int i = 1;
		while (str && str[i] != '\0') {
			switch (str[i++]) {
			case '-':
				if (i == 2 && str[i] == '\0') {
					process = 0;
				} else {
					flags = SL_PRINTHELP;
					helpfile = stderr;
					process = 0;
					retval = 1;
					i = strlen(str);
				}
				break;
			case 'D':
				flags &= ~SL_EMITDATA;
				break;
			case 'M':
				flags &= ~SL_EMITMAP;
				break;
			case 'T':
				flags &= ~SL_EMITTEXT;
				break;
			case 'd':
				flags |= SL_EMITDATA;
				break;
			case 'h':
			case '?':
				flags |= SL_PRINTHELP;
				break;
			case 'm':
				flags |= SL_EMITMAP;
				break;
			case 'o':
				if (str[i] == '\0') {
					i = 0;
					str = *(argv++); argc--;
				}
				if (!str) {
					fprintf(stderr, "starlink: error: ");
					fprintf(stderr, "missing parameter ");
					fprintf(stderr, "to -o flag\n");
					flags |= SL_DIE;
					process = 0;
					break;
				}
				outprefix = str + i;
				i += strlen(outprefix);
				break;
			case 't':
				flags |= SL_EMITTEXT;
				break;
			case 'v':
				flags |= SL_PRINTVERSION;
				break;
			default:
				retval = 1;
				flags |= SL_PRINTHELP;
				helpfile = stderr;
				break;
			}
		}
	}

	if (flags&SL_DIE) return 1;

	if (flags & SL_PRINTVERSION) {
		printf("starlink (URSA) " URSA_VERSION "\n");
		if ((flags&SL_PRINTHELP) == 0) return retval;
	}
	if (flags & SL_PRINTHELP) {
		printhelp(helpfile);
		return retval;
	}

	if (!argc) {
		fprintf(stderr, "starlink: error: no input files\n");
		return 1;
	}

	struct dynarr objs = loadobjs(argc, argv);
	if (!objs.content) return 1;
	if (!checkglobals(objs, argv)) {
		cleanup_objs(&objs);
		return 1;
	}
	struct dynarr text = da_new(1);
	struct dynarr data = da_new(1);
	for (int i = 0; i < DATA_BASE; i++) da_append(&data, "");
	layout(objs, &text, &data);
	if (!relocate(objs, &text, &data)) {
		da_free(text);
		da_free(data);
		cleanup_objs(&objs);
		return 1;
	}
	if (text.length > ALLOC_TEXT_SIZE) {
		fprintf(stderr,
			"error: .text segment of size %d "
			"too large for capacity (%d)",
			text.length, ALLOC_TEXT_SIZE);
		if (data.length <= ALLOC_DATA_SIZE) {
			da_free(text);
			da_free(data);
			cleanup_objs(&objs);
			return 1;
		}
	}
	if (data.length > ALLOC_DATA_SIZE) {
		fprintf(stderr,
			"error: .data segment of size %d "
			"too large for capacity (%d)",
			data.length, ALLOC_DATA_SIZE);
		da_free(text);
		da_free(data);
		cleanup_objs(&objs);
		return 1;
	}

	retval = emitlogisim(outprefix, text, data, flags);
	if (flags & SL_EMITMAP) emitmap(outprefix, argv, objs);

	da_free(text);
	da_free(data);
	cleanup_objs(&objs);
	return retval;
}

/* PHASES *************************************************************/

static struct dynarr
loadobjs(int argc, char * const *argv)
{
	struct dynarr d = da_new(sizeof(struct objfile));
	_Bool go = 1;
	for (int i = 0; i < argc; i++) {
		struct objfile obj = {0};
		FILE *file = fopen(argv[i], "rb");
		if (!file) {
			perror(argv[i]);
			go = 0;
		}
		if (!obj_load(&obj, file)) {
			fprintf(stderr, "%s: error: failed to load ELF\n",
			        argv[i]);
			go = 0;
		} else if (obj.h.e_machine != 0xCEC6) {
			fprintf(stderr, "%s: error: wrong machine\n",
			        argv[i]);
			go = 0;
		} else if (obj.h.e_type != ET_REL) {
			fprintf(stderr, "%s: error: not a relocatable ELF\n",
			        argv[i]);
			go = 0;
		}
		fclose(file);
		da_append(&d, &obj);
	}
	if (!go) cleanup_objs(&d);
	return d;
}

static _Bool
checkglobals(struct dynarr d, char * const *argv)
{
	for (int i = 0; i < d.length; i++) {
		struct objfile *obj = (struct objfile *)da_get(d, i);
		if (!obj) return 0;
		struct section *symtab = obj_secn(*obj, ".symtab");
		if (!symtab || !symtab->content) continue;
		struct section *strtab = obj_sec(*obj, symtab->h.sh_link);
		if (!strtab || !strtab->content) {
			fprintf(stderr,
			        "error: symtab without valid strtab\n");
			return 0;
		}
		int n = symtab->h.sh_size / symtab->h.sh_entsize;
		if (!symtab->h.sh_info) continue;
		for (int j = symtab->h.sh_info; j < n; j++) {
			Elf32_Sym sym = obj_getsym(symtab, j);
			char const *name = (char const *)(strtab->content)
			                 + sym.st_name;
			struct defloc dl = findglobaldef(d, name, i);
			if (sym.st_shndx == SHN_UNDEF && dl.obji == -1) {
				fprintf(stderr,
				        "%s: error: undefined symbol: %s\n",
				        argv[i], name);
				return 0;
			}
			if (sym.st_shndx != SHN_UNDEF && dl.obji != -1) {
				fprintf(stderr,
				        "%s: error: multiply defined symbol: "
				        "%s\n",
				        argv[dl.obji], name);
				fprintf(stderr,
				        "previously defined in %s\n",
				        argv[i]);
				return 0;
			}
		}
	}
	return 1;
}

static void
layout(struct dynarr objs, struct dynarr *text, struct dynarr *data)
{
	if (!text || !data) return;
	if (!objs.content) return;
	/* handle text sections */
	for (int i = 0; i < objs.length; i++) {
		struct objfile obj = *(struct objfile *)da_get(objs, i);
		struct section *t = obj_secn(obj, ".text");
		if (!t) continue;
		unsigned char c = '\0';
		while (t->h.sh_addralign && text->length%t->h.sh_addralign) {
			da_append(text, &c);
		}
		t->h.sh_addr = text->length;
		for (int j = 0; j < t->h.sh_size; j++) {
			da_append(text, t->content + j);
		}
	}
	/* handle data sections */
	for (int i = 0; i < objs.length; i++) {
		struct objfile obj = *(struct objfile *)da_get(objs, i);
		struct section *d = obj_secn(obj, ".data");
		if (!d) continue;
		unsigned char c = '\0';
		while (d->h.sh_addralign && data->length%d->h.sh_addralign) {
			da_append(data, &c);
		}
		d->h.sh_addr = data->length;
		for (int j = 0; j < d->h.sh_size; j++) {
			da_append(data, d->content + j);
		}
	}
	/* handle bss sections */
	for (int i = 0; i < objs.length; i++) {
		struct objfile obj = *(struct objfile *)da_get(objs, i);
		struct section *b = obj_secn(obj, ".bss");
		if (!b) continue;
		unsigned char c = '\0';
		while (b->h.sh_addralign && data->length%b->h.sh_addralign) {
			da_append(data, &c);
		}
		b->h.sh_addr = data->length;
		for (int j = 0; j < b->h.sh_size; j++) {
			da_append(data, &c);
		}
	}
	/* handle symbols */
	for (int i = 0; i < objs.length; i++) {
		struct objfile obj = *(struct objfile *)da_get(objs, i);
		struct section *t = obj_secn(obj, ".text");
		struct section *d = obj_secn(obj, ".data");
		struct section *b = obj_secn(obj, ".bss");
		struct section *symtab = obj_secn(obj, ".symtab");
		if (!symtab || !symtab->content) continue;
		if (!symtab->h.sh_entsize) continue;
		int n = symtab->h.sh_size / symtab->h.sh_entsize;
		for (int symi = 0; symi < n; symi++) {
			Elf32_Sym sym = obj_getsym(symtab, symi);
			struct section *x = obj_sec(obj, sym.st_shndx);
			if (t && x == t) {
				sym.st_value += t->h.sh_addr;
			} else if (d && x == d) {
				sym.st_value += d->h.sh_addr;
			} else if (b && x == b) {
				sym.st_value += b->h.sh_addr;
			} else {
				continue;
			}
			obj_setsym(symtab, symi, sym);
		}
	}
}

static _Bool
relocate(struct dynarr objs, struct dynarr *t, struct dynarr *d)
{
	if (!t || !d) return 0;
	for (int i = 0; i < objs.length; i++) {
		struct objfile obj = *(struct objfile *)da_get(objs, i);
		struct section *ts = obj_secn(obj, ".text");
		struct section *ds = obj_secn(obj, ".data");
		for (int shnum = 1; shnum < obj.h.e_shnum; shnum++) {
			struct section *sec = obj_sec(obj, shnum);
			if (!sec) continue;
			if (   sec->h.sh_type != SHT_REL
			    && sec->h.sh_type != SHT_RELA) {
				continue;
			}
			if (!sec->h.sh_entsize) continue;
			struct section *target = obj_sec(obj, sec->h.sh_info);
			if (target != ts && target != ds) {
				fprintf(stderr,
				        "warning: ignoring relocations "
				        "against discarded section\n");
				/* targeting bss is nonsense */
				continue;
			}
			if (!applyrels1(objs, obj, sec, t, d)) return 0;
		}
	}
	return 1;
}

static _Bool
emitlogisim(char const *prefix, struct dynarr t, struct dynarr d, int flags)
{
	int n = strlen(prefix);
	char *buf = malloc(n + 7);
	if (!buf) return 0;
	if (flags & SL_EMITTEXT) {
		strcpy(buf, prefix);
		strcat(buf, ".lcode");
		FILE *cfile = fopen(buf, "w");
		if (!cfile) {
			perror(buf);
			free(buf);
			return 0;
		}
		emitlogisimblock(t, cfile, ALLOC_TEXT_SIZE);
		fclose(cfile);
	}
	if (flags & SL_EMITDATA) {
		strcpy(buf, prefix);
		strcat(buf, ".ldata");
		FILE *dfile = fopen(buf, "w");
		if (!dfile) {
			perror(buf);
			free(buf);
			return 0;
		}
		emitlogisimblock(d, dfile, ALLOC_DATA_SIZE);
		fclose(dfile);
	}
	free(buf);
	return 1;
}

static void
emitmap(char const *prefix, char **argv, struct dynarr d)
{
	int n = strlen(prefix);
	char *buf = malloc(n + 5);
	if (!buf) return;
	strncpy(buf, prefix, n + 1);
	strcat(buf, ".map");
	FILE *out = fopen(buf, "w");
	for (int i = 0; i < d.length; i++) {
		struct objfile obj = *(struct objfile *)da_get(d, i);
		struct section *symtab = obj_secn(obj, ".symtab");
		struct section *strtab = obj_secn(obj, ".strtab");
		if (!symtab || !symtab->content) continue;
		if (!strtab || !strtab->content) continue;
		if (!symtab->h.sh_entsize) continue;
		int n = symtab->h.sh_size / symtab->h.sh_entsize;
		fprintf(out, " @ %s\n", argv[i]);
		int li = 0;
		int gi = symtab->h.sh_info;
		while (li < symtab->h.sh_info && gi < n) {
			Elf32_Sym lsym = obj_getsym(symtab, li);
			Elf32_Sym gsym = obj_getsym(symtab, gi);
			if (lsym.st_value < gsym.st_value) {
				emitmapsym(out, lsym, obj);
				li++;
			} else {
				emitmapsym(out, gsym, obj);
				gi++;
			}
		}
		for (; li < symtab->h.sh_info; li++) {
			emitmapsym(out, obj_getsym(symtab, li), obj);
		}
		for (; gi < n; gi++) {
			emitmapsym(out, obj_getsym(symtab, gi), obj);
		}
	}
	fclose(out);
}

/* HELPERS ************************************************************/

static void
cleanup_objs(struct dynarr *d)
{
	for (int i = 0; i < d->length; i++) {
		struct objfile *obj = (struct objfile *)da_get(*d, i);
		if (!obj) continue;
		obj_free(*obj);
		obj->sections = NULL;
	}
	da_free(*d);
	d->length = 0;
	d->content = NULL;
}

static _Bool
applyrels1(struct dynarr objs, struct objfile obj, struct section *sec,
           struct dynarr *t, struct dynarr *d)
{
	struct section *target = obj_sec(obj, sec->h.sh_info);
	struct section *ts = obj_secn(obj, ".text");
	/* struct section *ds = obj_secn(obj, ".data"); */
	struct section *symtab = obj_sec(obj, sec->h.sh_link);
	struct section *strtab
		= obj_sec(obj, symtab->h.sh_link);
	if (!symtab || !strtab) return 0;
	int n = sec->h.sh_size / sec->h.sh_entsize;
	for (int ri = 0; ri < n; ri++) {
		Elf32_Rela rel = obj_getrela(sec, ri);
		enum reloc rt = Elf32_R_TYPE(rel.r_info);
		int symi = Elf32_R_SYM(rel.r_info);
		Elf32_Sym sym = evaluate(objs, obj, symtab, strtab, symi);
		int targi = (target == ts)? 1 : 2;
		unsigned char *buf = (target == ts)? t->content : d->content;
		if (rt == R_URSA_PC8 && targi != sym.st_shndx) {
			fprintf(stderr, "error: bad pcrel section\n");
			return 0;
		}
		if (!rel_apply(rt, buf + rel.r_offset, rel.r_offset,
		               sym.st_value, rel.r_addend)) {
			fprintf(stderr, "error: relocation failed\n");
			fprintf(stderr, "\t    rt:%d\n", rt);
			fprintf(stderr, "\toffset:%d\n", rel.r_offset);
			fprintf(stderr, "\t value:%d\n", sym.st_value);
			fprintf(stderr, "\taddend:%d\n", rel.r_addend);
			return 0;
		}
	}
	return 1;
}

static Elf32_Sym
evaluate(struct dynarr d,
         struct objfile obj,
         struct section *symtab,
         struct section *strtab,
         int i)
{
	Elf32_Sym sym = obj_getsym(symtab, i);
	enum Elf_SYMBIND bind = Elf32_ST_BIND(sym.st_info);
	/* enum Elf_SYMTYPE type = Elf32_ST_TYPE(sym.st_info); */
	while (sym.st_shndx == SHN_UNDEF && bind == STB_GLOBAL) {
		struct defloc dl =
			findglobaldef(d, strtab->content + sym.st_name, -1);
		obj = *(struct objfile *)da_get(d, dl.obji);
		symtab = obj_secn(obj, ".symtab");
		strtab = obj_sec(obj, symtab->h.sh_link);
		sym = obj_getsym(symtab, dl.symi);
		bind = Elf32_ST_BIND(sym.st_info);
		/* type = Elf32_ST_TYPE(sym.st_info); */
	}
	/* pseudosections for consistency */
	if (obj_sec(obj, sym.st_shndx) == obj_secn(obj, ".text")) {
		sym.st_shndx = 1;
	}
	if (obj_sec(obj, sym.st_shndx) == obj_secn(obj, ".data")) {
		sym.st_shndx = 2;
	}
	return sym;
}

static struct defloc
findglobaldef(struct dynarr d, char const *name, int skip)
{
	struct defloc out = {-1, 0};
	for (int i = 0; i < d.length; i++) {
		if (i == skip) continue;
		struct objfile *obj = (struct objfile *)da_get(d, i);
		if (!obj) continue;
		struct section *symtab = obj_secn(*obj, ".symtab");
		if (!symtab || !symtab->content) continue;
		struct section *strtab = obj_sec(*obj, symtab->h.sh_link);
		if (!strtab || !strtab->content) continue;
		int n = symtab->h.sh_size / symtab->h.sh_entsize;
		for (int j = symtab->h.sh_info; j < n; j++) {
			Elf32_Sym sym = obj_getsym(symtab, j);
			if (sym.st_shndx == SHN_UNDEF) continue;
			char const *sname = (char const *)(strtab->content)
			                  + sym.st_name;
			if (!strcmp(name, sname)) {
				out.obji = i;
				out.symi = j;
				return out;
			}
		}
	}
	return out;
}

static void
emitlogisimblock(struct dynarr s, FILE *file, int size)
{
	fprintf(file, "v3.0 hex bytes addressed little-endian");
	int nhex = 0;
	int sizecopy = size;
	while (sizecopy) {
		nhex++;
		sizecopy /= 16;
	}
	if (!nhex) nhex = 1;
	int i = 0;
	for (; i < size && i < s.length; i++) {
		if (i%16 == 0) {
			fprintf(file, "\n%04x: ", i);
		}
		fprintf(file, "%02x", ((unsigned char *)s.content)[i]);
	}
	for (; i < size; i++) {
		if (i%16 == 0) {
			fprintf(file, "\n%04x: ", i);
		}
		fputs("00", file);
	}
	fputs("\n", file);
}

static void
emitmapsym(FILE *out, Elf32_Sym sym, struct objfile obj)
{
	struct section *textsec = obj_secn(obj, ".text");
	struct section *datasec = obj_secn(obj, ".data");
	struct section *symtab = obj_secn(obj, ".symtab");
	if (!symtab || !symtab->content) return;
	struct section *strtab = obj_sec(obj, symtab->h.sh_link);
	if (!strtab || !strtab->content) return;
	if (!sym.st_name) return;
	char sec = 0;
	if (sym.st_shndx == SHN_UNDEF) return;
	if (sym.st_shndx == SHN_ABS) {
		sec = 'a';
	} else {
		struct section *symsec = obj_sec(obj, sym.st_shndx);
		if (textsec && symsec == textsec) {
			sec = 't';
		} else if (datasec && symsec == datasec) {
			sec = 'd';
		} else {
			return;
		}
	}
	if (!sec) return;
	if (Elf32_ST_BIND(sym.st_info) == STB_GLOBAL) {
		sec = toupper(sec);
	}
	char kind = '?';
	if (Elf32_ST_TYPE(sym.st_info) == STT_FUNC) {
		kind = 'f';
	} else if (Elf32_ST_TYPE(sym.st_info) == STT_OBJECT) {
		kind = 'o';
	}
	fprintf(out, "%c %c %08x %08x %s\n",
		sec, kind,
		sym.st_value, sym.st_size,
		((char *)strtab->content) + sym.st_name);
}
