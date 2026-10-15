/* DOOM EX-word game logic: player, monsters, combat, doors, items. */
#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "wad.h"
#include "render.h"
#include "game.h"

input_t g_input;
int g_state = GS_TITLE;
int g_gametic;
const char *g_message = 0;
int g_message_tics;
int g_exit_result;
int g_kills, g_kills_total, g_items, g_items_total;

/* ---------------- player ---------------- */

typedef struct {
	int x, y;				/* fixed */
	angle_t angle;
	int health, armor;
	int ammo_bullets, ammo_shells;
	int weapon;
	int keys;				/* bit0 blue, bit1 yellow, bit2 red */
	int fire_tics;
	int fire_frame;
	int bob_phase;
	int dead;
	int dead_tics;
	int pain_tics;
	byte automap;
	byte god;
} player_t;

static player_t P;
static int P_start_x, P_start_y;
static angle_t P_start_angle;

void game_set_message(const char *m)
{
	g_message = m;
	g_message_tics = 105;	/* 3 sec */
}

/* ---------------- things ---------------- */

typedef struct {
	byte kind;
	byte state;
	byte animtic;
	byte atkcool;
	byte alert;
	short hp;
	int x, y;
	angle_t angle;
	byte alive;
} mobj_t;

static mobj_t g_mobjs[MAXTHINGS];
static int g_nummobjs = 0;

enum {
	K_NONE = 0,
	K_ZOMBIE, K_SERGEANT, K_IMP, K_DEMON,
	K_BARREL,
	K_STIM, K_MEDKIT, K_ARM1, K_ARM2,
	K_CLIP, K_SHEL, K_SHOTGUN,
	K_BKEY, K_YKEY, K_RKEY,
	K_BON1, K_BON2,
	K_FIREBALL,
	K_EXPLOSION
};

/* DoomEd number -> entity */
static const struct {
	short type;
	byte kind;
	byte hp;
} thing_map[] = {
	{ 9, K_SERGEANT, 30 },
	{ 3001, K_IMP, 60 },
	{ 3002, K_DEMON, 150 },
	{ 3004, K_ZOMBIE, 20 },
	{ 2035, K_BARREL, 20 },
	{ 2011, K_STIM, 0 },
	{ 2012, K_MEDKIT, 0 },
	{ 2013, K_ARM1, 0 },
	{ 2019, K_ARM2, 0 },
	{ 2007, K_CLIP, 0 },
	{ 2008, K_SHEL, 0 },
	{ 2001, K_SHOTGUN, 0 },
	{ 5, K_BKEY, 0 },
	{ 40, K_BKEY, 0 },
	{ 6, K_YKEY, 0 },
	{ 39, K_YKEY, 0 },
	{ 13, K_RKEY, 0 },
	{ 38, K_RKEY, 0 },
	{ 2014, K_BON1, 0 },
	{ 2015, K_BON2, 0 },
	{ 0, K_NONE, 0 }
};

static door_t g_doors[MAXDOORS];
static int g_numdoors = 0;
static int g_ceiling_orig[MAXSECTORS];
static int g_floor_orig[MAXSECTORS];
static int g_lift_timer[MAXSECTORS];

/* line grid (our own blockmap) */
#define GRIDSHIFT 6			/* 64-unit cells */
#define GRIDSIZE 128
typedef struct {
	short count;
	short first;
} gridcell_t;
static short *g_gridlines;
static gridcell_t *g_grid;
static int g_grid_ox = 0, g_grid_oy = 0;

int side_of_pub(const node_t *nd, int x, int y)
{
	return (((nd->dx >> 16) * ((y - nd->y) >> 16)) -
			((nd->dy >> 16) * ((x - nd->x) >> 16))) > 0 ? 1 : 0;
}

/* ---------------- math helpers ---------------- */

static int isqrt32(unsigned int v)
{
	unsigned int res = 0;
	unsigned int bit = 1u << 30;
	while (bit > v)
		bit >>= 2;
	while (bit != 0) {
		if (v >= res + bit) {
			v -= res + bit;
			res = (res >> 1) + bit;
		} else {
			res >>= 1;
		}
		bit >>= 2;
	}
	return (int)res;
}

static int dist_units(int x1, int y1, int x2, int y2)
{
	int dx = (x1 - x2) >> 16, dy = (y1 - y2) >> 16;
	int d = dx * dx + dy * dy;
	if (d <= 0)
		return 0;
	return isqrt32((unsigned int)d);
}

/* All inputs in map units (NOT 16.16 fixed). Returns distance to segment in units. */
static int line_dist_units(int px, int py, int x1, int y1, int x2, int y2)
{
	int dx = x2 - x1, dy = y2 - y1;
	int wx = px - x1, wy = py - y1;
	int len2 = dx * dx + dy * dy;
	int proj, cx, cy, ex, ey, d2;
	if (len2 <= 0) {
		d2 = wx * wx + wy * wy;
	} else {
		proj = wx * dx + wy * dy;
		if (proj <= 0) {
			cx = x1; cy = y1;
		} else if (proj >= len2) {
			cx = x2; cy = y2;
		} else {
			int t8 = (proj << 8) / len2;
			cx = x1 + ((dx * t8) >> 8);
			cy = y1 + ((dy * t8) >> 8);
		}
		ex = px - cx;
		ey = py - cy;
		d2 = ex * ex + ey * ey;
	}
	if (d2 <= 0)
		return 0;
	return isqrt32((unsigned int)d2);
}

/* octant atan2 -> angle_t (0..65535), inputs in units */
static angle_t angle_to(int relx, int rely)
{
	int fx = relx, fy = rely;
	if (fx == 0 && fy == 0)
		return 0;
	if (fx >= 0 && fy >= 0)
		return (angle_t)((fy * 16384) / (fx + fy));
	if (fx < 0 && fy >= 0)
		return (angle_t)(16384 + (((-fx) * 16384) / ((-fx) + fy)));
	if (fx < 0 && fy < 0)
		return (angle_t)(32768 + (((-fy) * 16384) / ((-fx) + (-fy))));
	return (angle_t)(49152 + ((fx * 16384) / (fx + (-fy))));
}

/* ---------------- grid ---------------- */

static void grid_add_line(int i)
{
	int pad = 32 << FRACBITS;
	int x0 = (linedefs[i].bx0 - pad - g_grid_ox) >> (FRACBITS + GRIDSHIFT);
	int x1 = (linedefs[i].bx1 + pad - g_grid_ox) >> (FRACBITS + GRIDSHIFT);
	int y0 = (linedefs[i].by0 - pad - g_grid_oy) >> (FRACBITS + GRIDSHIFT);
	int y1 = (linedefs[i].by1 + pad - g_grid_oy) >> (FRACBITS + GRIDSHIFT);
	int gx, gy;
	if (x0 < 0) x0 = 0;
	if (y0 < 0) y0 = 0;
	if (x1 >= GRIDSIZE) x1 = GRIDSIZE - 1;
	if (y1 >= GRIDSIZE) y1 = GRIDSIZE - 1;
	for (gy = y0; gy <= y1; gy++)
		for (gx = x0; gx <= x1; gx++) {
			gridcell_t *c = &g_grid[gy * GRIDSIZE + gx];
			g_gridlines[c->first + c->count] = (short)i;
			c->count++;
		}
}

static void grid_build(void)
{
	int i, total = 0;
	int pad = 32 << FRACBITS;
	int minx = 0x7FFFFFFF, miny = 0x7FFFFFFF;
	for (i = 0; i < numvertexes; i++) {
		if (vertexes[i].x < minx) minx = vertexes[i].x;
		if (vertexes[i].y < miny) miny = vertexes[i].y;
	}
	g_grid_ox = minx - (128 << FRACBITS);
	g_grid_oy = miny - (128 << FRACBITS);

	if (!g_grid)
		g_grid = (gridcell_t *)malloc(sizeof(gridcell_t) * GRIDSIZE * GRIDSIZE);
	memset(g_grid, 0, sizeof(gridcell_t) * GRIDSIZE * GRIDSIZE);
	for (i = 0; i < numlinedefs; i++) {
		int x0 = (linedefs[i].bx0 - pad - g_grid_ox) >> (FRACBITS + GRIDSHIFT);
		int x1 = (linedefs[i].bx1 + pad - g_grid_ox) >> (FRACBITS + GRIDSHIFT);
		int y0 = (linedefs[i].by0 - pad - g_grid_oy) >> (FRACBITS + GRIDSHIFT);
		int y1 = (linedefs[i].by1 + pad - g_grid_oy) >> (FRACBITS + GRIDSHIFT);
		int gx, gy;
		if (x0 < 0) x0 = 0;
		if (y0 < 0) y0 = 0;
		if (x1 >= GRIDSIZE) x1 = GRIDSIZE - 1;
		if (y1 >= GRIDSIZE) y1 = GRIDSIZE - 1;
		for (gy = y0; gy <= y1; gy++)
			for (gx = x0; gx <= x1; gx++)
				g_grid[gy * GRIDSIZE + gx].count++;
	}
	for (i = 0; i < GRIDSIZE * GRIDSIZE; i++) {
		g_grid[i].first = (short)total;
		total += g_grid[i].count;
		g_grid[i].count = 0;
	}
	if (!g_gridlines)
		g_gridlines = (short *)malloc(sizeof(short) * (total > 0 ? total : 1));
	for (i = 0; i < numlinedefs; i++)
		grid_add_line(i);
}

static gridcell_t *cell_at(int x, int y)
{
	int gx = (x - g_grid_ox) >> (FRACBITS + GRIDSHIFT);
	int gy = (y - g_grid_oy) >> (FRACBITS + GRIDSHIFT);
	if (gx < 0 || gy < 0 || gx >= GRIDSIZE || gy >= GRIDSIZE)
		return 0;
	return &g_grid[gy * GRIDSIZE + gx];
}

/* ---------------- sector lookup ---------------- */

static int point_sector_bsp(int nodei, int x, int y)
{
	while (!(nodei & 0x8000u)) {
		const node_t *nd = &nodes[nodei];
		nodei = nd->children[side_of_pub(nd, x, y)];
	}
	{
		int ss = nodei & 0x7FFF;
		if (ss >= 0 && ss < numssectors && ssectors[ss].numsegs > 0) {
			const seg_t *sg = &segs[ssectors[ss].firstseg];
			const linedef_t *ld = &linedefs[sg->linedef];
			int sn = sg->dir ? ld->sidenum[1] : ld->sidenum[0];
			if (sn >= 0 && sn < numsidedefs)
				return sidedefs[sn].sector;
		}
	}
	return 0;
}

static int point_sector(int x, int y)
{
	if (numnodes > 0)
		return point_sector_bsp(numnodes - 1, x, y);
	return 0;
}

/* ---------------- blocking ---------------- */

static door_t *door_for_line(const linedef_t *ld)
{
	int d, sec = -1;
	if (ld->tag > 0) {
		int s;
		for (s = 0; s < numsectors; s++)
			if (sectors[s].tag == ld->tag) {
				sec = s;
				break;
			}
	} else if (ld->sidenum[1] >= 0 && ld->sidenum[1] < numsidedefs) {
		sec = sidedefs[ld->sidenum[1]].sector;
	} else if (ld->sidenum[0] >= 0 && ld->sidenum[0] < numsidedefs) {
		sec = sidedefs[ld->sidenum[0]].sector;
	}
	if (sec < 0)
		return 0;
	for (d = 0; d < g_numdoors; d++)
		if (g_doors[d].sector == sec)
			return &g_doors[d];
	return 0;
}

static int door_closed(const door_t *dr)
{
	return sectors[dr->sector].ceilz - sectors[dr->sector].floorz <
		   (50 << 16);
}

/* does line li block movement for an actor standing at height z
 * (feet, fixed) with the given height? */
static int line_blocks_move(int li, int z, int height)
{
	linedef_t *ld = &linedefs[li];
	int s0 = ld->sidenum[0], s1 = ld->sidenum[1];
	int fsec, bsec;
	const door_t *dr;
	if (s0 < 0 || s0 >= numsidedefs)
		return 1;
	dr = door_for_line(ld);
	if (dr && door_closed(dr))
		return 1;
	fsec = sidedefs[s0].sector;
	bsec = (s1 >= 0 && s1 < numsidedefs) ? sidedefs[s1].sector : -1;
	if (bsec < 0)
		return 1;
	if (bsec == fsec)
		return 0;
	{
		int ff = sectors[fsec].floorz, fc = sectors[fsec].ceilz;
		int bf = sectors[bsec].floorz, bc = sectors[bsec].ceilz;
		int lo = ff > bf ? ff : bf;
		int hi = fc < bc ? fc : bc;
		if (lo + (8 << 16) >= hi)		/* no passable gap */
			return 1;
		if (lo - z > (24 << 16))		/* step too high */
			return 1;
		if (z + height > hi)
			return 1;
	}
	return 0;
}

static int line_blocks_sight(int li)
{
	linedef_t *ld = &linedefs[li];
	int s0 = ld->sidenum[0], s1 = ld->sidenum[1];
	int fsec, bsec;
	const door_t *dr;
	if (s0 < 0 || s0 >= numsidedefs)
		return 1;
	dr = door_for_line(ld);
	if (dr && door_closed(dr))
		return 1;
	fsec = sidedefs[s0].sector;
	bsec = (s1 >= 0 && s1 < numsidedefs) ? sidedefs[s1].sector : -1;
	if (bsec < 0)
		return 1;
	if (bsec == fsec)
		return 0;
	{
		int ff = sectors[fsec].floorz, fc = sectors[fsec].ceilz;
		int bf = sectors[bsec].floorz, bc = sectors[bsec].ceilz;
		if (ff > bf ? ff + (8 << 16) >= (fc < bc ? fc : bc)
					: bf + (8 << 16) >= (fc < bc ? fc : bc))
			return 1;
	}
	return 0;
}

static int circle_hits_line(int px, int py, int r_units, int li)
{
	linedef_t *ld = &linedefs[li];
	if (px + (r_units << 16) < ld->bx0 || px - (r_units << 16) > ld->bx1 ||
		py + (r_units << 16) < ld->by0 || py - (r_units << 16) > ld->by1)
		return 0;
	return line_dist_units(px >> 16, py >> 16,
						  vertexes[ld->v1].x >> 16, vertexes[ld->v1].y >> 16,
						  vertexes[ld->v2].x >> 16, vertexes[ld->v2].y >> 16) <
		   r_units;
}

static void move_actor(int *x, int *y, int dx, int dy, int r_units, int z,
					   int height)
{
	int nx, ny, i;
	gridcell_t *c;
	nx = *x + dx;
	c = cell_at(nx, *y);
	if (c) {
		for (i = 0; i < c->count; i++) {
			int li = g_gridlines[c->first + i];
			if (circle_hits_line(nx, *y, r_units, li) &&
				line_blocks_move(li, z, height)) {
				nx = *x;
				break;
			}
		}
	}
	*x = nx;
	ny = *y + dy;
	c = cell_at(*x, ny);
	if (c) {
		for (i = 0; i < c->count; i++) {
			int li = g_gridlines[c->first + i];
			if (circle_hits_line(*x, ny, r_units, li) &&
				line_blocks_move(li, z, height)) {
				ny = *y;
				break;
			}
		}
	}
	*y = ny;
}

/* ray-march sight/hitscan; returns 1 if unobstructed up to maxdist units */
static int los_clear(int x, int y, angle_t angle, int maxdist)
{
	int dx = g_sin_tbl[((angle >> 4) + 1024) & 4095];
	int dy = g_sin_tbl[angle >> 4];
	int cx = x, cy = y;
	int steps = maxdist >> 3;	/* 8-unit steps */
	while (steps-- > 0) {
		gridcell_t *c;
		int i;
		cx += dx << 3;
		cy += dy << 3;
		c = cell_at(cx, cy);
		if (c) {
			for (i = 0; i < c->count; i++) {
				int li = g_gridlines[c->first + i];
				if (circle_hits_line(cx, cy, 8, li) &&
					line_blocks_sight(li))
					return 0;
			}
		}
	}
	return 1;
}

/* ---------------- doors ---------------- */

static void doors_update(void)
{
	int d;
	for (d = 0; d < g_numdoors; d++) {
		door_t *dr = &g_doors[d];
		sector_t *sec = &sectors[dr->sector];
		if (dr->state == 1) {
			sec->ceilz += (4 << 16);
			if (sec->ceilz >= dr->ceilpos) {
				sec->ceilz = dr->ceilpos;
				dr->state = 2;
				dr->timer = 140;
			}
		} else if (dr->state == 2) {
			if (dr->kind == 0 && --dr->timer <= 0) {
				/* don't close on top of the player */
				if ((dist_units(P.x, P.y, 0, 0), 1) &&
					point_sector(P.x, P.y) != dr->sector)
					dr->state = 3;
				else
					dr->timer = 35;
			}
		} else if (dr->state == 3) {
			sec->ceilz -= (4 << 16);
			if (sec->ceilz <= g_ceiling_orig[dr->sector]) {
				sec->ceilz = g_ceiling_orig[dr->sector];
				dr->state = 0;
			}
		}
	}
}

static void trigger_door(linedef_t *ld)
{
	door_t *dr = door_for_line(ld);
	if (!dr)
		return;
	if (dr->locked) {
		int have = (dr->locked == 2 && (P.keys & 1)) ||
				   (dr->locked == 3 && (P.keys & 2)) ||
				   (dr->locked == 6 && (P.keys & 4));
		if (!have) {
			game_set_message("YOU NEED A KEYCARD!");
			return;
		}
	}
	if (dr->state == 0 || dr->state == 3)
		dr->state = 1;
	else if (dr->state == 2 && dr->kind == 0)
		dr->state = 3;
}

/* ---------------- load / start ---------------- */

void game_load(void)
{
	int i;
	g_numdoors = 0;
	g_nummobjs = 0;
	g_kills = g_kills_total = g_items = g_items_total = 0;
	for (i = 0; i < numsectors && i < MAXSECTORS; i++) {
		g_ceiling_orig[i] = sectors[i].ceilz;
		g_floor_orig[i] = sectors[i].floorz;
		g_lift_timer[i] = 0;
	}

	for (i = 0; i < numlinedefs && g_numdoors < MAXDOORS; i++) {
		linedef_t *ld = &linedefs[i];
		int sp = ld->special;
		int isdoor = (sp == 1 || sp == 26 || sp == 27 || sp == 28 ||
					  sp == 31 || sp == 2 || sp == 103 || sp == 32 ||
					  sp == 33 || sp == 34 || sp == 50 || sp == 51);
		if (isdoor) {
			int dsec = -1, k, lowceil = 0x7FFFFFFF;
			if (ld->tag > 0) {
				for (k = 0; k < numsectors; k++)
					if (sectors[k].tag == ld->tag) {
						dsec = k;
						break;
					}
			} else if (ld->sidenum[1] >= 0 && ld->sidenum[1] < numsidedefs) {
				dsec = sidedefs[ld->sidenum[1]].sector;
			}
			if (dsec >= 0 && dsec < numsectors) {
				int exists = 0;
				for (k = 0; k < g_numdoors; k++)
					if (g_doors[k].sector == dsec) {
						exists = 1;
						break;
					}
				if (!exists) {
					door_t *dr = &g_doors[g_numdoors];
					dr->sector = (short)dsec;
					dr->state = 0;
					dr->kind = (sp == 31 || sp == 2 || sp == 103 || sp == 32 || sp == 33 || sp == 34 || sp == 50 || sp == 51) ? 1 : 0;
					dr->locked = (sp == 26 || sp == 32) ? 2
								 : (sp == 27 || sp == 34) ? 3
								 : (sp == 28 || sp == 33) ? 6
														  : 0;
					dr->timer = 0;
					/* Find lowest adjacent ceiling surrounding door sector */
					for (k = 0; k < numlinedefs; k++) {
						int s0 = linedefs[k].sidenum[0], s1 = linedefs[k].sidenum[1];
						int sec0 = (s0 >= 0 && s0 < numsidedefs) ? sidedefs[s0].sector : -1;
						int sec1 = (s1 >= 0 && s1 < numsidedefs) ? sidedefs[s1].sector : -1;
						if (sec0 == dsec && sec1 >= 0 && sec1 != dsec) {
							if (g_ceiling_orig[sec1] < lowceil)
								lowceil = g_ceiling_orig[sec1];
						} else if (sec1 == dsec && sec0 >= 0 && sec0 != dsec) {
							if (g_ceiling_orig[sec0] < lowceil)
								lowceil = g_ceiling_orig[sec0];
						}
					}
					if (lowceil == 0x7FFFFFFF)
						lowceil = sectors[dsec].floorz + (72 << 16);
					dr->ceilpos = lowceil - (4 << 16);
					g_numdoors++;
				}
			}
		}
	}

	for (i = 0; i < numthings && g_nummobjs < MAXTHINGS; i++) {
		rawthing_t *rt = &mapthings[i];
		int t, ti = -1;
		if (rt->type >= 1 && rt->type <= 4) {
			if (rt->type == 1) {	/* player 1 start */
				P_start_x = rt->x << FRACBITS;
				P_start_y = rt->y << FRACBITS;
				P_start_angle = (angle_t)(((rt->angle % 360) * 65536) / 360);
				P.x = P_start_x;
				P.y = P_start_y;
				P.angle = P_start_angle;
			}
			continue;
		}
		for (t = 0; thing_map[t].type; t++)
			if (thing_map[t].type == rt->type) {
				ti = t;
				break;
			}
		if (ti < 0)
			continue;
		{
			mobj_t *m = &g_mobjs[g_nummobjs];
			m->kind = thing_map[ti].kind;
			m->hp = thing_map[ti].hp;
			m->x = rt->x << FRACBITS;
			m->y = rt->y << FRACBITS;
			m->angle = (angle_t)(((rt->angle % 360) * 65536) / 360);
			m->state = TH_WALK;
			m->alive = 1;
			m->animtic = (byte)(i & 7);
			m->atkcool = 0;
			m->alert = 0;
			if (m->hp > 0 && m->kind != K_BARREL)
				g_kills_total++;
			else if (m->hp == 0)
				g_items_total++;
			g_nummobjs++;
		}
	}
	grid_build();
}

void game_start(void)
{
	int d, i;
	P.x = P_start_x;
	P.y = P_start_y;
	P.angle = P_start_angle;
	P.health = 100;
	P.armor = 0;
	P.ammo_bullets = 50;
	P.ammo_shells = 0;
	P.weapon = WP_PISTOL;
	P.keys = 0;
	P.fire_tics = 0;
	P.fire_frame = 0;
	P.bob_phase = 0;
	P.dead = 0;
	P.dead_tics = 0;
	P.pain_tics = 0;
	P.automap = 0;
	g_kills = 0;
	g_items = 0;
	g_exit_result = 0;
	g_message = "E1M1: HANGAR - FIGHT!";
	g_message_tics = 105;
	g_state = GS_PLAY;
	for (i = 0; i < numsectors && i < MAXSECTORS; i++) {
		sectors[i].ceilz = g_ceiling_orig[i];
		sectors[i].floorz = g_floor_orig[i];
		g_lift_timer[i] = 0;
	}
	for (d = 0; d < g_numdoors; d++) {
		g_doors[d].state = 0;
		g_doors[d].timer = 0;
		sectors[g_doors[d].sector].ceilz = g_ceiling_orig[g_doors[d].sector];
	}
	for (i = 0; i < g_nummobjs; i++) {
		mobj_t *m = &g_mobjs[i];
		int t;
		if (m->kind == K_FIREBALL || m->kind == K_EXPLOSION) {
			m->alive = 0;
			continue;
		}
		for (t = 0; thing_map[t].type; t++)
			if (thing_map[t].kind == m->kind)
				break;
		m->hp = thing_map[t].hp;
		m->state = TH_WALK;
		m->alive = 1;
		m->alert = 0;
		m->atkcool = 0;
	}
}

/* ---------------- combat ---------------- */

static void damage_player(int dmg)
{
	if (P.dead || P.god)
		return;
	if (P.armor > 0) {
		int saved = dmg / 3;
		if (saved > P.armor)
			saved = P.armor;
		P.armor -= saved;
		dmg -= saved;
	}
	P.health -= dmg;
	P.pain_tics = 12;
	if (P.health <= 0) {
		P.health = 0;
		P.dead = 1;
		P.dead_tics = 0;
	}
}

static void explode_at(int x, int y, int damage, int range)
{
	int i;
	for (i = 0; i < g_nummobjs; i++) {
		mobj_t *m = &g_mobjs[i];
		int d;
		if (!m->alive || m->hp <= 0)
			continue;
		d = dist_units(x, y, m->x, m->y);
		if (d < range) {
			int dmg = damage - (d * damage / range);
			if (m->kind == K_BARREL) {
				if (d > 8)
					m->hp -= dmg;
			} else {
				m->hp -= dmg;
				if (m->hp <= 0)
					m->alive = 0, g_kills++;
			}
		}
	}
	{
		int pd = dist_units(x, y, P.x, P.y);
		if (pd < range && !P.dead)
			damage_player(damage - (pd * damage / range));
	}
}

static mobj_t *spawn_fx(byte kind, int x, int y, angle_t angle)
{
	mobj_t *m = 0;
	int i;
	for (i = 0; i < g_nummobjs; i++)
		if (!g_mobjs[i].alive &&
			(g_mobjs[i].kind == K_FIREBALL || g_mobjs[i].kind == K_EXPLOSION)) {
			m = &g_mobjs[i];
			break;
		}
	if (!m && g_nummobjs < MAXTHINGS)
		m = &g_mobjs[g_nummobjs++];
	if (!m)
		m = &g_mobjs[0];
	m->kind = kind;
	m->state = TH_WALK;
	m->alive = 1;
	m->x = x;
	m->y = y;
	m->angle = angle;
	m->hp = 0;
	m->animtic = 0;
	m->alert = 0;
	m->atkcool = 0;
	return m;
}

static void player_fire(void)
{
	mobj_t *best = 0;
	int bestd = 0, i, dmg;
	int fc8 = g_sin_tbl[((P.angle >> 4) + 1024) & 4095] >> 8;
	int fs8 = g_sin_tbl[P.angle >> 4] >> 8;

	if (P.weapon == WP_FIST) {
		for (i = 0; i < g_nummobjs; i++) {
			mobj_t *m = &g_mobjs[i];
			int d, relx, rely, fwd, side;
			if (!m->alive || m->hp <= 0)
				continue;
			d = dist_units(P.x, P.y, m->x, m->y);
			relx = (m->x - P.x) >> 16;
			rely = (m->y - P.y) >> 16;
			fwd = (relx * fc8 + rely * fs8) >> 8;
			side = (relx * fs8 - rely * fc8) >> 8;
			if (fwd > 0 && d < 96 &&
				(side < 0 ? -side : side) < 48 + (d >> 1))
				if (!best || d < bestd) {
					best = m;
					bestd = d;
				}
		}
		if (best) {
			best->hp -= 4 * ((rnd_next() % 8) + 2);
			if (best->hp <= 0) {
				if (best->kind != K_BARREL) {
					best->alive = 0;
					g_kills++;
				}
			} else {
				best->alert = 1;
				best->atkcool = 12;
			}
		}
		P.fire_tics = 12;
		P.fire_frame = 1;
		return;
	}
	if (P.weapon == WP_PISTOL && P.ammo_bullets <= 0) {
		P.weapon = WP_FIST;
		game_set_message("OUT OF AMMO");
		return;
	}
	if (P.weapon == WP_SHOTGUN && P.ammo_shells <= 0) {
		P.weapon = WP_PISTOL;
		game_set_message("OUT OF SHELLS");
		return;
	}
	if (P.weapon == WP_PISTOL)
		P.ammo_bullets--;
	else
		P.ammo_shells--;

	for (i = 0; i < g_nummobjs; i++) {
		mobj_t *m = &g_mobjs[i];
		int d, relx, rely, fwd, side, side_lim;
		if (!m->alive || m->hp <= 0)
			continue;
		d = dist_units(P.x, P.y, m->x, m->y);
		relx = (m->x - P.x) >> 16;
		rely = (m->y - P.y) >> 16;
		fwd = (relx * fc8 + rely * fs8) >> 8;
		side = (relx * fs8 - rely * fc8) >> 8;
		side_lim = 24 + (d >> 2);
		if (fwd > 0 && d < 1600 && (side < 0 ? -side : side) < side_lim) {
			angle_t aim = angle_to(relx, rely);
			if (los_clear(P.x, P.y, aim, d)) {
				if (!best || d < bestd) {
					best = m;
					bestd = d;
				}
			}
		}
	}
	dmg = 5 * ((rnd_next() % 3) + 2);	/* 10..20 per pistol shot */
	if (P.weapon == WP_SHOTGUN)
		dmg *= 4;						/* 40..80 per shotgun blast */
	if (best) {
		best->hp -= dmg;
		best->alert = 1;
		best->atkcool = 14;				/* pain stun */
		if (best->hp <= 0) {
			if (best->kind == K_BARREL) {
				/* barrels_update() will explode it this tic */
			} else {
				best->alive = 0;
				g_kills++;
				if (best->kind == K_SERGEANT) {
					if (P.weapon < WP_SHOTGUN) {
						P.weapon = WP_SHOTGUN;
						game_set_message("GOT SHOTGUN FROM SERGEANT!");
					}
					if (P.ammo_shells < 50)
						P.ammo_shells += 4;
				} else if (best->kind == K_ZOMBIE) {
					if (P.ammo_bullets < 200)
						P.ammo_bullets += 4;
				}
			}
		}
	}
	for (i = 0; i < g_nummobjs; i++)
		if (g_mobjs[i].hp > 0 && g_mobjs[i].kind != K_BARREL && g_mobjs[i].alive &&
			dist_units(P.x, P.y, g_mobjs[i].x, g_mobjs[i].y) < 450) {
			angle_t to_p = angle_to((P.x - g_mobjs[i].x) >> 16, (P.y - g_mobjs[i].y) >> 16);
			if (los_clear(g_mobjs[i].x, g_mobjs[i].y, to_p, dist_units(P.x, P.y, g_mobjs[i].x, g_mobjs[i].y)))
				g_mobjs[i].alert = 1;
		}
	P.fire_tics = (P.weapon == WP_SHOTGUN) ? 20 : 10;
	P.fire_frame = 1;
}

/* ---------------- use ---------------- */

static void player_use(void)
{
	int i;
	int tx = P.x + (g_sin_tbl[((P.angle >> 4) + 1024) & 4095] << 5);
	int ty = P.y + (g_sin_tbl[P.angle >> 4] << 5);
	gridcell_t *c = cell_at(tx, ty);
	linedef_t *bl = 0;
	int bestd = 0;
	if (!c) {
		game_set_message("NOTHING TO USE HERE");
		return;
	}
	for (i = 0; i < c->count; i++) {
		int li = g_gridlines[c->first + i];
		linedef_t *ld = &linedefs[li];
		int sp = ld->special;
		int d = line_dist_units(P.x >> 16, P.y >> 16,
								vertexes[ld->v1].x >> 16,
								vertexes[ld->v1].y >> 16,
								vertexes[ld->v2].x >> 16,
								vertexes[ld->v2].y >> 16);
		if (d < 72 &&
			(sp == 1 || sp == 2 || sp == 11 || sp == 26 || sp == 27 ||
			 sp == 28 || sp == 31 || sp == 32 || sp == 33 || sp == 103 ||
			 sp == 50 || sp == 51)) {
			if (!bl || d < bestd) {
				bl = ld;
				bestd = d;
			}
		}
	}
	if (!bl) {
		game_set_message("NOTHING TO USE HERE");
		return;
	}
	if (bl->special == 11 || bl->special == 51) {
		g_exit_result = 1;
		return;
	}
	trigger_door(bl);
}

/* ---------------- pickups ---------------- */

static void pickup_item(mobj_t *m)
{
	int taken = 0;
	switch (m->kind) {
	case K_STIM:
		if (P.health < 100) {
			P.health += 10;
			taken = 1;
			game_set_message("PICKED UP A STIMPACK.");
		}
		break;
	case K_MEDKIT:
		if (P.health < 100) {
			P.health += 25;
			taken = 1;
			game_set_message("PICKED UP A MEDIKIT.");
		}
		break;
	case K_BON1:
		if (P.health < 200) {
			P.health += 1;
			taken = 1;
			game_set_message("PICKED UP A HEALTH BONUS.");
		}
		break;
	case K_BON2:
		if (P.armor < 200) {
			P.armor += 2;
			taken = 1;
			game_set_message("PICKED UP AN ARMOR BONUS.");
		}
		break;
	case K_ARM1:
		if (P.armor < 100) {
			P.armor = 100;
			taken = 1;
			game_set_message("PICKED UP THE ARMOR.");
		}
		break;
	case K_ARM2:
		if (P.armor < 200) {
			P.armor = 200;
			taken = 1;
			game_set_message("PICKED UP THE MEGAARMOR!");
		}
		break;
	case K_CLIP:
		if (P.ammo_bullets < 200) {
			P.ammo_bullets += 10;
			taken = 1;
			game_set_message("PICKED UP A CLIP.");
		}
		break;
	case K_SHEL:
		if (P.ammo_shells < 50) {
			P.ammo_shells += 4;
			taken = 1;
			game_set_message("PICKED UP SHOTGUN SHELLS.");
		}
		break;
	case K_SHOTGUN:
		if (P.weapon < WP_SHOTGUN)
			P.weapon = WP_SHOTGUN;
		P.ammo_shells += 8;
		taken = 1;
		game_set_message("YOU GOT THE SHOTGUN!");
		break;
	case K_BKEY:
		P.keys |= 1;
		taken = 1;
		game_set_message("PICKED UP A BLUE KEYCARD.");
		break;
	case K_YKEY:
		P.keys |= 2;
		taken = 1;
		game_set_message("PICKED UP A YELLOW KEYCARD.");
		break;
	case K_RKEY:
		P.keys |= 4;
		taken = 1;
		game_set_message("PICKED UP A RED KEYCARD.");
		break;
	}
	if (taken) {
		m->alive = 0;
		g_items++;
	}
}

/* ---------------- monsters ---------------- */

static void monster_think(mobj_t *m)
{
	int d;
	if (m->kind == K_FIREBALL) {
		int dx = g_sin_tbl[((m->angle >> 4) + 1024) & 4095];
		int dy = g_sin_tbl[m->angle >> 4];
		int nx = m->x + (dx << 4), ny = m->y + (dy << 4);
		gridcell_t *c = cell_at(nx, ny);
		int i, blocked = 0;
		if (c) {
			for (i = 0; i < c->count; i++)
				if (circle_hits_line(nx, ny, 6, g_gridlines[c->first + i]) &&
					line_blocks_sight(g_gridlines[c->first + i])) {
					blocked = 1;
					break;
				}
		} else
			blocked = 1;
		m->x = nx;
		m->y = ny;
		if (!blocked && !P.dead &&
			dist_units(m->x, m->y, P.x, P.y) < 28) {
			damage_player((rnd_next() % 15) + 8);
			blocked = 2;
		}
		if (blocked) {
			spawn_fx(K_EXPLOSION, m->x, m->y, 0);
			m->alive = 0;
		}
		return;
	}
	if (m->kind == K_EXPLOSION) {
		if (++m->animtic > 8)
			m->alive = 0;
		return;
	}
	if (!m->alive || m->hp <= 0 || m->kind == K_BARREL)
		return;

	d = dist_units(P.x, P.y, m->x, m->y);
	if (!m->alert) {
		if (d < 480 && ((g_gametic + m->animtic) & 7) == 0) {
			angle_t to_p = angle_to((P.x - m->x) >> 16, (P.y - m->y) >> 16);
			if (los_clear(m->x, m->y, to_p, d))
				m->alert = 1;
		}
		return;
	}

	/* face the player periodically */
	if ((g_gametic & 7) == (m->animtic & 7)) {
		int relx = (P.x - m->x) >> 16, rely = (P.y - m->y) >> 16;
		m->angle = angle_to(relx, rely);
	}

	if (m->atkcool)
		m->atkcool--;
	else if (d < (m->kind == K_DEMON ? 90 : 900) &&
			 los_clear(m->x, m->y, m->angle, d)) {
		m->atkcool = (m->kind == K_IMP) ? 60
					   : (m->kind == K_DEMON) ? 45
					   : (m->kind == K_SERGEANT) ? 70
												 : 55;
		if (m->kind == K_IMP) {
			spawn_fx(K_FIREBALL, m->x + (g_sin_tbl[((m->angle >> 4) + 1024) & 4095] << 4),
					 m->y + (g_sin_tbl[m->angle >> 4] << 4), m->angle);
		} else if (m->kind == K_DEMON) {
			damage_player((rnd_next() % 12) + 6);
		} else if (m->kind == K_SERGEANT) {
			int hits = 3;
			while (hits--)
				if ((rnd_next() % 3) != 0)
					damage_player((rnd_next() % 5) + 3);
		} else {
			if ((rnd_next() % 3) != 0)
				damage_player((rnd_next() % 12) + 3);
		}
		return;
	}

	/* chase */
	{
		int spd = (m->kind == K_DEMON) ? 10 : (m->kind == K_IMP) ? 6 : 5;
		int dx = g_sin_tbl[((m->angle >> 4) + 1024) & 4095] * spd;
		int dy = g_sin_tbl[m->angle >> 4] * spd;
		int ox = m->x, oy = m->y;
		int msec = point_sector(m->x, m->y);
		move_actor(&m->x, &m->y, dx, dy, 16, sectors[msec].floorz, 56 << 16);
		if (m->x == ox && m->y == oy) {
			if ((g_gametic + m->animtic) & 15)
				;
			else
				m->angle += 8192;
		}
		m->animtic++;
	}
}

static void barrels_update(void)
{
	int i;
	for (i = 0; i < g_nummobjs; i++) {
		mobj_t *m = &g_mobjs[i];
		if (m->alive && m->kind == K_BARREL && m->hp <= 0) {
			m->alive = 0;
			spawn_fx(K_EXPLOSION, m->x, m->y, 0);
			explode_at(m->x, m->y, 96, 128);
		}
	}
}

/* ---------------- main update ---------------- */

static void trigger_walk_special(int sp, int tag)
{
	int s;
	if (sp == 36 && tag > 0) {
		/* W1 Lower Floor to 8 above highest adjacent floor (E1M1 courtyard secret!) */
		for (s = 0; s < numsectors; s++)
			if (sectors[s].tag == tag && sectors[s].floorz > (-16 << 16)) {
				sectors[s].floorz = -24 << 16;
				game_set_message("SECRET WALL LOWERED!");
			}
	} else if (sp == 88 && tag > 0) {
		/* WR Lift Lower Wait Raise (E1M1 zig-zag room lift!) */
		for (s = 0; s < numsectors && s < MAXSECTORS; s++)
			if (sectors[s].tag == tag && g_lift_timer[s] == 0) {
				sectors[s].floorz = -24 << 16;
				g_lift_timer[s] = 140;
				game_set_message("LIFT ACTIVATED!");
			}
	}
}

static void lifts_update(void)
{
	int s;
	for (s = 0; s < numsectors && s < MAXSECTORS; s++) {
		if (g_lift_timer[s] > 0) {
			g_lift_timer[s]--;
			if (g_lift_timer[s] == 0)
				sectors[s].floorz = g_floor_orig[s];
		}
	}
}

void game_update(void)
{
	int i;
	g_gametic++;
	if (g_message_tics > 0)
		g_message_tics--;
	if (g_state != GS_PLAY)
		return;

	if (g_input.automap_toggle) {
		g_input.automap_toggle = 0;
		P.automap ^= 1;
	}
	if (g_input.god_toggle) {
		g_input.god_toggle = 0;
		P.god ^= 1;
		if (P.god) {
			P.health = 100;
			game_set_message("DEGREELESSNESS MODE ON (IDDQD)");
		} else {
			game_set_message("DEGREELESSNESS MODE OFF");
		}
	}
	if (g_input.idkfa) {
		g_input.idkfa = 0;
		P.armor = 200;
		P.ammo_bullets = 200;
		P.ammo_shells = 50;
		P.weapon = WP_SHOTGUN;
		P.keys = 7;
		game_set_message("VERY HAPPY AMMO ADDED! (IDKFA)");
	}

	if (P.pain_tics)
		P.pain_tics--;

	if (P.dead) {
		P.dead_tics++;
		if (P.dead_tics > 60 && (g_input.fire || g_input.use)) {
			g_input.fire = 0;
			g_input.use = 0;
			game_start();
			return;
		}
	} else {
		int fc = g_sin_tbl[((P.angle >> 4) + 1024) & 4095];
		int fs = g_sin_tbl[P.angle >> 4];
		int spd = g_input.run ? 14 : 9;
		int dx = 0, dy = 0;
		if (g_input.turn)
			P.angle = (angle_t)(P.angle +
								g_input.turn * (g_input.run ? 2400 : 1500));
		if (g_input.forward) {
			int f = g_input.forward * spd;
			dx += fc * f;
			dy += fs * f;
		}
		if (g_input.sidestep) {
			int s = g_input.sidestep * spd;
			dx += fs * s;
			dy -= fc * s;
		}
		if (dx || dy) {
			int sec = point_sector(P.x, P.y);
			gridcell_t *gc;
			move_actor(&P.x, &P.y, dx, dy, PLAYERRADIUS,
					   sectors[sec].floorz, 56 << 16);
			P.bob_phase += g_input.run ? 3 : 2;
			/* Check walk-over linedefs in current cell */
			gc = cell_at(P.x, P.y);
			if (gc) {
				int k;
				for (k = 0; k < gc->count; k++) {
					linedef_t *ld = &linedefs[g_gridlines[gc->first + k]];
					if ((ld->special == 36 || ld->special == 88 || ld->special == 2) &&
						circle_hits_line(P.x, P.y, 24, g_gridlines[gc->first + k])) {
						if (ld->special == 2)
							trigger_door(ld);
						else
							trigger_walk_special(ld->special, ld->tag);
					}
				}
			}
		}
		if (P.fire_tics > 0) {
			P.fire_tics--;
			if (P.fire_tics == ((P.weapon == WP_SHOTGUN) ? 18 : 6))
				P.fire_frame = 2;
			if (P.fire_tics == 0)
				P.fire_frame = 0;
		}
		if (g_input.weapon_next) {
			g_input.weapon_next = 0;
			if (P.weapon == WP_FIST && P.ammo_bullets > 0)
				P.weapon = WP_PISTOL;
			else if (P.weapon == WP_PISTOL && P.ammo_shells > 0)
				P.weapon = WP_SHOTGUN;
			else
				P.weapon = WP_FIST;
		}
		if (g_input.fire && P.fire_tics == 0)
			player_fire();
		if (g_input.use) {
			g_input.use = 0;
			player_use();
		}
	}

	doors_update();
	lifts_update();
	barrels_update();
	for (i = 0; i < g_nummobjs; i++)
		monster_think(&g_mobjs[i]);

	if (!P.dead) {
		for (i = 0; i < g_nummobjs; i++) {
			mobj_t *m = &g_mobjs[i];
			if (m->alive && m->hp == 0 &&
				dist_units(P.x, P.y, m->x, m->y) < 28)
				pickup_item(m);
		}
	}
}

/* ---------------- rendering glue ---------------- */

static int spr_for_mobj(const mobj_t *m)
{
	switch (m->kind) {
	case K_ZOMBIE:
		if (!m->alive) return SPR_POSSL;
		if (m->atkcool > 8 && g_sprites[SPR_POSSH]) return SPR_POSSH;
		return ((m->animtic >> 3) & 1) ? SPR_POSSB : SPR_POSSA;
	case K_SERGEANT:
		if (!m->alive) return SPR_SPOSL;
		if (m->atkcool > 8 && g_sprites[SPR_SPOSH]) return SPR_SPOSH;
		return ((m->animtic >> 3) & 1) ? SPR_SPOSB : SPR_SPOSA;
	case K_IMP:
		if (!m->alive) return SPR_TROOM;
		return ((m->animtic >> 3) & 1) ? SPR_TROOB : SPR_TROOA;
	case K_DEMON:
		if (!m->alive) return SPR_SARGN;
		return ((m->animtic >> 3) & 1) ? SPR_SARGB : SPR_SARGA;
	case K_BARREL: return SPR_BAR1;
	case K_FIREBALL:
	case K_EXPLOSION: return SPR_BEXP;
	case K_STIM: return SPR_STIM;
	case K_MEDKIT: return SPR_MEDIA;
	case K_ARM1: return SPR_ARM1;
	case K_ARM2: return SPR_ARM2;
	case K_CLIP: return SPR_CLIP;
	case K_SHEL: return SPR_SHEL;
	case K_SHOTGUN: return SPR_SHOT;
	case K_BKEY: return SPR_BKEY;
	case K_YKEY: return SPR_YKEY;
	case K_RKEY: return SPR_RKEY;
	case K_BON1: return SPR_BON1;
	case K_BON2: return SPR_BON2;
	}
	return SPR_NONE;
}

static void draw_automap_line(int x0, int y0, int x1, int y1, byte c)
{
	int dx = x1 > x0 ? (x1 - x0) : (x0 - x1);
	int sx = x0 < x1 ? 1 : -1;
	int dy = -(y1 > y0 ? (y1 - y0) : (y0 - y1));
	int sy = y0 < y1 ? 1 : -1;
	int err = dx + dy, e2;
	for (;;) {
		if (x0 >= 0 && x0 < SCREENW && y0 >= 0 && y0 < VIEWH)
			g_fb[y0 * SCREENW + x0] = c;
		if (x0 == x1 && y0 == y1)
			break;
		e2 = 2 * err;
		if (e2 >= dy) { err += dy; x0 += sx; }
		if (e2 <= dx) { err += dx; y0 += sy; }
	}
}

static void draw_automap(void)
{
	int i;
	int px = P.x >> 16, py = P.y >> 16;
	int fc = g_sin_tbl[((P.angle >> 4) + 1024) & 4095] >> 12;
	int fs = g_sin_tbl[P.angle >> 4] >> 12;
	memset(g_fb, 0, SCREENW * VIEWH);
	for (i = 0; i < numlinedefs; i++) {
		const linedef_t *ld = &linedefs[i];
		int x0 = CENTERX + (((vertexes[ld->v1].x >> 16) - px) >> 3);
		int y0 = CENTERY - (((vertexes[ld->v1].y >> 16) - py) >> 3);
		int x1 = CENTERX + (((vertexes[ld->v2].x >> 16) - px) >> 3);
		int y1 = CENTERY - (((vertexes[ld->v2].y >> 16) - py) >> 3);
		byte col = (ld->sidenum[1] < 0) ? g_colred
				 : (ld->special != 0) ? g_colgold : g_colbrick;
		draw_automap_line(x0, y0, x1, y1, col);
	}
	draw_automap_line(CENTERX - (fc >> 2), CENTERY + (fs >> 2),
					  CENTERX + (fc >> 1), CENTERY - (fs >> 1), g_colwhite);
	scr_text(4, 4, "E1M1: HANGAR [AUTOMAP]", g_colgreen);
}

void game_render(void)
{
	int i, sec;
	if (g_state == GS_TITLE || g_state == GS_HELP ||
		g_state == GS_INTERMISSION)
		return;

	sec = point_sector(P.x, P.y);
	vw_x = P.x;
	vw_y = P.y;
	vw_z = sectors[sec].floorz + (EYEHEIGHT << 16);
	if (P.dead && P.dead_tics < 20)
		vw_z -= P.dead_tics * (2 << 16);
	vw_angle = P.angle;
	vw_sector_light = 20 + ((sectors[sec].light * 11) >> 5);
	if (vw_sector_light > 31) vw_sector_light = 31;

	/* Populate sprite draw list BEFORE render_frame() (frustum-culled)! */
	g_numsprdraws = 0;
	{
		int fc = g_sin_tbl[((vw_angle >> 4) + 1024) & 4095] >> 8;
		int fs = g_sin_tbl[vw_angle >> 4] >> 8;
		for (i = 0; i < g_nummobjs; i++) {
			mobj_t *m = &g_mobjs[i];
			int spr, rx, ry, z, xs;
			if (!m->alive && m->kind != K_ZOMBIE && m->kind != K_SERGEANT &&
				m->kind != K_IMP && m->kind != K_DEMON)
				continue;
			rx = (m->x - vw_x) >> 8;
			ry = (m->y - vw_y) >> 8;
			z = rx * fc + ry * fs;
			if (z < (16 << 16) || z > (2048 << 16))
				continue;
			xs = rx * fs - ry * fc;
			if (xs < -z || xs > z)
				continue;
			spr = spr_for_mobj(m);
			if (spr < 0 || !g_sprites[spr])
				continue;
			if (g_numsprdraws < MAXSPRDRAW) {
				int msec = point_sector(m->x, m->y);
				sprdraw_t *sd = &g_sprdraws[g_numsprdraws++];
				sd->x = m->x;
				sd->y = m->y;
				sd->z = sectors[msec].floorz;
				sd->spr = spr;
				sd->light = 20 + ((sectors[msec].light * 11) >> 5);
				sd->flip = 0;
				sd->fullbright = (m->kind == K_FIREBALL ||
								  m->kind == K_EXPLOSION);
			}
		}
	}

	if (P.automap) {
		draw_automap();
		g_numsprdraws = 0;
	} else {
		render_frame();
		if (!P.dead) {
			int bobx = (g_sin_tbl[((P.bob_phase << 3) & 4095)]) >> 14;
			int boby = (g_sin_tbl[((P.bob_phase << 4) & 4095)]) >> 15;
			draw_weapon(P.weapon, P.fire_frame, bobx, boby);
		}
	}

	if (P.pain_tics > 6)
		scr_tint(g_colred);
	if (P.dead)
		scr_tint(g_colred);
	draw_hud(P.health, P.armor,
			 P.weapon == WP_SHOTGUN ? P.ammo_shells
									: P.weapon == WP_PISTOL ? P.ammo_bullets : 0,
			 P.weapon, P.keys, P.dead);
	if (g_message_tics > 0 && g_message)
		draw_message(g_message);
	if (P.dead && P.dead_tics > 20) {
		scr_text_center(40, "YOU DIED", g_colred);
		if (P.dead_tics > 60)
			scr_text_center(60, "PRESS ENTER TO RESTART", g_colwhite);
	}
}
