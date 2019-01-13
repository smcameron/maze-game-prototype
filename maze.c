#include <stdio.h>
#include <stdlib.h> /* for rand() */
#include <sys/time.h> /* for gettimeofday */
#include <string.h> /* for memset */

#include "bline.h"

/* Program states.  Initial state is MAZE_INIT */
enum maze_program_state_t {
    MAZE_INIT,
    MAZE_BUILD,
    MAZE_PRINT,
    MAZE_RENDER,
    MAZE_OBJECT_RENDER,
    MAZE_SCREEN_RENDER,
    MAZE_PROCESS_COMMANDS,
    MAZE_EXIT
};
static enum maze_program_state_t maze_program_state = MAZE_INIT;
static int maze_back_wall_distance = 7;
static int maze_object_distance_limit = 0;
static int maze_start = 0;
static int maze_scale = 12;
static int current_drawing_object = 0;

#define TERMINATE_CHANCE 5
#define BRANCH_CHANCE 30
#define OBJECT_CHANCE 20

/* Dimensions of generated maze */
#define XDIM (4 * 16)
#define YDIM 64

/* Array to hold the maze.  Each square of the maze is represented by 1 bit.
 * 0 means solid rock, 1 means empty passage.
 */
static unsigned char maze[XDIM >> 3][YDIM] = { { 0 }, };

/*
 * Stack structure used when generating maze to remember where we left off.
 */
typedef struct maze_gen_stack_element {
    unsigned char x, y, direction;
} maze_gen_stack_element_t;
static maze_gen_stack_element_t maze_stack[50];
#define MAZE_STACK_EMPTY -1
static short maze_stack_ptr = MAZE_STACK_EMPTY;
static int maze_size = 0;
static int max_maze_stack_depth = 0;
static int generation_iterations = 0;

typedef struct player_state {
    unsigned char x, y, direction;
} player_state_t;
static player_state_t player;

typedef struct point {
    signed char x, y;
} maze_point_t;

typedef union maze_object_type_specific_data {
    struct {
        unsigned char hitpoints;
    } monster;
    struct {
        signed char health_impact;
    } potion;
} maze_object_type_specific_data_t;

enum maze_object_category {
    MAZE_OBJECT_MONSTER,
    MAZE_OBJECT_WEAPON,
    MAZE_OBJECT_KEY,
    MAZE_OBJECT_POTION,
    MAZE_OBJECT_ARMOR,
    MAZE_OBJECT_TREASURE
};

typedef struct maze_object_template {
    char name[20];
    enum maze_object_category category;
    maze_point_t *drawing;
    int npoints;
} maze_object_template_t;

typedef struct object {
    unsigned char x, y;
    unsigned char type;
} maze_object_t;

static maze_point_t scroll_points[] =
#include "scroll_points.h"

static maze_point_t dragon_points[] =
#include "dragon_points.h"

static maze_point_t chest_points[] =
#include "chest_points.h"

static maze_point_t cobra_points[] =
#include "cobra_points.h"

static maze_point_t grenade_points[] =
#include "grenade_points.h"

static maze_point_t key_points[] =
#include "key_points.h"

static maze_point_t orc_points[] =
#include "orc_points.h"

static maze_point_t phantasm_points[] =
#include "phantasm_points.h"

static maze_point_t potion_points[] =
#include "potion_points.h"

static maze_point_t shield_points[] =
#include "shield_points.h"

static maze_point_t sword_points[] =
#include "sword_points.h"

#define MAZE_NOBJECT_TYPES 11
static int nobject_types = MAZE_NOBJECT_TYPES;

#define MAX_MAZE_OBJECTS 30
#define ARRAYSIZE(x) (sizeof((x)) / sizeof((x)[0]))

static maze_object_template_t maze_object_template[] = {
    { "SCROLL", MAZE_OBJECT_WEAPON, scroll_points, ARRAYSIZE(scroll_points), },
    { "DRAGON", MAZE_OBJECT_WEAPON, dragon_points, ARRAYSIZE(dragon_points), },
    { "CHEST", MAZE_OBJECT_TREASURE, chest_points, ARRAYSIZE(chest_points), },
    { "COBRA", MAZE_OBJECT_MONSTER, cobra_points, ARRAYSIZE(cobra_points), },
    { "GRENADE", MAZE_OBJECT_WEAPON, grenade_points, ARRAYSIZE(grenade_points), },
    { "KEY", MAZE_OBJECT_KEY, key_points, ARRAYSIZE(key_points), },
    { "ORC", MAZE_OBJECT_MONSTER, orc_points, ARRAYSIZE(orc_points), },
    { "PHANTASM", MAZE_OBJECT_MONSTER, phantasm_points, ARRAYSIZE(phantasm_points), },
    { "POTION", MAZE_OBJECT_POTION, potion_points, ARRAYSIZE(potion_points), },
    { "SHIELD", MAZE_OBJECT_ARMOR, shield_points, ARRAYSIZE(shield_points), },
    { "SWORD", MAZE_OBJECT_WEAPON, sword_points, ARRAYSIZE(sword_points), },
};

static maze_object_t maze_object[MAX_MAZE_OBJECTS];
static int nmaze_objects = 0;

static int min_maze_size(void)
{
    return XDIM * YDIM / 3;
}

/* X and Y offsets for 8 cardinal directions: N, NE, E, SE, S, SW, W, NW */
static const char xoff[] = { 0, 1, 1, 1, 0, -1, -1, -1 };
static const char yoff[] = { -1, -1, 0, 1, 1, 1, 0, -1 };

static void maze_stack_push(unsigned char x, unsigned char y, unsigned char direction)
{
    maze_stack_ptr++;
    if (maze_stack_ptr > 0 && maze_stack_ptr >= (ARRAYSIZE(maze_stack))) {
        /* Oops, we blew our stack.  Start over */
        printf("Oops, stack blew... size = %d\n", maze_stack_ptr);
        maze_program_state = MAZE_INIT;
        return;
    }
    if (max_maze_stack_depth < maze_stack_ptr)
        max_maze_stack_depth = maze_stack_ptr;
    maze_stack[maze_stack_ptr].x = x;
    maze_stack[maze_stack_ptr].y = y;
    maze_stack[maze_stack_ptr].direction = direction;
}

static void maze_stack_pop(void)
{
    maze_stack_ptr--;
}

/* Initial program state to kick off maze generation */
static void maze_init(void)
{
    player.x = XDIM / 2;
    player.y = YDIM - 2;
    player.direction = 0;
    max_maze_stack_depth = 0;
    memset(maze, 0, sizeof(maze));
    maze_stack_ptr = MAZE_STACK_EMPTY;
    maze_stack_push(player.x, player.y, player.direction);
    maze_program_state = MAZE_BUILD;
    maze_size = 0;
    nmaze_objects = 0;
}

/* Returns 1 if (x,y) is empty passage, 0 if solid rock */
static unsigned char is_passage(unsigned char x, unsigned char y)
{
    return maze[x >> 3][y] & (1 << (x % 8));
}

/* Sets maze square at x,y to 1 (empty passage) */
static void dig_maze_square(unsigned char x, unsigned char y)
{
    unsigned char bit = 1 << (x % 8);
    x = x >> 3;
    maze[x][y] |= bit;
    maze_size++;
}

/* Returns 0 if x,y are in bounds of maze dimensions, 1 otherwise */
static int out_of_bounds(int x, int y)
{
    if (x < XDIM && y < YDIM && x >= 0 && y >= 0)
        return 0;
    return 1;
}

static unsigned char normalize_direction(int direction)
{
    if (direction < 0)
        return (unsigned char) direction + 8;
    if (direction > 7)
        return (unsigned char) direction - 8;
    return direction;
}

/* Consider whether digx, digy is diggable.  */
static int diggable(unsigned char digx, unsigned char digy, unsigned char direction)
{
    int i, startdir, enddir;

    if (out_of_bounds(digx, digy)) /* not diggable if out of bounds */
        return 0;


    startdir = normalize_direction(((int) direction) - 2);
    enddir = normalize_direction(((int) direction) + 3);

    i = startdir;
    while (i != enddir) {
        unsigned char x, y;

        x = digx + xoff[i];
        y = digy + yoff[i];

        if (out_of_bounds(x, y)) /* We do not dig at the edge of the maze */
            return 0;
        if (is_passage(x, y)) /* do not connect to other passages */
            return 0;
        i = normalize_direction(i + 1);
    }
    return 1;
}

/* Rolls the dice and returns 1 chance percent of the time
 * e.g. random_chance(80) returns 1 80% of the time, and 0
 * 20% of the time
 */
static int random_choice(int chance)
{
    return (rand() % 10000) < 100 * chance;
}

static unsigned char left_dir(int direction)
{
    return normalize_direction(direction - 2);
}

static unsigned char right_dir(int direction)
{
    return normalize_direction(direction + 2);
}

static void add_object(int x, int y)
{
    if (nmaze_objects >= MAX_MAZE_OBJECTS)
        return;

    maze_object[nmaze_objects].x = x;
    maze_object[nmaze_objects].y = y;
    maze_object[nmaze_objects].type = rand() % nobject_types;
    nmaze_objects++;
}

static void print_maze();
/* Normally this would be recursive, but instead we use an explicit stack
 * to enable this to yield and then restart as needed.
 */
static void generate_maze(void)
{
    static int counter = 0;
    unsigned char *x, *y, *d;
    unsigned char nx, ny;

    x = &maze_stack[maze_stack_ptr].x;
    y = &maze_stack[maze_stack_ptr].y;
    d = &maze_stack[maze_stack_ptr].direction;

    counter++;
    generation_iterations++;

    dig_maze_square(*x, *y);

    if (random_choice(OBJECT_CHANCE))
        add_object(*x, *y);
    nx = *x + xoff[*d];
    ny = *y + yoff[*d];
    if (!diggable(nx, ny, *d))
        maze_stack_pop();
    if (maze_stack_ptr == MAZE_STACK_EMPTY) {
        maze_program_state = MAZE_PRINT;
        if (maze_size < min_maze_size()) {
            printf("maze too small, starting over\n");
            maze_program_state = MAZE_INIT;
        }
        return;
    }
    *x = nx;
    *y = ny;
    if (random_choice(TERMINATE_CHANCE)) {
        maze_stack_pop();
        if (maze_stack_ptr == MAZE_STACK_EMPTY) {
            maze_program_state = MAZE_PRINT;
            if (maze_size < min_maze_size()) {
                printf("maze too small, starting over\n");
                maze_program_state = MAZE_INIT;
            }
            return;
        }
    }
    if (random_choice(BRANCH_CHANCE)) {
        int new_dir = random_choice(50) ? right_dir(*d) : left_dir(*d);
        if (diggable(xoff[new_dir] + nx, yoff[new_dir] + ny, new_dir))
            maze_stack_push(nx, ny, (unsigned char) new_dir);
    }
}

static void print_maze()
{
    int i, j;

    for (j = 0; j < YDIM; j++) {
        for (i = 0; i < XDIM; i++) {
            if (is_passage(i, j))
                if (j == player.y && i == player.x)
                    printf("@");
                else
                    printf(" ");
            else
                printf("#");
        }
        printf("\n");
    }
    maze_program_state = MAZE_RENDER;
    printf("maze_size = %d, max stack depth = %d, generation_iterations = %d\n", maze_size, max_maze_stack_depth, generation_iterations);
}

#define SCREEN_XDIM 128
#define SCREEN_YDIM 96
static unsigned char screen[SCREEN_XDIM][SCREEN_YDIM];


static void plot_point(int x, int y, void *context)
{
    unsigned char *screen = context;

    screen[SCREEN_YDIM * x + y] = '#';
}

/* These integer ratios approximate 0.4, 0.4 * 0.8, 0.4 * 0.8^2, 0.4 * 0.8^3, 0.4 * 0.8^4, ...
 * which are used to approximate appropriate perspective scaling (reduction in size as viewing
 * distance linearly increases stepwise) while avoiding floating point operations.
 */
static const int drawing_scale_numerator[] = { 4, 32, 27, 204, 163, 131, 104, 84 };
static const int drawing_scale_denom[] = { 10, 100, 100, 1000, 1000, 1000, 1000, 1000 };

static void draw_object(maze_point_t drawing[], int npoints, int scale_index)
{
    int i;
    static const int xcenter = SCREEN_XDIM / 2;
    static const int ycenter = SCREEN_YDIM / 2;
    int num, denom;

    num = drawing_scale_numerator[scale_index];
    denom = drawing_scale_denom[scale_index];

    for (i = 0; i < npoints - 1;) {
        if (drawing[i].x == -128) {
            i++;
            continue;
        }
        if (drawing[i + 1].x == -128) {
            i+=2;
            continue;
        }
        bline(xcenter + (drawing[i].x * num) / denom, ycenter + (drawing[i].y * num) / denom,
            xcenter + (drawing[i + 1].x * num) / denom, ycenter + (drawing[i + 1].y * num) / denom, plot_point, screen);
        i++;
    }
}

static void draw_left_passage(int start, int scale)
{
    bline(start, start, start, SCREEN_YDIM - 1 - start, plot_point, screen);
    bline(start, start + scale, start + scale, start + scale, plot_point, screen);
    bline(start, SCREEN_YDIM - 1 - (start + scale), start + scale, SCREEN_YDIM - 1 - (start + scale), plot_point, screen);
}

static void draw_right_passage(int start, int scale)
{
    bline(SCREEN_XDIM - 1 - start, start, SCREEN_XDIM - 1 - start, SCREEN_YDIM - 1 - start, plot_point, screen);
    bline(SCREEN_XDIM - 1 - start, start + scale, SCREEN_XDIM - 1 - (start + scale), start + scale, plot_point, screen);
    bline(SCREEN_XDIM - 1 - start, SCREEN_YDIM - 1 - (start + scale), SCREEN_XDIM - 1 - (start + scale), SCREEN_YDIM - 1 - (start + scale), plot_point, screen);
}

static void draw_left_wall(int start, int scale)
{
    bline(start, start, start, SCREEN_YDIM - 1 - start, plot_point, screen);
    bline(start, start, start + scale, start + scale, plot_point, screen);
    bline(start, SCREEN_YDIM - 1 - start, start + scale, SCREEN_YDIM - 1 - (start + scale), plot_point, screen);
}

static void draw_right_wall(int start, int scale)
{
    bline(SCREEN_XDIM - 1 - start, start, SCREEN_XDIM - 1 - start, SCREEN_YDIM - 1 - start, plot_point, screen);
    bline(SCREEN_XDIM - 1 - start, start, SCREEN_XDIM - 1 - (start + scale), start + scale, plot_point, screen);
    bline(SCREEN_XDIM - 1 - start, SCREEN_YDIM - 1 - start, SCREEN_XDIM - 1 - (start + scale), SCREEN_YDIM - 1 - (start + scale), plot_point, screen);
}

static void draw_forward_wall(int start, int scale)
{
    bline(start, start, start, SCREEN_YDIM - 1 - start, plot_point, screen);
    bline(SCREEN_XDIM - 1 - start, start, SCREEN_XDIM - 1 - start, SCREEN_YDIM - 1 - start, plot_point, screen);
    bline(start, start, SCREEN_XDIM - 1 - start, start, plot_point, screen);
    bline(start, SCREEN_YDIM - 1 - start, SCREEN_XDIM - 1 - start, SCREEN_YDIM - 1 - start, plot_point, screen);
}

static void render_screen(void)
{
    int x, y;

    for (y = 0; y < SCREEN_YDIM; y++) {
        for (x = 0; x < SCREEN_XDIM; x++)
            printf("%c", screen[x][y]);
        printf("\n");
    }
    maze_program_state = MAZE_PROCESS_COMMANDS;
}

static void process_commands(void)
{
    char cmd[100];
    char *n;
    printf("Enter command [f, l, r, b]:\n");
    n = fgets(cmd, sizeof(cmd), stdin);

    if (!n) {
        maze_program_state = MAZE_EXIT;
    } else {
        if (strncmp(cmd, "f", 1) == 0) {
            int newx, newy;
            newx = player.x + xoff[player.direction];
            newy = player.y + yoff[player.direction];
            if (!out_of_bounds(newx, newy) && is_passage(newx, newy)) {
                player.x = newx;
                player.y = newy;
            }
        } else if (strncmp(cmd, "b", 1) == 0) {
            int newx, newy, backwards;
            backwards = normalize_direction(player.direction + 4);
            newx = player.x + xoff[backwards];
            newy = player.y + yoff[backwards];
            if (!out_of_bounds(newx, newy) && is_passage(newx, newy)) {
                player.x = newx;
                player.y = newy;
            }
        } else if (strncmp(cmd, "l", 1) == 0) {
            player.direction = normalize_direction(player.direction - 2);
        } else if (strncmp(cmd, "r", 1) == 0) {
            player.direction = normalize_direction(player.direction + 2);
        } else if (strncmp(cmd, "m", 1) == 0) {
            maze_program_state = MAZE_PRINT;
            return;
        } else printf("Bad command. Use f, l, r\n");
    }
    maze_program_state = MAZE_RENDER;
}

static void draw_objects(void)
{
    int a, b, i, x[2], y[2], s, otype, npoints;
    maze_point_t *drawing;

    a = 0;
    b = 1;
    x[0] = player.x;
    y[0] = player.y;
    x[1] = player.x + xoff[player.direction] * 6;
    y[1] = player.y + yoff[player.direction] * 6;

    if (x[0] == x[1]) {
        if (y[0] > y[1]) {
            a = 1;
            b = 0;
        }
    } else if (y[0] == y[1]) {
        if (x[0] > x[1]) {
            a = 1;
            b = 0;
        }
    }

    i = current_drawing_object;
    otype = maze_object[i].type;
    drawing = maze_object_template[otype].drawing;
    npoints = maze_object_template[otype].npoints;
    if (x[0] == x[1]) {
        if (maze_object[i].x == x[0] && maze_object[i].y >= y[a] && maze_object[i].y <= y[b]) {
            s = abs(maze_object[i].y - player.y);
            if (s >= maze_object_distance_limit)
                goto next_object;
            draw_object(drawing, npoints, s);
        }
    } else if (y[0] == y[1]) {
        if (maze_object[i].y == y[0] && maze_object[i].x >= x[a] && maze_object[i].x <= x[b]) {
            s = abs(maze_object[i].x - player.x);
            if (s >= maze_object_distance_limit)
                goto next_object;
            draw_object(drawing, npoints, s);
        }
    }

next_object:
    current_drawing_object++;
    if (current_drawing_object >= nmaze_objects) {
        current_drawing_object = 0;
        maze_program_state = MAZE_SCREEN_RENDER;
    }
}

static int maze_render_step = 0;

static void render_maze(void)
{
    int x, y, ox, oy, left, right;
    const int steps = 7;
    int hit_back_wall = 0;

    if (maze_render_step == 0)
        memset(screen, ' ', SCREEN_XDIM * SCREEN_YDIM);

    ox = player.x + xoff[player.direction] * maze_render_step;
    oy = player.y + yoff[player.direction] * maze_render_step;

    /* Draw the wall we might be facing */
    x = ox;
    y = oy;
    if (!out_of_bounds(x, y)) {
        if (!is_passage(x, y)) {
            // draw_forward_wall(maze_start + maze_scale, (maze_scale * 80) / 100);
            draw_forward_wall(maze_start, (maze_scale * 80) / 100);
            hit_back_wall = 1;
        }
    }
    /* Draw the wall or passage to our left */
    left = left_dir(player.direction);
    x = ox + xoff[left];
    y = oy + yoff[left];
    if (!out_of_bounds(x, y) && !hit_back_wall) {
        if (is_passage(x, y))
            draw_left_passage(maze_start, maze_scale);
        else
            draw_left_wall(maze_start, maze_scale);
    }
    /* Draw the wall or passage to our right */
    right = right_dir(player.direction);
    x = ox + xoff[right];
    y = oy + yoff[right];
    if (!out_of_bounds(x, y) && !hit_back_wall) {
        if (is_passage(x, y))
            draw_right_passage(maze_start, maze_scale);
        else
            draw_right_wall(maze_start, maze_scale);
    }
    /* Advance forward ahead of the player in our rendering */
    maze_start = maze_start + maze_scale;
    maze_scale = (maze_scale * 80) / 100;
    if (hit_back_wall) { /* If we are facing a wall, do not draw beyond that wall. */
        maze_back_wall_distance = maze_render_step; /* used by draw_objects */
        maze_object_distance_limit = maze_render_step;
        maze_render_step = steps;
    }
    maze_render_step++;
    if (maze_render_step >= steps) {
        maze_render_step = 0;
        maze_back_wall_distance = steps;
        maze_object_distance_limit = steps;
        maze_program_state = MAZE_OBJECT_RENDER;
        maze_start = 0;
        maze_scale = 12;
   }
}

static int maze_loop(void)
{
    switch (maze_program_state) {
    case MAZE_INIT:
        maze_init();
        break;
    case MAZE_BUILD:
        generate_maze();
        break;
    case MAZE_PRINT:
        print_maze();
        break;
    case MAZE_RENDER:
        render_maze();
        break;
    case MAZE_OBJECT_RENDER:
        draw_objects();
        break;
    case MAZE_SCREEN_RENDER:
        render_screen();
        break;
    case MAZE_PROCESS_COMMANDS:
        process_commands();
        break;
    case MAZE_EXIT:
        return 1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    srand(tv.tv_usec);

    do {
        if (maze_loop())
            return 0;
    } while (1);
    return 0;
}
