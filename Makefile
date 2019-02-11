
MYCFLAGS=-fsanitize=address -g -Wall --pedantic
GTKCFLAGS:=$(subst -I,-isystem ,$(shell pkg-config --cflags gtk+-2.0))
GTKLDFLAGS:=$(shell pkg-config --libs gtk+-2.0) $(shell pkg-config --libs gthread-2.0)

all:	maze

xorshift.o:	xorshift.c xorshift.h Makefile
	$(CC) ${MYCFLAGS} ${GTKCFLAGS} -c xorshift.c

bline.o:	bline.c bline.h Makefile
	$(CC) ${MYCFLAGS} ${GTKCFLAGS} -c bline.c

linuxcompat.o:	linuxcompat.c linuxcompat.h bline.h
	$(CC) ${MYCFLAGS} ${GTKCFLAGS} -c linuxcompat.c

maze.o:	maze.c maze.h build_bug_on.h chest_points.h cobra_points.h dragon_points.h grenade_points.h \
	key_points.h orc_points.h phantasm_points.h potion_points.h scroll_points.h \
	shield_points.h sword_points.h down_ladder_points.h up_ladder_points.h Makefile
	$(CC) ${MYCFLAGS} ${GTKCFLAGS} -c maze.c

maze:	maze.o bline.o linuxcompat.o xorshift.o Makefile
	$(CC) ${MYCFLAGS} ${GTKCFLAGS} -o maze  maze.o bline.o linuxcompat.o xorshift.o ${GTKLDFLAGS}

clean:
	rm -f maze *.o
