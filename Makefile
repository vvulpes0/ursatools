## TOOLS AND FLAGS #####################################################
CFLAGS=-O2 -std=c99 -Wall

## DIRECTORIES #########################################################
DESTDIR?=
PREFIX?=/usr/local
BINDIR?=$(PREFIX)/bin
MANDIR?=$(PREFIX)/share/man
MAN1DIR=$(MANDIR)/man1
MAN5DIR=$(MANDIR)/man5

## RULES ###############################################################
.SUFFIXES :
.SUFFIXES : .c .o .y
% : %.c
.y.c :
	$(dir $(filter LL1,$^))$(notdir $(filter LL1,$^)) $< >$@
.c.o :
	$(CC) $(CFLAGS) -c $< -o $@
.o :
	$(CC) $(LDFLAGS) $^ $(LDLIBS) -o $@

.PHONY : all
all : aster starlink teddy

.PHONY : clean
clean :
	rm -f -- *.o LL1 parse.c

.PHONY : distclean
distclean : clean
	rm -f -- aster starlink teddy

.PHONY : install install-bin install-man1 install-man5
install : install-bin install-man1 install-man5
install-bin : aster starlink teddy
	[ -d $(DESTDIR)$(BINDIR) ] || install -dm755 $(DESTDIR)$(BINDIR); \
	install -m755 $^ $(DESTDIR)$(BINDIR)
install-man1 : aster.1 starlink.1 teddy.1
	[ -d $(DESTDIR)$(MAN1DIR) ] || install -dm755 $(DESTDIR)$(MAN1DIR); \
	install -m644 $^ $(DESTDIR)$(MAN1DIR)
install-man5 : aster.5
	[ -d $(DESTDIR)$(MAN5DIR) ] || install -dm755 $(DESTDIR)$(MAN5DIR); \
	install -m644 $^ $(DESTDIR)$(MAN5DIR)

.PHONY : uninstall
uninstall:
	rm -f $(DESTDIR)$(BINDIR)/aster
	rm -f $(DESTDIR)$(BINDIR)/starlink
	rm -f $(DESTDIR)$(BINDIR)/teddy
	rm -f $(DESTDIR)$(MAN1DIR)/aster.1
	rm -f $(DESTDIR)$(MAN1DIR)/starlink.1
	rm -f $(DESTDIR)$(MAN1DIR)/teddy.1
	rm -f $(DESTDIR)$(MAN5DIR)/aster.5

## EXECUTABLE TARGETS ##################################################
aster    : aster.o apn_print.o dynarr.o instr.o lex.o
aster    :     node.o ntsl.o object.o parse.o reloc.o
starlink : starlink.o dynarr.o object.o reloc.o
teddy    : teddy.o dynarr.o

## OBJECTS AND PARTS ###################################################
parse.c     : parse.y LL1
apn_print.o : apn_print.c common.h dynarr.h node.h
aster.o     : aster.c common.h dynarr.h elf32.h instr.h lex.h
aster.o     :     ntsl.h node.h object.h reloc.h version.h
dynarr.o    : dynarr.c dynarr.h
instr.o     : instr.c common.h dynarr.h instr.h node.h reloc.h
LL1.o       : LL1.c
lex.o       : lex.c lex.h common.h
node.o      : node.c common.h dynarr.h node.h
ntsl.o      : ntsl.c ntsl.h
object.o    : object.c dynarr.h elf32.h object.h
parse.o     : parse.c common.h dynarr.h lex.h node.h
starlink.o  : starlink.c dynarr.h object.h elf32.h reloc.h version.h
teddy.o     : teddy.c dynarr.h version.h
