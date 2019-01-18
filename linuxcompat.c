#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <termios.h>

#include "bline.h"
#include "linuxcompat.h"

void FbInit(void)
{
}

static unsigned char screen[SCREEN_XDIM][SCREEN_YDIM];

void plot_point(int x, int y, void *context)
{
    unsigned char *screen = context;

    screen[SCREEN_YDIM * x + y] = '#';
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

    for (x = x1; x < x2; x++)
        plot_point(x, y1, screen);
}

void FbVerticalLine(unsigned char x1, unsigned char y1, unsigned char x2, unsigned char y2)
{
    unsigned char y;

    for (y = y1; y < y2; y++)
        plot_point(x1, y, screen);
}

void FbClear(void)
{
    memset(screen, ' ', SCREEN_XDIM * SCREEN_YDIM);
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

