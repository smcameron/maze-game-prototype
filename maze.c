/*********************************************

 Pseudo 3D maze game for RVASEC badge hardware.

 Author: Stephen M. Cameron <stephenmcameron@gmail.com>
 (c) 2019 Stephen M. Cameron

 If you're wondering why this program is written
 in such a strange way, it's because of the environment
 it needs to fit into.  This program is meant to run
 on a PIC32 in a kind of cooperative multiprogram system
 and can't monopolize the CPU for any significant length
 of time (though it does have some compatibility bodges that
 allow it to run as a native linux program too.)  That is
 why maze_loop() is just a big switch statement that does
 different things based on maze_program_state and why there
 are so many global variables.  So the weirdness is there
 for a reason, and is not how I would normally write a program.

**********************************************/
#ifdef __linux__
#include <stdio.h>
#include <sys/time.h> /* for gettimeofday */
#include <string.h> /* for memset */
#endif

#ifdef __linux__
#include "linuxcompat.h"
#include "bline.h"
#endif

#include "build_bug_on.h"
#include "xorshift.h"


/* Program states.  Initial state is MAZE_GAME_INIT */
enum maze_program_state_t {
    MAZE_GAME_INIT,
    MAZE_GAME_START_MENU,
    MAZE_LEVEL_INIT,
    MAZE_BUILD,
    MAZE_PRINT,
    MAZE_RENDER,
    MAZE_OBJECT_RENDER,
    MAZE_RENDER_ENCOUNTER,
    MAZE_DRAW_STATS,
    MAZE_SCREEN_RENDER,
    MAZE_PROCESS_COMMANDS,
    MAZE_DRAW_MAP,
    MAZE_DRAW_MENU,
    MAZE_STATE_GO_DOWN,
    MAZE_STATE_GO_UP,
    MAZE_STATE_FIGHT,
    MAZE_STATE_FLEE,
    MAZE_EXIT
};
static enum maze_program_state_t maze_program_state = MAZE_GAME_INIT;
static int maze_back_wall_distance = 7;
static int maze_object_distance_limit = 0;
static int maze_start = 0;
static int maze_scale = 12;
static int current_drawing_object = 0;
static unsigned int xorshift_state = 0xa5a5a5a5;

#define TERMINATE_CHANCE 5
#define BRANCH_CHANCE 30
#define OBJECT_CHANCE 20

/* Dimensions of generated maze */
#define XDIM 24 /* Must be divisible by 8 */
#define YDIM 24
#define NLEVELS 3

static unsigned int maze_random_seed[NLEVELS] = { 0 };
static int maze_current_level = 0;

#define MAZE_PLACE_PLAYER_DO_NOT_MOVE 0
#define MAZE_PLACE_PLAYER_BENEATH_UP_LADDER 1
#define MAZE_PLACE_PLAYER_ABOVE_DOWN_LADDER 2
static int maze_player_initial_placement = MAZE_PLACE_PLAYER_BENEATH_UP_LADDER;

/* Array to hold the maze.  Each square of the maze is represented by 1 bit.
 * 0 means solid rock, 1 means empty passage.
 */
static unsigned char maze[XDIM >> 3][YDIM] = { { 0 }, };
static unsigned char maze_visited[XDIM >> 3][YDIM] = { { 0 }, };

/*
 * Stack structure used when generating maze to remember where we left off.
 */
static struct maze_gen_stack_element {
    unsigned char x, y, direction;
} maze_stack[50];
#define MAZE_STACK_EMPTY -1
static short maze_stack_ptr = MAZE_STACK_EMPTY;
static int maze_size = 0;
static int max_maze_stack_depth = 0;
static int generation_iterations = 0;

static struct player_state {
    unsigned char x, y, direction;
    unsigned char hitpoints;
    int gp;
} player;

struct point {
    signed char x, y;
};

union maze_object_type_specific_data {
    struct {
        unsigned char hitpoints;
    } monster;
    struct {
        signed char health_impact;
    } potion;
    struct {
        unsigned char protection;
    } shield;
    struct {
      unsigned char gp;
    } treasure;
};

enum maze_object_category {
    MAZE_OBJECT_MONSTER,
    MAZE_OBJECT_WEAPON,
    MAZE_OBJECT_KEY,
    MAZE_OBJECT_POTION,
    MAZE_OBJECT_ARMOR,
    MAZE_OBJECT_TREASURE,
    MAZE_OBJECT_DOWN_LADDER,
    MAZE_OBJECT_UP_LADDER
};

struct maze_object_template {
    char name[14];
    enum maze_object_category category;
    struct point *drawing;
    int npoints;
};

struct maze_object {
    unsigned char x, y;
    unsigned char type;
    union maze_object_type_specific_data tsd;
};

static struct point scroll_points[] =
#include "scroll_points.h"

static struct point dragon_points[] =
#include "dragon_points.h"

static struct point chest_points[] =
#include "chest_points.h"

static struct point cobra_points[] =
#include "cobra_points.h"

static struct point grenade_points[] =
#include "grenade_points.h"

static struct point key_points[] =
#include "key_points.h"

static struct point orc_points[] =
#include "orc_points.h"

static struct point phantasm_points[] =
#include "phantasm_points.h"

static struct point potion_points[] =
#include "potion_points.h"

static struct point shield_points[] =
#include "shield_points.h"

static struct point sword_points[] =
#include "sword_points.h"

static struct point up_ladder_points[] =
#include "up_ladder_points.h"

static struct point down_ladder_points[] =
#include "down_ladder_points.h"

#define MAZE_NOBJECT_TYPES 13
static int nobject_types = MAZE_NOBJECT_TYPES;

#define MAX_MAZE_OBJECTS 30
#define ARRAYSIZE(x) (sizeof((x)) / sizeof((x)[0]))

static struct maze_object_template maze_object_template[] = {
    { "SCROLL", MAZE_OBJECT_WEAPON, scroll_points, ARRAYSIZE(scroll_points), },
    { "DRAGON", MAZE_OBJECT_MONSTER, dragon_points, ARRAYSIZE(dragon_points), },
    { "CHEST", MAZE_OBJECT_TREASURE, chest_points, ARRAYSIZE(chest_points), },
    { "COBRA", MAZE_OBJECT_MONSTER, cobra_points, ARRAYSIZE(cobra_points), },
    { "HOLY GRENADE", MAZE_OBJECT_WEAPON, grenade_points, ARRAYSIZE(grenade_points), },
    { "KEY", MAZE_OBJECT_KEY, key_points, ARRAYSIZE(key_points), },
    { "SCARY ORC", MAZE_OBJECT_MONSTER, orc_points, ARRAYSIZE(orc_points), },
    { "PHANTASM", MAZE_OBJECT_MONSTER, phantasm_points, ARRAYSIZE(phantasm_points), },
    { "POTION", MAZE_OBJECT_POTION, potion_points, ARRAYSIZE(potion_points), },
    { "SHIELD", MAZE_OBJECT_ARMOR, shield_points, ARRAYSIZE(shield_points), },
    { "SWORD", MAZE_OBJECT_WEAPON, sword_points, ARRAYSIZE(sword_points), },
#define DOWN_LADDER 11
    { "LADDER", MAZE_OBJECT_DOWN_LADDER, down_ladder_points, ARRAYSIZE(down_ladder_points), },
#define UP_LADDER 12
    { "LADDER", MAZE_OBJECT_UP_LADDER, up_ladder_points, ARRAYSIZE(down_ladder_points), },
};

struct maze_menu_item {
    char text[15];
    enum maze_program_state_t next_state;
    unsigned char cookie;
};

static struct maze_menu {
    char title[15];
    struct maze_menu_item item[20];
    unsigned char nitems;
    unsigned char current_item;
    unsigned char menu_active;
    unsigned char chosen_cookie;
} maze_menu;

static struct maze_object maze_object[MAX_MAZE_OBJECTS];
static int nmaze_objects = 0;

static void maze_menu_clear(void)
{
    strncpy(maze_menu.title, "", sizeof(maze_menu.title) - 1);
    maze_menu.nitems = 0;
    maze_menu.current_item = 0;
    maze_menu.menu_active = 0;
    maze_menu.chosen_cookie = 0;
}

static void maze_menu_add_item(char *text, enum maze_program_state_t next_state, unsigned char cookie)
{
    int i;

    if (maze_menu.nitems >= ARRAYSIZE(maze_menu.item))
        return;

    i = maze_menu.nitems;
    strncpy(maze_menu.item[i].text, text, sizeof(maze_menu.item[i].text) - 1);
    maze_menu.item[i].next_state = next_state;
    maze_menu.item[i].cookie = cookie;
    maze_menu.nitems++;
}

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
#ifdef __linux__
        printf("Oops, stack blew... size = %d\n", maze_stack_ptr);
#endif
        maze_program_state = MAZE_LEVEL_INIT;
        maze_random_seed[maze_current_level] = xorshift(&xorshift_state);
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

static void player_init()
{
    player.hitpoints = 255;
    player.gp = 0;
}

/* Initial program state to kick off maze generation */
static void maze_init(void)
{
    FbInit();
    xorshift_state = maze_random_seed[maze_current_level];
    if (xorshift_state == 0)
        xorshift_state = 0xa5a5a5a5;
    player.x = XDIM / 2;
    player.y = YDIM - 2;
    player.direction = 0;
    max_maze_stack_depth = 0;
    memset(maze, 0, sizeof(maze));
    memset(maze_visited, 0, sizeof(maze_visited));
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

static void mark_maze_square_visited(unsigned char x, unsigned char y)
{
    unsigned char bit = 1 << (x % 8);
    x = x >> 3;
    maze_visited[x][y] |= bit;
}

static int is_visited(unsigned char x, unsigned char y)
{
    return maze_visited[x >> 3][y] & (1 << (x % 8));
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

static unsigned char left_dir(int direction)
{
    return normalize_direction(direction - 2);
}

static unsigned char right_dir(int direction)
{
    return normalize_direction(direction + 2);
}

/* Consider whether digx, digy is diggable.  */
static int diggable(unsigned char digx, unsigned char digy, unsigned char direction)
{
    int i, startdir, enddir;

    if (out_of_bounds(digx, digy)) /* not diggable if out of bounds */
        return 0;


    startdir = left_dir(direction);
    enddir = normalize_direction(right_dir(direction) + 1);

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
    return (xorshift(&xorshift_state) % 10000) < 100 * chance;
}

static void add_ladder(int ladder_type)
{
    int x, y;

    do {
        x = xorshift(&xorshift_state) % XDIM;
        y = xorshift(&xorshift_state) % YDIM;
    } while (!is_passage(x, y));

    maze_object[nmaze_objects].x = x;
    maze_object[nmaze_objects].y = y;
    maze_object[nmaze_objects].type = ladder_type;

    if ((maze_player_initial_placement == MAZE_PLACE_PLAYER_BENEATH_UP_LADDER &&
        ladder_type == UP_LADDER) ||
        (maze_player_initial_placement == MAZE_PLACE_PLAYER_ABOVE_DOWN_LADDER &&
        ladder_type == DOWN_LADDER)) {
        player.x = x;
        player.y = y;
    }

    nmaze_objects++;
}

static void add_ladders(int level)
{
    if (level < NLEVELS - 1)
        add_ladder(DOWN_LADDER);
    add_ladder(UP_LADDER);
}

static void add_random_object(int x, int y)
{
    if (nmaze_objects >= MAX_MAZE_OBJECTS - 2) /* Leave 2 for ladders */
        return;

    maze_object[nmaze_objects].x = x;
    maze_object[nmaze_objects].y = y;
    maze_object[nmaze_objects].type = xorshift(&xorshift_state) % (nobject_types - 2); /* minus 2 to exclude ladders */
    switch(maze_object_template[maze_object[nmaze_objects].type].category) {
    case MAZE_OBJECT_MONSTER:
        maze_object[nmaze_objects].tsd.monster.hitpoints = 10 + (xorshift(&xorshift_state) % 20);
        break;
    case MAZE_OBJECT_POTION:
        maze_object[nmaze_objects].tsd.potion.health_impact = (xorshift(&xorshift_state) % 40) - 10;
        break;
    case MAZE_OBJECT_ARMOR:
        maze_object[nmaze_objects].tsd.shield.protection = (xorshift(&xorshift_state) % 10);
        break;
    case MAZE_OBJECT_TREASURE:
        maze_object[nmaze_objects].tsd.treasure.gp = (xorshift(&xorshift_state) % 40);
        break;
    default:
        break;
    }
    nmaze_objects++;
}

static void print_maze()
{
#ifdef __linux__
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
    printf("maze_size = %d, max stack depth = %d, generation_iterations = %d\n", maze_size, max_maze_stack_depth, generation_iterations);
#endif
    maze_program_state = MAZE_RENDER;
}

/* Normally this would be recursive, but instead we use an explicit stack
 * to enable this to yield and then restart as needed.
 */
static void generate_maze(void)
{
    static int counter = 0;
    unsigned char *x, *y, *d;
    unsigned char nx, ny;

    BUILD_ASSERT((XDIM % 8) == 0); /* This is a build-time assertion that generates no code */

    x = &maze_stack[maze_stack_ptr].x;
    y = &maze_stack[maze_stack_ptr].y;
    d = &maze_stack[maze_stack_ptr].direction;

    counter++;
    generation_iterations++;

    dig_maze_square(*x, *y);

    if (random_choice(OBJECT_CHANCE))
        add_random_object(*x, *y);
    nx = *x + xoff[*d];
    ny = *y + yoff[*d];
    if (!diggable(nx, ny, *d))
        maze_stack_pop();
    if (maze_stack_ptr == MAZE_STACK_EMPTY) {
        maze_program_state = MAZE_PRINT;
        if (maze_size < min_maze_size()) {
#ifdef __linux
            printf("maze too small, starting over\n");
#endif
            maze_program_state = MAZE_LEVEL_INIT;
            maze_random_seed[maze_current_level] = xorshift(&xorshift_state);
        }
        add_ladders(0);
        return;
    }
    *x = nx;
    *y = ny;
    if (random_choice(TERMINATE_CHANCE)) {
        maze_stack_pop();
        if (maze_stack_ptr == MAZE_STACK_EMPTY) {
            maze_program_state = MAZE_PRINT;
            if (maze_size < min_maze_size()) {
#ifdef __linux
                printf("maze too small, starting over\n");
#endif
                maze_program_state = MAZE_LEVEL_INIT;
                maze_random_seed[maze_current_level] = xorshift(&xorshift_state);
            }
            add_ladders(0);
            return;
        }
    }
    if (random_choice(BRANCH_CHANCE)) {
        int new_dir = random_choice(50) ? right_dir(*d) : left_dir(*d);
        if (diggable(xoff[new_dir] + nx, yoff[new_dir] + ny, new_dir))
            maze_stack_push(nx, ny, (unsigned char) new_dir);
    }
}

static void draw_map()
{
    int x, y;

    FbClear();
    for (x = 0; x < XDIM; x++) {
        for (y = 0; y < YDIM; y++) {
            if (x == player.x && y == player.y) {
                FbLine(x * 3, y * 3, x * 3, y * 3);
                continue;
            }
            if (is_visited(x, y)) {
                FbHorizontalLine(x * 3 - 1, y * 3 - 1, x * 3 + 1, y * 3 - 1);
                FbHorizontalLine(x * 3 - 1, y * 3, x * 3 + 1, y * 3);
                FbHorizontalLine(x * 3 - 1, y * 3 + 1, x * 3 + 1, y * 3 + 1);
            }
       }
    }
    maze_program_state = MAZE_SCREEN_RENDER;
}

/* These integer ratios approximate 0.4, 0.4 * 0.8, 0.4 * 0.8^2, 0.4 * 0.8^3, 0.4 * 0.8^4, ...
 * which are used to approximate appropriate perspective scaling (reduction in size as viewing
 * distance linearly increases stepwise) while avoiding floating point operations.
 *
 * The denominator is always 1024.
 */
static const int drawing_scale_numerator[] = { 410, 328, 262, 210, 168, 134, 107, 86 };

static void draw_object(struct point drawing[], int npoints, int scale_index)
{
    int i;
    static const int xcenter = SCREEN_XDIM / 2;
    static const int ycenter = SCREEN_YDIM / 2;
    int num;

    num = drawing_scale_numerator[scale_index];

    for (i = 0; i < npoints - 1;) {
        if (drawing[i].x == -128) {
            i++;
            continue;
        }
        if (drawing[i + 1].x == -128) {
            i+=2;
            continue;
        }
        FbLine(xcenter + ((drawing[i].x * num) >> 10), ycenter + ((drawing[i].y * num) >> 10),
            xcenter + ((drawing[i + 1].x * num) >> 10), ycenter + ((drawing[i + 1].y * num) >> 10));
        i++;
    }
}

static void draw_left_passage(int start, int scale)
{
    FbVerticalLine(start, start, start, SCREEN_YDIM - 1 - start);
    FbHorizontalLine(start, start + scale, start + scale, start + scale);
    FbHorizontalLine(start, SCREEN_YDIM - 1 - (start + scale), start + scale, SCREEN_YDIM - 1 - (start + scale));
}

static void draw_right_passage(int start, int scale)
{
    FbVerticalLine(SCREEN_XDIM - 1 - start, start, SCREEN_XDIM - 1 - start, SCREEN_YDIM - 1 - start);
    FbHorizontalLine(SCREEN_XDIM - 1 - (start + scale), start + scale, SCREEN_XDIM - 1 - start, start + scale);
    FbHorizontalLine(SCREEN_XDIM - 1 - (start + scale), SCREEN_YDIM - 1 - (start + scale), SCREEN_XDIM - 1 - start, SCREEN_YDIM - 1 - (start + scale));
}

static void draw_left_wall(int start, int scale)
{
    FbVerticalLine(start, start, start, SCREEN_YDIM - 1 - start);
    FbLine(start, start, start + scale, start + scale);
    FbLine(start, SCREEN_YDIM - 1 - start, start + scale, SCREEN_YDIM - 1 - (start + scale));
}

static void draw_right_wall(int start, int scale)
{
    FbVerticalLine(SCREEN_XDIM - 1 - start, start, SCREEN_XDIM - 1 - start, SCREEN_YDIM - 1 - start);
    FbLine(SCREEN_XDIM - 1 - start, start, SCREEN_XDIM - 1 - (start + scale), start + scale);
    FbLine(SCREEN_XDIM - 1 - start, SCREEN_YDIM - 1 - start, SCREEN_XDIM - 1 - (start + scale), SCREEN_YDIM - 1 - (start + scale));
}

static void draw_forward_wall(int start, int scale)
{
    FbVerticalLine(start, start, start, SCREEN_YDIM - 1 - start);
    FbVerticalLine(SCREEN_XDIM - 1 - start, start, SCREEN_XDIM - 1 - start, SCREEN_YDIM - 1 - start);
    FbHorizontalLine(start, start, SCREEN_XDIM - 1 - start, start);
    FbHorizontalLine(start, SCREEN_YDIM - 1 - start, SCREEN_XDIM - 1 - start, SCREEN_YDIM - 1 - start);
}

static int go_up_or_down(int direction)
{
    int i, ok, ladder_type, placement;

    if (direction > 0) {
        ladder_type = DOWN_LADDER;
        placement = MAZE_PLACE_PLAYER_BENEATH_UP_LADDER;
    } else {
        ladder_type = UP_LADDER;
        placement = MAZE_PLACE_PLAYER_ABOVE_DOWN_LADDER;
    }

    ok = 0;
    /* Check if we are facing a down ladder */
    for (i = 0; i < nmaze_objects; i++) {
        if (maze_object[i].type == ladder_type &&
            maze_object[i].x == player.x && maze_object[i].y == player.y) {
                ok = 1;
                break;
        }
    }
    if (!ok)
        return 0;

    if (direction > 0 && maze_current_level >= NLEVELS - 1)
        return 0;
    else if (direction < 0 && maze_current_level <= 0)
        return 0;

    maze_current_level += direction;
    maze_program_state = MAZE_LEVEL_INIT;
    maze_player_initial_placement = placement;
    return 1;
}

static int go_up(void)
{
    return go_up_or_down(-1);
}

static int go_down(void)
{
    return go_up_or_down(1);
}

static char *encounter_text = "x";
static char *encounter_name = "";

static int check_for_encounter(unsigned char newx, unsigned char newy)
{
    int i, monster;

    monster = 0;
    encounter_text = "x";
    for (i = 0; i < nmaze_objects; i++) {
        /* If we are just about to move onto a square where an object is... */
        if (maze_object[i].x == newx && maze_object[i].y == newy) {
            switch(maze_object_template[maze_object[i].type].category) {
            case MAZE_OBJECT_MONSTER:
                encounter_text = "you encounter a";
                encounter_name = maze_object_template[maze_object[i].type].name;
                monster = 1;
                break;
            case MAZE_OBJECT_WEAPON:
            case MAZE_OBJECT_KEY:
            case MAZE_OBJECT_POTION:
            case MAZE_OBJECT_TREASURE:
            case MAZE_OBJECT_ARMOR:
                encounter_text = "you found a";
                encounter_name = maze_object_template[maze_object[i].type].name;
                break;
            case MAZE_OBJECT_DOWN_LADDER:
                encounter_text = "a ladder";
                encounter_name = "leads down";
                break;
            case MAZE_OBJECT_UP_LADDER:
                encounter_text = "a ladder";
                encounter_name = "leads up";
                break;
            default:
                encounter_text = "you found something";
                encounter_name = maze_object_template[maze_object[i].type].name;
                break;
            }
        }
        /* If we are just about to move off of a square where an object is... */
        if (maze_object[i].x == player.x && maze_object[i].y == player.y) {
            switch(maze_object_template[maze_object[i].type].category) {
            case MAZE_OBJECT_WEAPON:
               maze_object[i].x = 255; /* Take the object */
               break;
            case MAZE_OBJECT_KEY:
               maze_object[i].x = 255; /* Take the object */
               break;
            case MAZE_OBJECT_POTION:
               maze_object[i].x = 255; /* Take the object */
               break;
            case MAZE_OBJECT_TREASURE:
               maze_object[i].x = 255; /* Take the object */
               player.gp += maze_object[i].tsd.treasure.gp;
               break;
            case MAZE_OBJECT_ARMOR:
               maze_object[i].x = 255; /* Take the object */
               break;
            default:
               break;
            }
        }
    }
    return monster;
}

static void maze_button_pressed(void)
{
    int i;

    int newx, newy;
    int monster_present = 0;

    if (maze_menu.menu_active) {
        maze_program_state = maze_menu.item[maze_menu.current_item].next_state;
        maze_menu.chosen_cookie = maze_menu.item[maze_menu.current_item].cookie;
        maze_menu.menu_active = 0;
        return;
    }
    newx = player.x + xoff[player.direction];
    newy = player.y + yoff[player.direction];
    maze_menu_clear();
    strcpy(maze_menu.title, "CHOOSE ACTION");
    for (i = 0; i < nmaze_objects; i++) {
        if (player.x == maze_object[i].x && player.y == maze_object[i].y) {
            switch(maze_object_template[maze_object[i].type].category) {
            case MAZE_OBJECT_DOWN_LADDER:
                 maze_menu_add_item("CLIMB DOWN", MAZE_STATE_GO_DOWN, 1);
                 break;
            case MAZE_OBJECT_UP_LADDER:
                 maze_menu_add_item("CLIMB UP", MAZE_STATE_GO_UP, 1);
                 break;
            case MAZE_OBJECT_MONSTER:
                 monster_present = 1;
                 break;
            default:
                 break;
            }
        }
        if (maze_object[i].x == newx && maze_object[i].y == newy &&
            maze_object_template[maze_object[i].type].category == MAZE_OBJECT_MONSTER)
            monster_present = 1;
    }
    if (monster_present) {
        maze_menu_add_item("FIGHT MONSTER!", MAZE_STATE_FIGHT, 1);
        maze_menu_add_item("FLEE!", MAZE_STATE_FLEE, 1);
    }
    maze_menu_add_item("WIELD WEAPON", MAZE_RENDER, 1);
    maze_menu_add_item("READ SCROLL", MAZE_RENDER, 1);
    maze_menu_add_item("QUAFF POTION", MAZE_RENDER, 1);
    maze_menu_add_item("NEVER MIND", MAZE_RENDER, 1);
    maze_menu_add_item("EXIT GAME", MAZE_EXIT, 1);
    maze_menu.menu_active = 1;
    maze_program_state = MAZE_DRAW_MENU;
}

static void move_player_one_step(int direction)
{
    int newx, newy;
    newx = player.x + xoff[direction];
    newy = player.y + yoff[direction];
    if (!out_of_bounds(newx, newy) && is_passage(newx, newy)) {
        if (!check_for_encounter(newx, newy)) {
            player.x = newx;
            player.y = newy;
        }
    }
}

static void maze_menu_change_current_selection(int direction)
{
    maze_menu.current_item += direction;
    if (maze_menu.current_item < 0)
        maze_menu.current_item = maze_menu.nitems - 1;
    else if (maze_menu.current_item >= maze_menu.nitems)
        maze_menu.current_item = 0;
}

static void process_commands(void)
{
    int kp;

    wait_for_keypress();
    kp = get_keypress();
    if (kp < 0) {
        maze_program_state = MAZE_EXIT;
        return;
    }

    switch (kp) {
    case 'w':
        if (maze_menu.menu_active)
            maze_menu_change_current_selection(-1);
        else
            move_player_one_step(player.direction);
        break;
    case 's':
        if (maze_menu.menu_active)
            maze_menu_change_current_selection(1);
        else
            move_player_one_step(normalize_direction(player.direction + 4));
        break;
    case 'a':
        player.direction = left_dir(player.direction);
        break;
    case 'd':
        player.direction = right_dir(player.direction);
        break;
    case 'm':
        maze_program_state = MAZE_DRAW_MAP;
        return;
    case ' ':
        maze_button_pressed();
        break;
    case 'c':
        if (go_down())
            return;
        else
           maze_program_state = MAZE_RENDER;
        break;
    case 'e':
        if (go_up())
           return;
        else
           maze_program_state = MAZE_RENDER;
        break;
    case 'q':
        maze_program_state = MAZE_EXIT;
        return;
    default:
        break;
    }
    if (maze_program_state == MAZE_PROCESS_COMMANDS) {
        if (maze_menu.menu_active)
            maze_program_state = MAZE_DRAW_MENU;
        else
            maze_program_state = MAZE_RENDER;
    }
}

static void draw_objects(void)
{
    int a, b, i, x[2], y[2], s, otype, npoints;
    struct point *drawing;

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
        maze_program_state = MAZE_RENDER_ENCOUNTER;
    }
}

static int maze_render_step = 0;

static void render_maze(void)
{
    int x, y, ox, oy, left, right;
    const int steps = 7;
    int hit_back_wall = 0;

    if (maze_render_step == 0) {
        FbClear();
        maze_object_distance_limit = steps;
    }

    ox = player.x + xoff[player.direction] * maze_render_step;
    oy = player.y + yoff[player.direction] * maze_render_step;

    /* Draw the wall we might be facing */
    x = ox;
    y = oy;
    if (!out_of_bounds(x, y)) {
        if (!is_passage(x, y)) {
            draw_forward_wall(maze_start, (maze_scale * 80) / 100);
            hit_back_wall = 1;
        } else {
            mark_maze_square_visited(ox, oy);
        }
    }
    /* Draw the wall or passage to our left */
    left = left_dir(player.direction);
    x = ox + xoff[left];
    y = oy + yoff[left];
    if (!out_of_bounds(x, y) && !hit_back_wall) {
        if (is_passage(x, y)) {
            draw_left_passage(maze_start, maze_scale);
            mark_maze_square_visited(x, y);
        } else {
            draw_left_wall(maze_start, maze_scale);
        }
    }
    /* Draw the wall or passage to our right */
    right = right_dir(player.direction);
    x = ox + xoff[right];
    y = oy + yoff[right];
    if (!out_of_bounds(x, y) && !hit_back_wall) {
        if (is_passage(x, y)) {
            draw_right_passage(maze_start, maze_scale);
            mark_maze_square_visited(x, y);
        } else {
            draw_right_wall(maze_start, maze_scale);
        }
    }
    /* Advance forward ahead of the player in our rendering */
    maze_start = maze_start + maze_scale;
    maze_scale = (maze_scale * 819) >> 10; /* Approximately multiply by 0.8 */
    if (hit_back_wall) { /* If we are facing a wall, do not draw beyond that wall. */
        maze_back_wall_distance = maze_render_step; /* used by draw_objects */
        maze_object_distance_limit = maze_render_step;
        maze_render_step = steps;
    }
    maze_render_step++;
    if (maze_render_step >= steps) {
        maze_render_step = 0;
        maze_back_wall_distance = steps;
        maze_program_state = MAZE_OBJECT_RENDER;
        maze_start = 0;
        maze_scale = 12;
   }
}

static void draw_encounter(void)
{
    maze_program_state = MAZE_DRAW_STATS;

    if (encounter_text[0] == 'x')
        return;

    FbMove(10, 100);
    FbWriteLine(encounter_text);
    FbMove(10, 110);
    FbWriteLine(encounter_name);
}

static void maze_draw_stats(void)
{
    char gold_pieces[10], hitpoints[10];

    itoa(gold_pieces, player.gp, 10);
    itoa(hitpoints, player.hitpoints, 10);
    FbMove(2, 122);
    FbWriteLine("HP:");
    FbMove(2 + 24, 122);
    FbWriteLine(hitpoints);
    FbMove(55, 122);
    FbWriteLine("GP:");
    FbMove(55 + 24, 122);
    FbWriteLine(gold_pieces);

    maze_program_state = MAZE_SCREEN_RENDER;
}

static void init_seeds()
{
    int i;

    for (i = 0; i < NLEVELS; i++)
        maze_random_seed[i] = xorshift(&xorshift_state);
}

static void maze_game_init(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    xorshift_state = tv.tv_usec;
    if (xorshift_state == 0)
        xorshift_state = 0xa5a5a5a5;

    init_seeds();
    player_init();

    maze_program_state = MAZE_GAME_START_MENU;
}

static void maze_draw_menu(void)
{
    int i, y, first_item, last_item;

    first_item = maze_menu.current_item - 4;
    if (first_item < 0)
        first_item = 0;
    last_item = maze_menu.current_item + 4;
    if (last_item > maze_menu.nitems - 1)
        last_item = maze_menu.nitems - 1;

    FbClear();
    FbMove(8, 5);
    FbWriteLine(maze_menu.title);

    y = SCREEN_YDIM / 2 - 10 * (maze_menu.current_item - first_item);
    for (i = first_item; i <= last_item; i++) {
        FbMove(10, y);
        FbWriteLine(maze_menu.item[i].text);
        y += 10;
    }

    FbHorizontalLine(5, SCREEN_YDIM / 2 - 2, SCREEN_XDIM - 5, SCREEN_YDIM / 2 - 2);
    FbHorizontalLine(5, SCREEN_YDIM / 2 + 10, SCREEN_XDIM - 5, SCREEN_YDIM / 2 + 10);
    FbVerticalLine(5, SCREEN_YDIM / 2 - 2, 5, SCREEN_YDIM / 2 + 10);
    FbVerticalLine(SCREEN_XDIM - 5, SCREEN_YDIM / 2 - 2, SCREEN_XDIM - 5, SCREEN_YDIM / 2 + 10);
    maze_program_state = MAZE_SCREEN_RENDER;
}

static void maze_game_start_menu(void)
{

    maze_menu_clear();

    maze_menu.menu_active = 1;
    strcpy(maze_menu.title, "MINES OF BADGE");
    maze_menu_add_item("NEW GAME", MAZE_LEVEL_INIT, 0);
    maze_menu_add_item("EXIT GAME", MAZE_EXIT, 0);

    maze_program_state = MAZE_DRAW_MENU;
}

static int maze_loop(void)
{
    switch (maze_program_state) {
    case MAZE_GAME_INIT:
        maze_game_init();
        break;
    case MAZE_GAME_START_MENU:
        maze_game_start_menu();
        break;
    case MAZE_LEVEL_INIT:
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
    case MAZE_RENDER_ENCOUNTER:
        draw_encounter();
        break;
    case MAZE_SCREEN_RENDER:
        FbSwapBuffers();
        maze_program_state = MAZE_PROCESS_COMMANDS;
        break;
    case MAZE_PROCESS_COMMANDS:
        process_commands();
        break;
    case MAZE_DRAW_MAP:
        draw_map();
        break;
    case MAZE_DRAW_STATS:
        maze_draw_stats();
        break;
    case MAZE_DRAW_MENU:
        maze_draw_menu();
        break;
    case MAZE_STATE_GO_DOWN:
         if (!go_down())
            maze_program_state = MAZE_RENDER;
         break;
    case MAZE_STATE_GO_UP:
        if (!go_up())
            maze_program_state = MAZE_RENDER;
        break;
    case MAZE_STATE_FIGHT:
    case MAZE_STATE_FLEE:
         maze_program_state = MAZE_RENDER;
         break;
    case MAZE_EXIT:
        return 1;
    }
    return 0;
}

int main(int argc, char *argv[])
{
    do {
        if (maze_loop())
            return 0;
    } while (1);
    return 0;
}
