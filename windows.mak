## RULES ###############################################################
.SUFFIXES : .c .exe .obj .y
all : aster.exe starlink.exe teddy.exe

.c.obj :
	CL /nologo /O2 /std:c11 /c $<
.obj.exe :
	LINK /nologo /OUT:$@ $**
.y.c :
	.\LL1.exe $< >$@

clean :
	DEL /Q *.obj LL1.exe parse.c

distclean : clean
	DEL /Q aster.exe starlink.exe teddy.exe

## EXECUTABLE TARGETS ##################################################
aster.exe    : aster.obj apn_print.obj dynarr.obj instr.obj lex.obj
aster.exe    :     node.obj ntsl.obj object.obj parse.obj reloc.obj
starlink.exe : starlink.obj dynarr.obj object.obj reloc.obj
teddy.exe    : teddy.obj dynarr.obj

## OBJECTS AND PARTS ###################################################
parse.c       : parse.y LL1.exe
apn_print.obj : apn_print.c common.h dynarr.h node.h
aster.obj     : aster.c common.h dynarr.h elf32.h instr.h lex.h
aster.obj     :     ntsl.h node.h object.h reloc.h version.h
dynarr.obj    : dynarr.c dynarr.h
instr.obj     : instr.c common.h dynarr.h instr.h node.h reloc.h
LL1.obj       : LL1.c
lex.obj       : lex.c lex.h common.h
node.obj      : node.c common.h dynarr.h node.h
ntsl.obj      : ntsl.c ntsl.h
object.obj    : object.c dynarr.h elf32.h object.h
parse.obj     : parse.c common.h dynarr.h lex.h node.h
starlink.obj  : starlink.c dynarr.h object.h elf32.h reloc.h version.h
teddy.obj     : teddy.c dynarr.h version.h
