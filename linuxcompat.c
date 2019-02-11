#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/select.h>
#include <termios.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <errno.h>
#include <gtk/gtk.h>
#include <gdk/gdkkeysyms.h>

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

static GtkWidget *vbox, *window, *drawing_area;
#define SCALE_FACTOR 6
#define GTK_SCREEN_WIDTH (SCREEN_XDIM * SCALE_FACTOR)
#define GTK_SCREEN_HEIGHT (SCREEN_YDIM * SCALE_FACTOR)
static int real_screen_width = GTK_SCREEN_WIDTH;
static int real_screen_height = GTK_SCREEN_HEIGHT;
static GdkGC *gc = NULL;               /* our graphics context. */
static int screen_offset_x = 0;
static int screen_offset_y = 0;
static gint timer_tag;
#define NCOLORS 8 
GdkColor huex[NCOLORS];
static int (*badge_function)(void);
static int time_to_quit = 0;

static unsigned char current_color = BLUE;

#define BUTTON 0
#define LEFT 1
#define RIGHT 2
#define UP 3
#define DOWN 4

static int button_pressed[5] = { 0 };

void FbColor(int color)
{
    current_color = (unsigned char) (color % 8);
}

void FbInit(void)
{
}

static unsigned char screen_color[SCREEN_XDIM][SCREEN_YDIM];
static unsigned char live_screen_color[SCREEN_XDIM][SCREEN_YDIM];

void plot_point(int x, int y, void *context)
{
    unsigned char *screen_color = context;

    screen_color[x * SCREEN_YDIM + y] = current_color;
}

void clear_point(int x, int y, void *context)
{
    unsigned char *screen_color = context;

    screen_color[x * SCREEN_YDIM + y] = BLACK;
}

void FbSwapBuffers(void)
{
	memcpy(live_screen_color, screen_color, sizeof(live_screen_color));
}

void FbLine(unsigned char x1, unsigned char y1, unsigned char x2, unsigned char y2)
{
    bline(x1, y1, x2, y2, plot_point, screen_color);
}

void FbHorizontalLine(unsigned char x1, unsigned char y1, unsigned char x2, unsigned char y2)
{
    unsigned char x;

    for (x = x1; x <= x2; x++)
        plot_point(x, y1, screen_color);
}

void FbVerticalLine(unsigned char x1, unsigned char y1, unsigned char x2, unsigned char y2)
{
    unsigned char y;

    for (y = y1; y <= y2; y++)
        plot_point(x1, y, screen_color);
}

void FbClear(void)
{
    memset(screen_color, BLACK, SCREEN_XDIM * SCREEN_YDIM);
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
				plot_point(sx, sy, screen_color);
			} else {
				clear_point(sx, sy, screen_color);
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

void itoa(char *string, int value, __attribute__((unused)) int base)
{
	sprintf(string, "%d", value);
}

int abs(int x)
{
    return x > 0 ? x : -x;
}

void returnToMenus(void)
{
	exit(0);
}

static void ir_packet_ignore(unsigned int packet)
{
}

static void (*ir_packet_callback)(unsigned int) = ir_packet_ignore;


void register_ir_packet_callback(void (*callback)(unsigned int))
{
	ir_packet_callback = callback;
}

void unregister_ir_packet_callback(void (*callback)(unsigned int))
{
	ir_packet_callback = ir_packet_ignore;
}

static int fifo_fd = -1;

static void *read_from_fifo(void *thread_info)
{
	int rc, count, bytesleft;
	unsigned int buffer;
	unsigned char *buf = (unsigned char *) &buffer;

	fifo_fd = open("/tmp/badge-ir-fifo", O_RDONLY);
	if (fifo_fd < 0) {
		fprintf(stderr, "Failed to open /tmp/badge-ir-fifo: %s\n", strerror(errno));
		exit(1);
		return NULL;
	}

	count = 0;
	bytesleft = 4;
	do {
		do {
			rc = read(fifo_fd, &buf[count], bytesleft);
			if (rc < 0 && errno == EINTR)
				continue;
			if (rc < 0) {
				fprintf(stderr, "Failed to read from /tmp/badge-ir-fifo: %s\n", strerror(errno));
				exit(1);
				break;
			}
			if (rc == bytesleft) {
				bytesleft = 4;
				count = 0;
				break;
			}
			bytesleft -= rc;
			count += rc;
		} while (bytesleft > 0);
		if (rc < 0)
			break;
		disable_interrupts();
		ir_packet_callback(buffer);
		enable_interrupts();
	} while (1);
	return NULL;
}

void setup_ir_sensor()
{
	pthread_t thr;
	int rc;

	rc = pthread_create(&thr, NULL, read_from_fifo, NULL);
	if (rc < 0)
		fprintf(stderr, "Failed to create thread to read from fifo: %s\n", strerror(errno));
}

static pthread_mutex_t interrupt_mutex = PTHREAD_MUTEX_INITIALIZER;

void disable_interrupts(void)
{
	pthread_mutex_lock(&interrupt_mutex);
}

void enable_interrupts(void)
{
	pthread_mutex_unlock(&interrupt_mutex);
}

void send_ir_packet(unsigned int packet)
{
	printf("Send packet to base station: 0x%08x\n", packet);
}

unsigned int get_badge_id(void)
{
	return 0x0100;
}

static void setup_window_geometry(GtkWidget *window)
{
	/* clamp window aspect ratio to constant */
	GdkGeometry geom;
	geom.min_aspect = (gdouble) GTK_SCREEN_WIDTH / (gdouble) GTK_SCREEN_HEIGHT;
	geom.max_aspect = geom.min_aspect;
	gtk_window_set_geometry_hints(GTK_WINDOW(window), NULL, &geom, GDK_HINT_ASPECT);
}

static gboolean delete_event(GtkWidget *widget,
        GdkEvent *event, gpointer data)
{
	/* If you return FALSE in the "delete_event" signal handler,
	 * GTK will emit the "destroy" signal. Returning TRUE means
	 * you don't want the window to be destroyed.
	 * This is useful for popping up 'are you sure you want to quit?'
	 * type dialogs. */

	/* Change TRUE to FALSE and the main window will be destroyed with
	 * a "delete_event". */
	return FALSE;
}

static void destroy(GtkWidget *widget, gpointer data)
{
	gtk_main_quit();
}

static gint key_press_cb(GtkWidget* widget, GdkEventKey* event, gpointer data)
{
	switch (event->keyval) {
	case GDK_w:
		button_pressed[UP] = 1;
		break;
	case GDK_s:
		button_pressed[DOWN] = 1;
		break;
	case GDK_a:
		button_pressed[LEFT] = 1;
		break;
	case GDK_d:
		button_pressed[RIGHT] = 1;
		break;
	case GDK_space:
		button_pressed[BUTTON] = 1;
		break;
	case GDK_q:
		time_to_quit = 1;
		break;
	}
	return TRUE;
}

static int drawing_area_expose(GtkWidget *widget, GdkEvent *event, gpointer p)
{
	/* Draw the screen */
	int x, y, w, h;

	w = real_screen_width / SCREEN_XDIM;
	if (w < 1)
		w = 1;
	h = real_screen_height / SCREEN_YDIM;
	if (h < 1)
		h = 1;

	for (y = 0; y < SCREEN_YDIM; y++) {
		for (x = 0; x < SCREEN_XDIM; x++) {
			unsigned char c = live_screen_color[x][y] % NCOLORS;
			gdk_gc_set_foreground(gc, &huex[c]);
			gdk_draw_rectangle(widget->window, gc, 1 /* filled */, x * w, y * h, w, h);
		}
	}
	return 0;
}

static gint drawing_area_configure(GtkWidget *w, GdkEventConfigure *event)
{
        GdkRectangle cliprect;

        /* first time through, gc is null, because gc can't be set without */
        /* a window, but, the window isn't there yet until it's shown, but */
        /* the show generates a configure... chicken and egg.  And we can't */
        /* proceed without gc != NULL...  but, it's ok, because 1st time thru */
        /* we already sort of know the drawing area/window size. */

        if (gc == NULL)
                return TRUE;

        real_screen_width =  w->allocation.width;
        real_screen_height =  w->allocation.height;

        gdk_gc_set_clip_origin(gc, 0, 0);
        cliprect.x = 0;
        cliprect.y = 0;
        cliprect.width = real_screen_width;
        cliprect.height = real_screen_height;
        gdk_gc_set_clip_rectangle(gc, &cliprect);
	return TRUE;
}

static void setup_gtk_colors(void)
{
	gdk_color_parse("white", &huex[WHITE]);
	gdk_color_parse("blue", &huex[BLUE]);
	gdk_color_parse("black", &huex[BLACK]);
	gdk_color_parse("green", &huex[GREEN]);
	gdk_color_parse("yellow", &huex[YELLOW]);
	gdk_color_parse("red", &huex[RED]);
	gdk_color_parse("cyan", &huex[CYAN]);
	gdk_color_parse("MAGENTA", &huex[MAGENTA]);
}

static void setup_gtk_window_and_drawing_area(GtkWidget **window, GtkWidget **vbox, GtkWidget **drawing_area)
{
	GdkRectangle cliprect;
	int i;

	*window = gtk_window_new(GTK_WINDOW_TOPLEVEL);
	setup_window_geometry(*window);
	gtk_container_set_border_width(GTK_CONTAINER(*window), 0);
	*vbox = gtk_vbox_new(FALSE, 0);
	gtk_window_move(GTK_WINDOW(*window), screen_offset_x, screen_offset_y);
	*drawing_area = gtk_drawing_area_new();
        g_signal_connect(G_OBJECT(*window), "delete_event",
                G_CALLBACK(delete_event), NULL);
        g_signal_connect(G_OBJECT(*window), "destroy",
                G_CALLBACK(destroy), NULL);
        g_signal_connect(G_OBJECT(*window), "key_press_event",
                G_CALLBACK(key_press_cb), "window");
        g_signal_connect(G_OBJECT(*drawing_area), "expose_event",
                G_CALLBACK(drawing_area_expose), NULL);
        g_signal_connect(G_OBJECT(*drawing_area), "configure_event",
                G_CALLBACK(drawing_area_configure), NULL);
        gtk_container_add(GTK_CONTAINER(*window), *vbox);
        gtk_box_pack_start(GTK_BOX(*vbox), *drawing_area, TRUE /* expand */, TRUE /* fill */, 0);
        gtk_window_set_default_size(GTK_WINDOW(*window), real_screen_width, real_screen_height);

	for (i = 0; i < NCOLORS; i++)
		gdk_colormap_alloc_color(gtk_widget_get_colormap(*drawing_area), &huex[i], FALSE, FALSE);

        gtk_widget_modify_bg(*drawing_area, GTK_STATE_NORMAL, &huex[BLACK]);
        gtk_widget_show(*vbox);
        gtk_widget_show(*drawing_area);
	gtk_widget_show(*window);
	gc = gdk_gc_new(GTK_WIDGET(*drawing_area)->window);
	gdk_gc_set_foreground(gc, &huex[WHITE]);

	gdk_gc_set_clip_origin(gc, 0, 0);
	cliprect.x = 0;
	cliprect.y = 0;
	cliprect.width = real_screen_width;
	cliprect.height = real_screen_height;
	gdk_gc_set_clip_rectangle(gc, &cliprect);
}

static gint advance_game(__attribute__((unused)) gpointer data)
{
	if (time_to_quit)
		exit(0);
	badge_function();
	gdk_threads_enter();
	gtk_widget_queue_draw(drawing_area);
	gdk_threads_leave();
	return TRUE;
}

void start_gtk(int *argc, char ***argv, int (*main_badge_function)(void), int callback_hz)
{
	gtk_set_locale();
	gtk_init(argc, argv);
	setup_gtk_colors();
	setup_gtk_window_and_drawing_area(&window, &vbox, &drawing_area);
	badge_function = main_badge_function;
	timer_tag = g_timeout_add(1000 / callback_hz, advance_game, NULL);

	/* Apparently (some versions of?) portaudio calls g_thread_init(). */
	/* It may only be called once, and subsequent calls abort, so */
	/* only call it if the thread system is not already initialized. */
	if (!g_thread_supported ())
		g_thread_init(NULL);
	gdk_threads_init();
	gtk_main();
}

static int generic_button_pressed(int which_button)
{
	if (button_pressed[which_button]) {
		button_pressed[which_button] = 0;
		return 1;
	}
	return 0;
}

int button_pressed_and_consume()
{
	return generic_button_pressed(BUTTON);
}

int top_tap_and_consume()
{
	return generic_button_pressed(UP);
}

int bottom_tap_and_consume()
{
	return generic_button_pressed(DOWN);
}

int left_tap_and_consume()
{
	return generic_button_pressed(LEFT);
}

int right_tap_and_consume()
{
	return generic_button_pressed(RIGHT);
}


