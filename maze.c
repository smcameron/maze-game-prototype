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
#else
#include "colors.h"
#include "menu.h"
#include "touchCTMU.h"
#endif

#include "build_bug_on.h"
#include "xorshift.h"


/* TODO figure out where these should really come from */
#define SCREEN_XDIM 132
#define SCREEN_YDIM 132

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
    MAZE_RENDER_COMBAT,
    MAZE_COMBAT_MONSTER_MOVE,
    MAZE_STATE_PLAYER_DEFEATS_MONSTER,
    MAZE_STATE_PLAYER_DIED,
    MAZE_CHOOSE_POTION,
    MAZE_QUAFF_POTION,
    MAZE_CHOOSE_WEAPON,
    MAZE_WIELD_WEAPON,
    MAZE_CHOOSE_ARMOR,
    MAZE_DON_ARMOR,
    MAZE_CHOOSE_TAKE_OBJECT,
    MAZE_CHOOSE_DROP_OBJECT,
    MAZE_TAKE_OBJECT,
    MAZE_DROP_OBJECT,
    MAZE_EXIT
};
static enum maze_program_state_t maze_program_state = MAZE_GAME_INIT;
static int maze_back_wall_distance = 7;
static int maze_object_distance_limit = 0;
static int maze_start = 0;
static int maze_scale = 12;
static int current_drawing_object = 0;
static unsigned int xorshift_state = 0xa5a5a5a5;
static unsigned char combat_mode = 0;

#define TERMINATE_CHANCE 5
#define BRANCH_CHANCE 30
#define OBJECT_CHANCE 20

/* Dimensions of generated maze */
#define XDIM 24 /* Must be divisible by 8 */
#define YDIM 24
#define NLEVELS 3

static unsigned int maze_random_seed[NLEVELS] = { 0 };
static int maze_previous_level = -1;
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
    unsigned char combatx, combaty;
    unsigned char hitpoints;
    unsigned char weapon;
    unsigned char armor;
    int gp;
} player, combatant;

struct point {
    signed char x, y;
};

static struct potion_descriptor {
    char *adjective;
    signed char health_impact;
} potion_type[] = {
    { "smelly", 0 },
    { "funky", 0 },
    { "misty", 0 },
    { "frothy", 0 },
    { "foamy", 0 },
    { "fulminating", 0 },
    { "smoking", 0 },
    { "sparkling", 0 },
    { "bubbling", 0 },
    { "acrid", 0 },
    { "pungent", 0 },
    { "stinky", 0 },
    { "aromatic", 0 },
    { "gelatinous", 0 },
    { "jiggly", 0 },
    { "glowing", 0 },
    { "luminescent", 0 },
    { "pearlescent", 0 },
    { "fruity", 0 },
};

static struct weapon_descriptor {
    char *adjective;
    char *name;
    unsigned char ranged;
    unsigned char shots;
    unsigned char damage;
} weapon_type[] = {
 { "ordinary", "dagger", 0, 0, 3 },
 { "black", "sword", 0, 0, 5 },
 { "short", "sword", 0, 0, 8 },
 { "broad", "sword", 0, 0, 10 },
 { "long", "sword", 0, 0, 10 },
 { "dwarven", "sword", 0, 0, 12 },
 { "orcish", "sword", 0, 0, 13 },
 { "golden", "sword", 0, 0, 14 },
 { "elvish", "sword", 0, 0, 15 },
 { "lost", "katana", 0, 0, 15 },
 { "great", "sword", 0, 0, 16 },
 { "cosmic", "sword", 0, 0, 20 },
 { "glowing", "sword", 0, 0, 22 },
 { "fire", "saber", 0, 0, 25 },
 { "flaming", "scimitar", 0, 0, 27 },
 { "electro", "cutlass", 0, 0, 30 },
};

static struct armor_descriptor {
    char *adjective;
    char *name;
    unsigned char protection;
} armor_type[] = {
    { "leather", "armor", 2 },
    { "chainmail", "armor", 5 },
    { "plate", "armor", 8 },
};

union maze_object_type_specific_data {
    struct {
        unsigned char hitpoints;
        unsigned char speed;
    } monster;
    struct {
        unsigned char type; /* index into potion_type[] */
    } potion;
    struct {
        unsigned char type; /* index into armor_type[] */
    } armor;
    struct {
      unsigned char gp;
    } treasure;
    struct {
      unsigned char type; /* index into weapon_type[] */
    } weapon;
};

enum maze_object_category {
    MAZE_OBJECT_MONSTER,
    MAZE_OBJECT_WEAPON,
    MAZE_OBJECT_SCROLL,
    MAZE_OBJECT_GRENADE,
    MAZE_OBJECT_KEY,
    MAZE_OBJECT_POTION,
    MAZE_OBJECT_ARMOR,
    MAZE_OBJECT_TREASURE,
    MAZE_OBJECT_DOWN_LADDER,
    MAZE_OBJECT_UP_LADDER
};

struct maze_object_template {
    char name[14];
    int color;
    enum maze_object_category category;
    struct point *drawing;
    int npoints;
    short speed;
    unsigned char hitpoints;
    unsigned char damage;
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

static struct point player_points[] =
#include "player_points.h"

static struct point bones_points[] =
#include "bones_points.h"

#define MAZE_NOBJECT_TYPES 13
static int nobject_types = MAZE_NOBJECT_TYPES;

#define MAX_MAZE_OBJECTS 30
#define ARRAYSIZE(x) (sizeof((x)) / sizeof((x)[0]))

static struct maze_object_template maze_object_template[] = {
    { "SCROLL", BLUE, MAZE_OBJECT_SCROLL, scroll_points, ARRAYSIZE(scroll_points), 0, 0, 10 },
    { "DRAGON", GREEN, MAZE_OBJECT_MONSTER, dragon_points, ARRAYSIZE(dragon_points), 8, 40, 20 },
    { "CHEST", YELLOW, MAZE_OBJECT_TREASURE, chest_points, ARRAYSIZE(chest_points), 0, 0, 0 },
    { "COBRA", GREEN, MAZE_OBJECT_MONSTER, cobra_points, ARRAYSIZE(cobra_points), 2, 5, 10 },
    { "HOLY GRENADE", YELLOW, MAZE_OBJECT_GRENADE, grenade_points, ARRAYSIZE(grenade_points), 0, 0, 20 },
    { "KEY", YELLOW, MAZE_OBJECT_KEY, key_points, ARRAYSIZE(key_points), 0, 0, 0 },
    { "SCARY ORC", GREEN, MAZE_OBJECT_MONSTER, orc_points, ARRAYSIZE(orc_points), 4, 15, 15 },
    { "PHANTASM", WHITE, MAZE_OBJECT_MONSTER, phantasm_points, ARRAYSIZE(phantasm_points), 6, 15, 4 },
    { "POTION", RED, MAZE_OBJECT_POTION, potion_points, ARRAYSIZE(potion_points), 0, 0, 30 },
    { "SHIELD", YELLOW, MAZE_OBJECT_ARMOR, shield_points, ARRAYSIZE(shield_points), 0, 0, 5 },
    { "SWORD", YELLOW, MAZE_OBJECT_WEAPON, sword_points, ARRAYSIZE(sword_points), 0, 0, 15 },
#define DOWN_LADDER 11
    { "LADDER", WHITE, MAZE_OBJECT_DOWN_LADDER, down_ladder_points, ARRAYSIZE(down_ladder_points), 0, 0, 0 },
#define UP_LADDER 12
    { "LADDER", WHITE, MAZE_OBJECT_UP_LADDER, up_ladder_points, ARRAYSIZE(down_ladder_points), 0, 0, 0 },
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

static void init_maze_objects(void)
{
    int i;

    if (maze_previous_level == -1) { /* game is just beginning */
        nmaze_objects = 0;
        memset(maze_object, 0, sizeof(maze_object));
        return;
    }
    /* zero out all objects not in player's possession */
    for (i = 0; i < MAX_MAZE_OBJECTS; i++)
        if (maze_object[i].x != 255)
            memset(&maze_object[i], 0, sizeof(maze_object[i]));
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
    player.weapon = 255;
    player.armor = 255;
    max_maze_stack_depth = 0;
    memset(maze, 0, sizeof(maze));
    memset(maze_visited, 0, sizeof(maze_visited));
    maze_stack_ptr = MAZE_STACK_EMPTY;
    maze_stack_push(player.x, player.y, player.direction);
    maze_program_state = MAZE_BUILD;
    maze_size = 0;
    init_maze_objects();
    combat_mode = 0;
    generation_iterations = 0;
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

static int object_is_portable(int i)
{
    switch(maze_object_template[maze_object[i].type].category) {
    case MAZE_OBJECT_WEAPON:
    case MAZE_OBJECT_KEY:
    case MAZE_OBJECT_POTION:
    case MAZE_OBJECT_TREASURE:
    case MAZE_OBJECT_ARMOR:
    case MAZE_OBJECT_SCROLL:
    case MAZE_OBJECT_GRENADE:
        return 1;
    default:
        return 0;
    }
}

static int something_here(int x, int y)
{
    int i;

    for (i = 0; i < nmaze_objects; i++)
        if (maze_object[i].x == x && maze_object[i].y == y)
            return 1;
    return 0;
}

static void add_ladder(int ladder_type)
{
    int i, x, y;

    do {
        x = xorshift(&xorshift_state) % XDIM;
        y = xorshift(&xorshift_state) % YDIM;
    } while (!is_passage(x, y) || something_here(x, y));

    if (nmaze_objects < MAX_MAZE_OBJECTS - 1) {
        i = nmaze_objects;
    } else {
        for (i = 0; i < MAX_MAZE_OBJECTS; i++) {
            if (maze_object[i].x != 255 &&
                maze_object[i].type != DOWN_LADDER && maze_object[i].type != UP_LADDER)
                break;
        }
    }
    if (i >= MAX_MAZE_OBJECTS) { /* Player somehow has all objects */
        /* now what? */
    }

    maze_object[i].x = x;
    maze_object[i].y = y;
    maze_object[i].type = ladder_type;

    if ((maze_player_initial_placement == MAZE_PLACE_PLAYER_BENEATH_UP_LADDER &&
        ladder_type == UP_LADDER) ||
        (maze_player_initial_placement == MAZE_PLACE_PLAYER_ABOVE_DOWN_LADDER &&
        ladder_type == DOWN_LADDER)) {
        player.x = x;
        player.y = y;
    }

    if (i >= nmaze_objects - 1)
        nmaze_objects = i + 1;
}

static void add_ladders(int level)
{
    if (level < NLEVELS - 1)
        add_ladder(DOWN_LADDER);
    add_ladder(UP_LADDER);
}

static void add_random_object(int x, int y)
{
    int otype, i;

    /* Find a free object */
    for (i = 0; i < MAX_MAZE_OBJECTS; i++)
       if (maze_object[i].x == 0)
          break;
    if (i >= MAX_MAZE_OBJECTS)
        return;

    maze_object[i].x = x;
    maze_object[i].y = y;
    otype = xorshift(&xorshift_state) % (nobject_types - 2); /* minus 2 to exclude ladders */
    maze_object[i].type = otype;
    switch(maze_object_template[maze_object[i].type].category) {
    case MAZE_OBJECT_MONSTER:
        maze_object[i].tsd.monster.hitpoints =
            maze_object_template[otype].hitpoints + (xorshift(&xorshift_state) % 5);
        maze_object[i].tsd.monster.speed =
            maze_object_template[maze_object[i].type].speed;
        break;
    case MAZE_OBJECT_POTION:
        maze_object[i].tsd.potion.type = (xorshift(&xorshift_state) % ARRAYSIZE(potion_type));
        break;
    case MAZE_OBJECT_ARMOR:
        maze_object[i].tsd.armor.type = (xorshift(&xorshift_state) % ARRAYSIZE(armor_type));
        break;
    case MAZE_OBJECT_TREASURE:
        maze_object[i].tsd.treasure.gp = (xorshift(&xorshift_state) % 40);
        break;
    case MAZE_OBJECT_WEAPON:
        maze_object[i].tsd.weapon.type = (xorshift(&xorshift_state) % ARRAYSIZE(weapon_type));
        break;
    case MAZE_OBJECT_SCROLL:
    case MAZE_OBJECT_GRENADE:
    default:
        break;
    }
    if (i > nmaze_objects - 1)
        nmaze_objects = i + 1;
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
        } else {
            add_ladders(maze_current_level);
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
#ifdef __linux
                printf("maze too small, starting over\n");
#endif
                maze_program_state = MAZE_LEVEL_INIT;
                maze_random_seed[maze_current_level] = xorshift(&xorshift_state);
            } else {
                add_ladders(maze_current_level);
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

static void draw_map()
{
    int x, y;

    FbClear();
    FbColor(GREEN);
    for (x = 0; x < XDIM; x++) {
        for (y = 0; y < YDIM; y++) {
            if (x == player.x && y == player.y) {
                FbColor(WHITE);
                FbLine(x * 3 - 2, y * 3 - 2, x * 3 + 2, y * 3 - 2);
                FbLine(x * 3 - 2, y * 3 - 1, x * 3 + 2, y * 3 - 1);
                FbLine(x * 3 - 2, y * 3 - 0, x * 3 + 2, y * 3 - 0);
                FbLine(x * 3 - 2, y * 3 + 1, x * 3 + 2, y * 3 + 1);
                FbLine(x * 3 - 2, y * 3 + 2, x * 3 + 2, y * 3 + 2);
                FbColor(GREEN);
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

static void draw_object(struct point drawing[], int npoints, int scale_index, int color, int x, int y)
{
    int i;
    int xcenter = x;
    int ycenter = y;
    int num;

    num = drawing_scale_numerator[scale_index];

    FbColor(color);
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
    FbVerticalLine(start + scale, start + scale, start + scale, SCREEN_YDIM - 1 - (start + scale));
    FbHorizontalLine(start, start + scale, start + scale, start + scale);
    FbHorizontalLine(start, SCREEN_YDIM - 1 - (start + scale), start + scale, SCREEN_YDIM - 1 - (start + scale));
}

static void draw_right_passage(int start, int scale)
{
    FbVerticalLine(SCREEN_XDIM - 1 - start, start, SCREEN_XDIM - 1 - start, SCREEN_YDIM - 1 - start);
    FbVerticalLine(SCREEN_XDIM - 1 - start - scale, start + scale, SCREEN_XDIM - 1 - start - scale, SCREEN_YDIM - 1 - start - scale);
    FbHorizontalLine(SCREEN_XDIM - 1 - (start + scale), start + scale, SCREEN_XDIM - 1 - start, start + scale);
    FbHorizontalLine(SCREEN_XDIM - 1 - (start + scale), SCREEN_YDIM - 1 - (start + scale), SCREEN_XDIM - 1 - start, SCREEN_YDIM - 1 - (start + scale));
}

static void draw_left_wall(int start, int scale)
{
    FbLine(start, start, start + scale, start + scale);
    FbLine(start, SCREEN_YDIM - 1 - start, start + scale, SCREEN_YDIM - 1 - (start + scale));
}

static void draw_right_wall(int start, int scale)
{
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

    maze_previous_level = maze_current_level;
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
static char *encounter_adjective = "";
static char *encounter_name = "";
static unsigned char encounter_object = 255;

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
                encounter_adjective = "";
                encounter_name = maze_object_template[maze_object[i].type].name;
                encounter_object = i;
                monster = 1;
                break;
            case MAZE_OBJECT_WEAPON:
                encounter_text = "you found a";
                encounter_adjective = weapon_type[maze_object[i].tsd.weapon.type].adjective;
                encounter_name = weapon_type[maze_object[i].tsd.weapon.type].name;
                break;
            case MAZE_OBJECT_KEY:
            case MAZE_OBJECT_TREASURE:
            case MAZE_OBJECT_SCROLL:
            case MAZE_OBJECT_GRENADE:
                if (!monster) {
                    encounter_text = "you found a";
                    encounter_adjective = "";
                    encounter_name = maze_object_template[maze_object[i].type].name;
                }
                break;
            case MAZE_OBJECT_ARMOR:
                if (!monster) {
                    encounter_text = "you found a";
                    encounter_adjective = armor_type[maze_object[i].tsd.armor.type].adjective;
                    encounter_name = armor_type[maze_object[i].tsd.armor.type].name;
                }
                break;
            case MAZE_OBJECT_POTION:
                if (!monster) {
                    encounter_text = "you found a";
                    encounter_adjective = potion_type[maze_object[i].tsd.potion.type].adjective;
                    encounter_name = "potion";
                }
                break;
            case MAZE_OBJECT_DOWN_LADDER:
                if (!monster) {
                    encounter_text = "a ladder";
                    encounter_adjective = "";
                    encounter_name = "leads down";
                }
                break;
            case MAZE_OBJECT_UP_LADDER:
                if (!monster) {
                    encounter_text = "a ladder";
                    encounter_adjective = "";
                    encounter_name = "leads up";
                }
                break;
            default:
                if (!monster) {
                    encounter_text = "you found something";
                    encounter_adjective = "";
                    encounter_name = maze_object_template[maze_object[i].type].name;
                }
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
    int takeable_object_count = 0;
    int droppable_object_count = 0;

    if (player.hitpoints == 0)
        return;
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
            case MAZE_OBJECT_WEAPON:
            case MAZE_OBJECT_KEY:
            case MAZE_OBJECT_POTION:
            case MAZE_OBJECT_TREASURE:
            case MAZE_OBJECT_ARMOR:
            case MAZE_OBJECT_SCROLL:
            case MAZE_OBJECT_GRENADE:
                 takeable_object_count++;
                 break;
            default:
                 break;
            }
        }
        if (maze_object[i].x == 255 && object_is_portable(i))
           droppable_object_count++;
        if (maze_object[i].x == newx && maze_object[i].y == newy &&
            maze_object_template[maze_object[i].type].category == MAZE_OBJECT_MONSTER)
            monster_present = 1;
    }
    maze_menu_add_item("NEVER MIND", MAZE_RENDER, 1);
    if (monster_present) {
        maze_menu_add_item("FIGHT MONSTER!", MAZE_STATE_FIGHT, 1);
        maze_menu_add_item("FLEE!", MAZE_STATE_FLEE, 1);
    }
    if (takeable_object_count > 0)
        maze_menu_add_item("TAKE ITEM", MAZE_CHOOSE_TAKE_OBJECT, takeable_object_count);
    if (droppable_object_count > 0)
        maze_menu_add_item("DROP OBJECT",  MAZE_CHOOSE_DROP_OBJECT, 1);
    maze_menu_add_item("VIEW MAP", MAZE_DRAW_MAP, 1);
    maze_menu_add_item("WIELD WEAPON", MAZE_CHOOSE_WEAPON, 1);
    maze_menu_add_item("DON ARMOR", MAZE_CHOOSE_ARMOR, 1);
    maze_menu_add_item("READ SCROLL", MAZE_RENDER, 1);
    maze_menu_add_item("QUAFF POTION", MAZE_CHOOSE_POTION, 1);
    maze_menu_add_item("EXIT GAME", MAZE_EXIT, 255);
    maze_menu.menu_active = 1;
    maze_program_state = MAZE_DRAW_MENU;
}

static void move_player_one_step(int direction)
{
    int newx, newy, dx, dy, dist, hp, str, damage;

    if (!combat_mode) {
       newx = player.x + xoff[direction];
       newy = player.y + yoff[direction];
       if (!out_of_bounds(newx, newy) && is_passage(newx, newy)) {
           if (!check_for_encounter(newx, newy)) {
               player.x = newx;
               player.y = newy;
           }
       }
   } else {

       newx = player.combatx + xoff[direction] * 4;
       newy = player.combaty + yoff[direction] * 4;
       if (newx < 10)
           newx = 10;
       if (newx > SCREEN_XDIM - 10)
           newx = SCREEN_XDIM - 10;
       if (newy < 10)
           newy = 10;
       if (newy > SCREEN_YDIM - 10)
           newy = SCREEN_YDIM - 10;
       player.combatx = newx;
       player.combaty = newy;
       dx = player.combatx - combatant.combatx;
       dy = player.combaty - combatant.combaty;
       dist = dx * dx + dy * dy;
       if (dist < 100) {
           str = xorshift(&xorshift_state) % 160;
           /* damage = xorshift(&xorshift_state) % maze_object_template[maze_object[player.weapon].type].damage; */
           if (player.weapon == 255) /* fists */
               damage = 1;
           else
               damage = xorshift(&xorshift_state) % weapon_type[maze_object[player.weapon].type].damage;
           combatant.combatx -= dx * (80 + str) / 100;
           combatant.combaty -= dy * (80 + str) / 100;
           if (combatant.combatx < 40)
              combatant.combatx += 40;
           if (combatant.combatx > SCREEN_XDIM - 40)
              combatant.combatx -= 40;
           if (combatant.combaty < 40)
              combatant.combaty += 40;
           if (combatant.combaty > SCREEN_XDIM - 40)
              combatant.combaty -= 40;
           hp = combatant.hitpoints - damage;
           if (hp < 0)
              hp = 0;
           combatant.hitpoints = hp;
           if (combatant.hitpoints == 0) {
               maze_program_state = MAZE_STATE_PLAYER_DEFEATS_MONSTER;
               combat_mode = 0;
               maze_object[encounter_object].x = 255; /* Move it off the board */
           }
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
#ifdef __linux__
    int kp;
#endif
    int base_direction;

    base_direction = combat_mode ? 0 : player.direction;
#ifdef __linux__

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
            move_player_one_step(base_direction);
        break;
    case 's':
        if (maze_menu.menu_active)
            maze_menu_change_current_selection(1);
        else
            move_player_one_step(normalize_direction(base_direction + 4));
        break;
    case 'a':
        if (combat_mode)
            move_player_one_step(6);
        else
           player.direction = left_dir(player.direction);
        break;
    case 'd':
        if (combat_mode)
            move_player_one_step(2);
        else
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
#else

    if (BUTTON_PRESSED_AND_CONSUME) {
        maze_button_pressed();
    } else if (TOP_TAP_AND_CONSUME) {
        if (maze_menu.menu_active)
            maze_menu_change_current_selection(-1);
        else
            move_player_one_step(base_direction);
    } else if (BOTTOM_TAP_AND_CONSUME) {
        if (maze_menu.menu_active)
            maze_menu_change_current_selection(1);
        else
            move_player_one_step(normalize_direction(base_direction + 4));
    } else if (LEFT_TAP_AND_CONSUME) {
        if (combat_mode)
            move_player_one_step(6);
        else
            player.direction = left_dir(player.direction);
    } else if (RIGHT_TAP_AND_CONSUME) {
        if (combat_mode)
            move_player_one_step(2);
        else
            player.direction = right_dir(player.direction);
    } else {
        return;
    }

#endif

    if (player.hitpoints == 0) {
        maze_program_state = MAZE_GAME_INIT;
    }

    if (maze_program_state == MAZE_PROCESS_COMMANDS) {
        if (maze_menu.menu_active)
            maze_program_state = MAZE_DRAW_MENU;
        else if (combat_mode)
            maze_program_state = MAZE_COMBAT_MONSTER_MOVE;
        else
            maze_program_state = MAZE_RENDER;
    }
}

static void draw_objects(void)
{
    int a, b, i, x[2], y[2], s, otype, npoints;
    struct point *drawing;
    int color;

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
    color = maze_object_template[otype].color;
    if (x[0] == x[1]) {
        if (maze_object[i].x == x[0] && maze_object[i].y >= y[a] && maze_object[i].y <= y[b]) {
            s = abs(maze_object[i].y - player.y);
            if (s >= maze_object_distance_limit)
                goto next_object;
            draw_object(drawing, npoints, s, color, SCREEN_XDIM / 2, SCREEN_YDIM / 2);
        }
    } else if (y[0] == y[1]) {
        if (maze_object[i].y == y[0] && maze_object[i].x >= x[a] && maze_object[i].x <= x[b]) {
            s = abs(maze_object[i].x - player.x);
            if (s >= maze_object_distance_limit)
                goto next_object;
            draw_object(drawing, npoints, s, color, SCREEN_XDIM / 2, SCREEN_YDIM / 2);
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

static void render_maze(int color)
{
    int x, y, ox, oy, left, right;
    const int steps = 7;
    int hit_back_wall = 0;

    if (maze_render_step == 0) {
        FbClear();
        maze_object_distance_limit = steps;
    }

    FbColor(color);

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
    FbColor(WHITE);
    FbMove(10, 90);
    FbWriteLine(encounter_text);
    FbMove(10, 100);
    if (strcmp(encounter_adjective, "") != 0) {
        FbWriteLine(encounter_adjective);
        FbMove(10, 110);
    }
    FbWriteLine(encounter_name);
}

static void maze_combat_monster_move(void)
{
    int dx, dy, dist, speed, str, damage, hp, protection;

    dx = player.combatx - combatant.combatx;
    dy = player.combaty - combatant.combaty;
    dist = dx * dx + dy * dy;
    speed = maze_object[encounter_object].tsd.monster.speed;

    if (dist > 100) {
        if (dx < 0)
            combatant.combatx -= speed;
        else if (dx > 0)
            combatant.combatx += speed;
        if (dy < 0)
            combatant.combaty -= speed;
        else if (dy > 0)
            combatant.combaty += speed;
    } else {
        str = xorshift(&xorshift_state) % 160;

        if (player.armor == 255)
            protection = 0;
        else
            protection = xorshift(&xorshift_state) % armor_type[maze_object[player.armor].tsd.armor.type].protection;

        damage = xorshift(&xorshift_state) % maze_object_template[maze_object[encounter_object].type].damage;
        damage = damage - protection;
        if (damage < 0)
            damage = 0;

        player.combatx += dx * (80 + str) / 100;
        player.combaty += dy * (80 + str) / 100;
        if (player.combatx < 40)
           player.combatx += 40;
        if (player.combatx > SCREEN_XDIM - 40)
           player.combatx -= 40;
        if (player.combaty < 40)
           player.combaty += 40;
        if (player.combaty > SCREEN_XDIM - 40)
           player.combaty -= 40;
        hp = player.hitpoints - damage;
        if (hp < 0)
           hp = 0;
        player.hitpoints = hp;
        if (player.hitpoints == 0) {
            maze_program_state = MAZE_STATE_PLAYER_DIED;
            return;
        }
    }
    maze_program_state = MAZE_RENDER_COMBAT;
}

static void maze_render_combat(void)
{
    int color, otype, npoints;
    struct point *drawing;

    FbClear();

    /* Draw the monster */
    otype = maze_object[encounter_object].type;
    color = maze_object_template[otype].color;
    drawing = maze_object_template[otype].drawing;
    npoints = maze_object_template[otype].npoints;
    draw_object(drawing, npoints, ARRAYSIZE(drawing_scale_numerator) - 1,
                color, combatant.combatx, combatant.combaty);

    /* Draw the player */
    draw_object(player_points, ARRAYSIZE(player_points), ARRAYSIZE(drawing_scale_numerator) - 1,
                WHITE, player.combatx, player.combaty);

    maze_program_state = MAZE_DRAW_STATS;
}

static void maze_player_defeats_monster(void)
{
    FbClear();
    FbColor(RED);
    FbMove(10, SCREEN_YDIM / 2 - 10);
    FbWriteLine("you kill the");
    FbMove(10, SCREEN_YDIM / 2);
    FbWriteLine(encounter_name);
    FbMove(10, SCREEN_YDIM / 2 + 10);
    if (player.weapon == 255) {
        FbWriteLine("with your fists");
    } else {
        FbWriteLine("with the");
        FbMove(10, SCREEN_YDIM / 2 + 20);
        FbWriteLine(weapon_type[maze_object[player.weapon].tsd.weapon.type].adjective);
        FbMove(10, SCREEN_YDIM / 2 + 30);
        FbWriteLine(weapon_type[maze_object[player.weapon].tsd.weapon.type].name);
    }
    encounter_text = "x";
    encounter_adjective = "";
    encounter_name = "x";
    combat_mode = 0;
    maze_program_state = MAZE_SCREEN_RENDER;
}

static void maze_player_died(void)
{
    FbClear();
    FbColor(RED);
    FbMove(10, SCREEN_YDIM - 20);
    FbWriteLine("YOU HAVE DIED");
    draw_object(bones_points, ARRAYSIZE(bones_points), 0, WHITE, SCREEN_XDIM / 2, SCREEN_YDIM / 2);
    encounter_text = "x";
    encounter_adjective = "";
    encounter_name = "x";
    combat_mode = 0;
    maze_program_state = MAZE_SCREEN_RENDER;
}

static void maze_choose_potion(void)
{
    int i;
    char name[20];

    maze_menu_clear();
    maze_menu.menu_active = 1;
    strcpy(maze_menu.title, "CHOOSE POTION");

    for (i = 0; i < nmaze_objects; i++) {
        if (maze_object_template[maze_object[i].type].category != MAZE_OBJECT_POTION)
            continue;
        if (maze_object[i].x == 255 ||
            (maze_object[i].x == player.x && maze_object[i].y == player.y)) {
            strcpy(name, potion_type[maze_object[i].tsd.potion.type].adjective);
            strcat(name, " potion");
            maze_menu_add_item(name, MAZE_QUAFF_POTION, i);
        }
    }
    maze_menu_add_item("NEVER MIND", MAZE_RENDER, 255);
    maze_program_state = MAZE_DRAW_MENU;
}

static void maze_quaff_potion(void)
{
    short hp, delta, object, ptype;
    char *feel;

    if (maze_menu.chosen_cookie == 255) { /* never mind */
        maze_program_state = MAZE_RENDER;
    }
    object = maze_menu.chosen_cookie;
    ptype = maze_object[object].tsd.potion.type;
    delta = potion_type[ptype].health_impact;
    maze_object[object].x = 254; /* off maze, but not in pocket, "use up" the potion */
    hp = player.hitpoints + delta;
    if (hp > 255)
        hp = 255;
    if (hp < 0)
        hp = 0;
    delta = hp - player.hitpoints;
    if (delta < 0)
       feel = "worse";
    else if (delta > 0)
       feel = "better";
    else
       feel = "nothing";
    player.hitpoints = hp;
    FbClear();
    FbMove(10, SCREEN_YDIM / 2);
    FbWriteLine("you feel");
    FbMove(10, SCREEN_YDIM / 2 + 10);
    FbWriteLine(feel);
    maze_program_state = MAZE_SCREEN_RENDER;
}

static void maze_choose_weapon(void)
{
    int i;
    char name[20];

    maze_menu_clear();
    maze_menu.menu_active = 1;
    strcpy(maze_menu.title, "WIELD WEAPON");

    for (i = 0; i < nmaze_objects; i++) {
        if (maze_object_template[maze_object[i].type].category != MAZE_OBJECT_WEAPON)
            continue;
        if (maze_object[i].x == 255 ||
            (maze_object[i].x == player.x && maze_object[i].y == player.y)) {
            strcpy(name, weapon_type[maze_object[i].tsd.weapon.type].adjective);
            strcat(name, " ");
            strcat(name, weapon_type[maze_object[i].tsd.weapon.type].name);
            maze_menu_add_item(name, MAZE_WIELD_WEAPON, i);
        }
    }
    maze_menu_add_item("NEVER MIND", MAZE_RENDER, 255);
    maze_program_state = MAZE_DRAW_MENU;
}

static void maze_wield_weapon(void)
{
    if (maze_menu.chosen_cookie == 255) { /* never mind */
        maze_program_state = MAZE_RENDER;
    }
    player.weapon = maze_menu.chosen_cookie;
    FbClear();
    FbMove(10, SCREEN_YDIM / 2);
    FbWriteLine("you wield the");
    FbMove(10, SCREEN_YDIM / 2 + 10);
    FbWriteLine(weapon_type[maze_object[player.weapon].tsd.weapon.type].adjective);
    FbMove(10, SCREEN_YDIM / 2 + 20);
    FbWriteLine(weapon_type[maze_object[player.weapon].tsd.weapon.type].name);
    maze_program_state = MAZE_SCREEN_RENDER;
}

static void maze_choose_armor(void)
{
    int i;
    char name[20];

    maze_menu_clear();
    maze_menu.menu_active = 1;
    strcpy(maze_menu.title, "DON ARMOR");

    for (i = 0; i < nmaze_objects; i++) {
        if (maze_object_template[maze_object[i].type].category != MAZE_OBJECT_ARMOR)
            continue;
        if (maze_object[i].x == 255 ||
            (maze_object[i].x == player.x && maze_object[i].y == player.y)) {
            strcpy(name, armor_type[maze_object[i].tsd.armor.type].adjective);
            strcat(name, " ");
            strcat(name, armor_type[maze_object[i].tsd.armor.type].name);
            maze_menu_add_item(name, MAZE_DON_ARMOR, i);
        }
    }
    maze_menu_add_item("NEVER MIND", MAZE_RENDER, 255);
    maze_program_state = MAZE_DRAW_MENU;
}

static void maze_don_armor(void)
{
    if (maze_menu.chosen_cookie == 255) { /* never mind */
        maze_program_state = MAZE_RENDER;
    }
    player.armor = maze_menu.chosen_cookie;
    FbClear();
    FbMove(10, SCREEN_YDIM / 2);
    FbWriteLine("you don the");
    FbMove(10, SCREEN_YDIM / 2 + 10);
    FbWriteLine(armor_type[maze_object[player.armor].tsd.armor.type].adjective);
    FbMove(10, SCREEN_YDIM / 2 + 20);
    FbWriteLine(armor_type[maze_object[player.armor].tsd.armor.type].name);
    maze_program_state = MAZE_SCREEN_RENDER;
}

static void maze_draw_stats(void)
{
    char gold_pieces[10], hitpoints[10];

    FbColor(WHITE);
    itoa(gold_pieces, player.gp, 10);
    itoa(hitpoints, player.hitpoints, 10);
    FbMove(2, 122);
    if (player.hitpoints < 50)
       FbColor(RED);
    else if (player.hitpoints < 100)
       FbColor(YELLOW);
    else
       FbColor(GREEN);
    FbWriteLine("HP:");
    FbMove(2 + 24, 122);
    FbWriteLine(hitpoints);
    FbColor(YELLOW);
    FbMove(55, 122);
    FbWriteLine("GP:");
    FbMove(55 + 24, 122);
    FbWriteLine(gold_pieces);

    maze_program_state = MAZE_SCREEN_RENDER;
}

static void init_seeds(void)
{
    int i;

    for (i = 0; i < NLEVELS; i++)
        maze_random_seed[i] = xorshift(&xorshift_state);
}

static void potions_init(void)
{
   int i;

   for (i = 0; i < ARRAYSIZE(potion_type); i++)
       potion_type[i].health_impact = (xorshift(&xorshift_state) % 50) - 15;
}

static void maze_game_init(void)
{
#ifdef __linux__
    struct timeval tv;

    gettimeofday(&tv, NULL);
    xorshift_state = tv.tv_usec;
#endif
    if (xorshift_state == 0)
        xorshift_state = 0xa5a5a5a5;

    init_seeds();
    player_init();
    potions_init();

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
    FbColor(WHITE);
    FbMove(8, 5);
    FbWriteLine(maze_menu.title);

    y = SCREEN_YDIM / 2 - 10 * (maze_menu.current_item - first_item);
    for (i = first_item; i <= last_item; i++) {
        if (i == maze_menu.current_item)
            FbColor(GREEN);
        else
            FbColor(WHITE);
        FbMove(10, y);
        FbWriteLine(maze_menu.item[i].text);
        y += 10;
    }

    FbColor(GREEN);
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

static void maze_begin_fight()
{
    combat_mode = 1;
    player.combatx = SCREEN_XDIM / 2;
    player.combaty = SCREEN_YDIM - 40;
    combatant.combatx = SCREEN_XDIM / 2;
    combatant.combaty = 50;
    combatant.hitpoints = maze_object[encounter_object].tsd.monster.hitpoints;
    maze_program_state = MAZE_RENDER_COMBAT;
}

static void maze_choose_take_or_drop_object(char *title, enum maze_program_state_t next_state)
{
    int i, limit;
    char name[20];

    maze_menu_clear();
    maze_menu.menu_active = 1;
    strcpy(maze_menu.title, title);

    limit = nmaze_objects;
    if (limit > 10)
        limit = 10;
    maze_menu_add_item("NEVER MIND", MAZE_RENDER, 255);
    for (i = 0; i < nmaze_objects; i++) {
        if ((next_state == MAZE_TAKE_OBJECT && maze_object[i].x == player.x && maze_object[i].y == player.y) ||
            (next_state == MAZE_DROP_OBJECT && maze_object[i].x == 255)) {
            switch(maze_object_template[maze_object[i].type].category) {
            case MAZE_OBJECT_WEAPON:
                strcpy(name, weapon_type[maze_object[i].tsd.weapon.type].adjective);
                strcat(name, " ");
                strcat(name, weapon_type[maze_object[i].tsd.weapon.type].name);
                break;
            case MAZE_OBJECT_KEY:
                strcpy(name, "key");
                break;
            case MAZE_OBJECT_POTION:
                strcpy(name, potion_type[maze_object[i].tsd.potion.type].adjective);
                strcat(name, " potion");
                maze_menu_add_item(name, MAZE_QUAFF_POTION, i);
                break;
            case MAZE_OBJECT_TREASURE:
                strcpy(name, "chest");
                break;
            case MAZE_OBJECT_ARMOR:
                strcpy(name, armor_type[maze_object[i].tsd.armor.type].adjective);
                strcat(name, " ");
                strcat(name, armor_type[maze_object[i].tsd.armor.type].name);
                break;
            case MAZE_OBJECT_SCROLL:
                strcpy(name, "scroll");
                break;
            case MAZE_OBJECT_GRENADE:
                strcpy(name, "grenade");
                break;
            default:
                continue;
            }
            maze_menu_add_item(name, next_state, i);
            limit--;
            if (limit == 0) /* Don't make the menu too big. */
                break;
        }
    }
    maze_program_state = MAZE_DRAW_MENU;
}

static void maze_choose_take_object(void)
{
    maze_choose_take_or_drop_object("TAKE OBJECT", MAZE_TAKE_OBJECT);
}

static void maze_choose_drop_object(void)
{
    maze_choose_take_or_drop_object("DROP OBJECT", MAZE_DROP_OBJECT);
}

static void maze_take_object(void)
{
    int i;

    i = maze_menu.chosen_cookie;
    maze_object[i].x = 255; /* Take object */
    maze_program_state = MAZE_RENDER;

    switch(maze_object_template[maze_object[i].type].category) {
    case MAZE_OBJECT_TREASURE:
        player.gp += maze_object[i].tsd.treasure.gp;
        maze_object[i].tsd.treasure.gp = 0; /* you can't get infinite gp by repeatedly dropping and taking treasure */
        break;
    default:
        break;
    }
}

static void maze_drop_object(void)
{
    int i;

    i = maze_menu.chosen_cookie;
    maze_object[i].x = player.x;
    maze_object[i].y = player.y;
    maze_program_state = MAZE_RENDER;
    if (player.weapon == i)
        player.weapon = 255;
    if (player.armor == i)
       player.armor = 255;
}

int maze_loop(void)
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
        render_maze(WHITE);
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
         maze_begin_fight();
         break;
    case MAZE_STATE_FLEE:
         maze_program_state = MAZE_RENDER;
         break;
    case MAZE_COMBAT_MONSTER_MOVE:
         maze_combat_monster_move();
         break;
    case MAZE_RENDER_COMBAT:
         maze_render_combat();
         break;
    case MAZE_STATE_PLAYER_DEFEATS_MONSTER:
         maze_player_defeats_monster();
         break;
    case MAZE_STATE_PLAYER_DIED:
         maze_player_died();
         break;
    case MAZE_CHOOSE_POTION:
         maze_choose_potion();
         break;
    case MAZE_QUAFF_POTION:
         maze_quaff_potion();
         break;
    case MAZE_CHOOSE_WEAPON:
         maze_choose_weapon();
         break;
    case MAZE_WIELD_WEAPON:
         maze_wield_weapon();
         break;
    case MAZE_CHOOSE_ARMOR:
         maze_choose_armor();
         break;
    case MAZE_DON_ARMOR:
         maze_don_armor();
         break;
    case MAZE_CHOOSE_TAKE_OBJECT:
         maze_choose_take_object();
         break;
    case MAZE_CHOOSE_DROP_OBJECT:
         maze_choose_drop_object();
         break;
    case MAZE_TAKE_OBJECT:
         maze_take_object();
         break;
    case MAZE_DROP_OBJECT:
         maze_drop_object();
         break;
    case MAZE_EXIT:
        maze_program_state = MAZE_GAME_INIT;
        returnToMenus();
        break;
    }
    return 0;
}

#ifdef __linux__
int main(int argc, char *argv[])
{
    do {
        if (maze_loop())
            return 0;
    } while (1);
    return 0;
}
#endif
