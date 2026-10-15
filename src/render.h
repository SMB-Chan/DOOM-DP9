#ifndef RENDER_H
#define RENDER_H

#include "doomdef.h"

/* framebuffer: 320x200, 8-bit palette indices */
extern byte *g_fb;

void render_init(void);		/* palette LUT, sin table, wall textures */

/* camera / draw-state set by game before rendering */
extern int vw_x, vw_y;			/* eye pos fixed */
extern int vw_z;				/* eye height fixed */
extern angle_t vw_angle;
extern int vw_sector_light;

void render_frame(void);		/* world view (walls, flats, sky, sprites) */

/* sprite ids resolved from wad */
enum {
	SPR_NONE = -1,
	SPR_PISF, SPR_PISG, SPR_PISGB,
	SPR_SHTF, SPR_SHTG, SPR_SHTGB,
	SPR_CHGG, SPR_MISG, SPR_PUNG,
	SPR_POSSA, SPR_POSSB, SPR_POSSH, SPR_POSSL,
	SPR_SPOSA, SPR_SPOSB, SPR_SPOSH, SPR_SPOSL,
	SPR_TROOA, SPR_TROOB, SPR_TROOM,
	SPR_SARGA, SPR_SARGB, SPR_SARGN,
	SPR_BAR1, SPR_BEXP,
	SPR_STIM, SPR_MEDIA, SPR_ARM1, SPR_ARM2,
	SPR_CLIP, SPR_SHEL, SPR_SHOT,
	SPR_BKEY, SPR_YKEY, SPR_RKEY,
	SPR_BON1, SPR_BON2,
	SPR_FACE_OK, SPR_FACE_DEAD, SPR_FACE_GOD,
	SPR_COUNT
};

/* sprite draw record filled by game each frame */
typedef struct {
	int x, y;			/* fixed world pos */
	int z;				/* bottom height fixed */
	int spr;			/* sprite id */
	int light;			/* 0..255 sector light */
	int flip;			/* mirror horizontally */
	int fullbright;
} sprdraw_t;
#define MAXSPRDRAW 160
extern sprdraw_t g_sprdraws[MAXSPRDRAW];
extern int g_numsprdraws;

/* shared tables (filled by render_init) */
extern int *g_sin_tbl;		/* 16.16 sine table, index = angle>>4 */
unsigned rnd_next(void);

/* common palette indices (set by render_init) */
extern byte g_colwhite, g_colred, g_colgold, g_colgray, g_colgreen,
	g_coldark, g_colblue, g_colbrick;

/* screen primitives (palette indices) */
int pal_nearest(int r, int g, int b);
void scr_fill(byte c);
void scr_rect(int x, int y, int w, int h, byte c);
void scr_text(int x, int y, const char *s, byte c);
void scr_text_big(int x, int y, const char *s, byte c, int scale);
void scr_text_center(int y, const char *s, byte c);
void draw_sprite_patch(int spr, int x, int y, int lightrow, int flip);
void scr_tint(byte c);		/* blend whole view toward palette color c */

/* game-facing HUD */
void draw_hud(int health, int armor, int ammo, int weapon, int keys,
			  int dead);
void draw_weapon(int weapon, int frame, int bobx, int boby);
void draw_message(const char *msg);

#endif
