#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <termios.h>

#include "bline.h"
#include "linuxcompat.h"


#define font_2_width 8
#define font_2_height 336
static const char font_2_bits[] = {
   0x04, 0x0a, 0x0a, 0x0a, 0x1f, 0x11, 0x11, 0x11, 0x07, 0x09, 0x09, 0x09,
   0x0f, 0x11, 0x11, 0x0f, 0x0e, 0x11, 0x01, 0x01, 0x01, 0x01, 0x11, 0x0e,
   0x07, 0x09, 0x11, 0x11, 0x11, 0x11, 0x09, 0x07, 0x1f, 0x01, 0x01, 0x01,
   0x0f, 0x01, 0x01, 0x1f, 0x1f, 0x01, 0x01, 0x01, 0x0f, 0x01, 0x01, 0x01,
   0x0e, 0x11, 0x01, 0x01, 0x19, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x11, 0x11,
   0x1f, 0x11, 0x11, 0x11, 0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f,
   0x1f, 0x08, 0x08, 0x08, 0x08, 0x08, 0x09, 0x06, 0x11, 0x09, 0x05, 0x03,
   0x07, 0x09, 0x11, 0x11, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x01, 0x1f,
   0x11, 0x1b, 0x15, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x13, 0x15,
   0x15, 0x19, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e,
   0x0f, 0x11, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x01, 0x0e, 0x11, 0x11, 0x11,
   0x11, 0x11, 0x09, 0x16, 0x0f, 0x11, 0x11, 0x11, 0x0f, 0x09, 0x11, 0x11,
   0x0e, 0x11, 0x01, 0x01, 0x0e, 0x10, 0x11, 0x0e, 0x1f, 0x04, 0x04, 0x04,
   0x04, 0x04, 0x04, 0x04, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e,
   0x11, 0x11, 0x11, 0x0a, 0x0a, 0x0a, 0x04, 0x04, 0x11, 0x11, 0x11, 0x11,
   0x15, 0x15, 0x15, 0x0a, 0x11, 0x11, 0x0a, 0x04, 0x04, 0x0a, 0x11, 0x11,
   0x11, 0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04, 0x1f, 0x10, 0x10, 0x08,
   0x04, 0x02, 0x01, 0x1f, 0x0e, 0x11, 0x15, 0x15, 0x15, 0x15, 0x11, 0x0e,
   0x06, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f, 0x0e, 0x11, 0x10, 0x08,
   0x04, 0x02, 0x01, 0x1f, 0x1f, 0x10, 0x08, 0x04, 0x08, 0x10, 0x11, 0x0e,
   0x10, 0x11, 0x11, 0x11, 0x1f, 0x10, 0x10, 0x10, 0x1f, 0x11, 0x01, 0x01,
   0x0e, 0x10, 0x11, 0x0e, 0x0e, 0x11, 0x01, 0x01, 0x0f, 0x11, 0x11, 0x0e,
   0x1f, 0x11, 0x10, 0x08, 0x04, 0x04, 0x04, 0x04, 0x0e, 0x11, 0x11, 0x0e,
   0x11, 0x11, 0x11, 0x0e, 0x0e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10, 0x10,
   0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x04, 0x00,
   0x00, 0x04, 0x00, 0x00, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04, 0x00, 0x04,
   0x00, 0x00, 0x00, 0x00, 0x0e, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
   0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

void FbInit(void)
{
}

static unsigned char screen[SCREEN_XDIM][SCREEN_YDIM];

void plot_point(int x, int y, void *context)
{
    unsigned char *screen = context;

    screen[SCREEN_YDIM * x + y] = '#';
}

void clear_point(int x, int y, void *context)
{
    unsigned char *screen = context;

    screen[SCREEN_YDIM * x + y] = ' ';
}

void FbSwapBuffers(void)
{
    int x, y;

    for (y = 0; y < SCREEN_YDIM; y++) {
        for (x = 0; x < SCREEN_XDIM; x++)
            printf("%c", screen[x][y]);
        printf("\n");
    }
}

void FbLine(unsigned char x1, unsigned char y1, unsigned char x2, unsigned char y2)
{
    bline(x1, y1, x2, y2, plot_point, screen);
}

void FbHorizontalLine(unsigned char x1, unsigned char y1, unsigned char x2, unsigned char y2)
{
    unsigned char x;

    for (x = x1; x <= x2; x++)
        plot_point(x, y1, screen);
}

void FbVerticalLine(unsigned char x1, unsigned char y1, unsigned char x2, unsigned char y2)
{
    unsigned char y;

    for (y = y1; y <= y2; y++)
        plot_point(x1, y, screen);
}

void FbClear(void)
{
    memset(screen, ' ', SCREEN_XDIM * SCREEN_YDIM);
}

unsigned char char_to_index(unsigned char charin){
    /*
        massaged from Jon's LCD code.
    */
    if (charin >= 'a' && charin <= 'z')
        charin -= 97;
    else {
        if (charin >= 'A' && charin <= 'Z')
                charin -= 65;
        else {
            if (charin >= '0' && charin <= '9')
                charin -= 22;
            else {
                switch (charin) {
                case '.':
                    charin = 36;
                    break;

                case ':':
                    charin = 37;
                    break;

                case '!':
                    charin = 38;
                    break;

                case '-':
                    charin = 39;
                    break;

                case '_':
                    charin = 40;
                    break;

                default:
                    charin = 41;
                }
            }
        }
    }
    return charin;
}

static void draw_character(unsigned char x, unsigned char y, unsigned char c)
{
	unsigned char index = char_to_index(c);
	unsigned char i, j, bits;
	unsigned char sx, sy;

	sy = y;
	for (i = 0; i < 8; i++) {
		bits = font_2_bits[8 * index + i];
		sx = x;
		for (j = 0; j < 8; j++) {
			if ((bits >> j) & 0x01) {
				plot_point(sx, sy, screen);
			} else {
				clear_point(sx, sy, screen);
			}
			sx++;
		}
		sy++;
	}
}

static unsigned char write_x = 0;
static unsigned char write_y = 0;

void FbMove(unsigned char x, unsigned char y)
{
	write_x = x;
	write_y = y;
}

void FbWriteLine(char *s)
{
	int i;

	for (i = 0; s[i]; i++) {
		draw_character(write_x, write_y, s[i]);
		write_x += 8;
		if (write_x > SCREEN_XDIM - 8) {
			write_x = 0;
			write_y += 8;
			if (write_y > SCREEN_YDIM - 8)
				write_y = 0;
		}
	}
}

static struct termios original_input_mode;

void restore_original_input_mode(void)
{
    tcsetattr(0, TCSANOW, &original_input_mode);
}

void set_nonblocking_input_mode(void)
{
    struct termios nonblocking_input_mode;

    tcgetattr(0, &original_input_mode);
    memcpy(&nonblocking_input_mode, &original_input_mode, sizeof(nonblocking_input_mode));
    cfmakeraw(&nonblocking_input_mode);
    tcsetattr(0, TCSANOW, &nonblocking_input_mode);
    atexit(restore_original_input_mode);
}

int wait_for_keypress(void)
{
    struct timeval tv = { 0L, 0L };
    fd_set fds;

    set_nonblocking_input_mode();
    FD_ZERO(&fds);
    FD_SET(0, &fds);

    return select(1, &fds, NULL, NULL, &tv);
}

int get_keypress(void)
{
    int rc;
    unsigned char kp;

    rc = read(0, &kp, sizeof(kp));
    restore_original_input_mode();
    return rc < 0 ? rc : kp;
}

void itoa(char *string, int value, __attribute__((unused)) int base)
{
	sprintf(string, "%d", value);
}

int abs(int x)
{
    return x > 0 ? x : -x;	
}
