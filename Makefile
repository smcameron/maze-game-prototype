
all:	maze

bline.o:	bline.c bline.h Makefile
	gcc -fsanitize=address -g -Wall --pedantic -c bline.c

linuxcompat.o:	linuxcompat.c linuxcompat.h bline.h
	gcc -fsanitize=address -g -Wall --pedantic -c linuxcompat.c

maze.o:	maze.c maze.h chest_points.h cobra_points.h dragon_points.h grenade_points.h \
	key_points.h orc_points.h phantasm_points.h potion_points.h scroll_points.h \
	shield_points.h sword_points.h down_ladder_points.h up_ladder_points.h Makefile
	gcc -fsanitize=address -g -Wall --pedantic -c maze.c

maze:	maze.o bline.o linuxcompat.o Makefile
	gcc -fsanitize=address -g -Wall --pedantic -o maze maze.o bline.o linuxcompat.o

clean:
	rm -f maze *.o
