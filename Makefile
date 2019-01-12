
all:	maze

bline.o:	bline.c bline.h Makefile
	gcc -fsanitize=address -g -Wall --pedantic -c bline.c

maze:	maze.c maze.h chest_points.h cobra_points.h dragon_points.h grenade_points.h \
	key_points.h orc_points.h phantasm_points.h potion_points.h scroll_points.h \
	shield_points.h sword_points.h down_ladder_points.h up_ladder_points.h Makefile bline.o
	gcc -fsanitize=address -g -Wall --pedantic -o maze maze.c bline.o

clean:
	rm -f maze *.o
