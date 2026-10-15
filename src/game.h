#ifndef GAME_H
#define GAME_H

#include "doomdef.h"

void game_load(void);		/* parse map things -> entities, doors, player */
void game_start(void);		/* (re)start level state */
void game_update(void);		/* one 35Hz tic */
void game_render(void);		/* fills framebuffer (view + weapon + hud) */

/* input state, filled by platform each tic */
typedef struct {
	sbyte forward;		/* -1/0/1 */
	sbyte sidestep;		/* -1/0/1 */
	sbyte turn;			/* -1/0/1 (turn left positive, right negative) */
	byte fire, use, run;
	byte weapon_next;
	byte automap_toggle;
	byte god_toggle;
	byte idkfa;
	byte quit;			/* user wants out */
} input_t;
extern input_t g_input;

extern int g_state;			/* GS_* */
extern int g_gametic;
extern const char *g_message;
extern int g_message_tics;
extern int g_exit_result;	/* 1 = level complete */
extern int g_kills, g_kills_total, g_items, g_items_total;

void game_set_message(const char *m);

#endif
