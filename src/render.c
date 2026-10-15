/* DOOM EX-word renderer: column-based BSP renderer, 320x200 8-bit paletted.
 *
 * Conventions:
 *  - world coords: fixed 16.16, map x right / y up
 *  - angle: 0..65535 CCW, 0 = +x
 *  - eye at (vw_x, vw_y), height vw_z
 *  - forward f=(cosA,sinA), right r=(sinA,-cosA)
 *  - depth z = dot(P-eye, f); col = CENTERX + dot(P-eye,r)*160/z
 *  - row = CENTERY - (h - vw_z)*160/z
 *  - per-column ray d = f + r*sx, sx=(col-CENTERX)/160 (stored 2.14);
 *    for points on the ray the forward depth equals the ray parameter t
 *  - ray/seg intersect uses the seg's UNIT direction u (2.14):
 *      t  = cross(A-eye,u) / cross(d,u)   [units]
 *      s  = cross(A-eye,d) / cross(d,u)   [units along seg]
 *    numerators in units, denominators in units<<14 (uniform)
 */
#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "wad.h"
#include "render.h"
#include "font8x8.h"

#ifdef SEG_DEBUG
int dbg_n = 0;
#ifndef DBGCOL
#define DBGCOL 200
#endif
#include <stdio.h>
#endif

byte *g_fb;

#define PROJ 160
#define NEARZ (1 << 16)			/* 1 unit */

int *g_sin_tbl;					/* 16.16, 4096 entries (heap) */
static byte *g_walls;				/* procedural wall textures 64x64 (heap) */
static byte g_skyband[VIEWH];

byte g_colwhite, g_colred, g_colgold, g_colgray, g_colgreen, g_coldark,
	g_colblue, g_colbrick;

/* camera */
int vw_x, vw_y, vw_z;
angle_t vw_angle;
int vw_sector_light;

static int g_cos, g_sinA;					/* 16.16 */
static int g_rdx[SCREENW], g_rdy[SCREENW];	/* 2.14 per-column rays */
static int g_invrow[VIEWH];					/* (PROJ<<6)/(CENTERY-y) */

static short g_ceilclip[SCREENW];
static short g_floorclip[SCREENW];
static int g_walldepth[SCREENW];
static int g_opencols;

sprdraw_t g_sprdraws[MAXSPRDRAW];
int g_numsprdraws;

/* ---------------- setup ---------------- */

/* Pure 32-bit integer sine in 16.16 fixed point for quadrant u in 0..1024 */
static int isin_quad(int i)
{
	int u = i << 4;				/* 0..16384 (2.14) */
	int u2 = (u * u) >> 14;		/* 0..16384 */
	int p = 5223 - ((u2 * 310) >> 14);
	p = 42334 - ((u2 * p) >> 14);
	p = 102944 - ((u2 * p) >> 14);
	p = (u * p) >> 14;
	if (p > 65536) p = 65536;
	return p;
}

int pal_nearest(int r, int g, int b)
{
	int best = 0, bestd = 0x7FFFFFFF, i;
	for (i = 0; i < 256; i++) {
		int dr = r - g_palette[i * 3];
		int dg = g - g_palette[i * 3 + 1];
		int db = b - g_palette[i * 3 + 2];
		int d = dr * dr + dg * dg + db * db;
		if (d < bestd) {
			bestd = d;
			best = i;
		}
	}
	return best;
}

static void set_texel(byte *tex, int u, int v, int r, int g, int b)
{
	tex[(v & 63) * 64 + (u & 63)] = (byte)pal_nearest(r, g, b);
}

static unsigned rs = 12345;
unsigned rnd_next(void)
{
	rs = rs * 1103515245u + 12345u;
	return rs >> 8;
}

static void gen_texture(int kind)
{
	byte *tex = g_walls + kind * 4096;
	int u, v;
	memset(tex, 0, 4096);
	switch (kind) {
	case WK_STARTAN:
		for (v = 0; v < 64; v++)
			for (u = 0; u < 64; u++) {
				int r = 168, g = 126, b = 86;
				int uu = (u + ((v >> 4) & 1) * 16) & 31;
				if (((v & 15) == 0) || (uu == 0)) { r = 92; g = 66; b = 44; }
				else if ((((v & 15) == 4) || ((v & 15) == 12)) &&
						 (uu == 6 || uu == 26)) { r = 64; g = 46; b = 30; }
				else if ((u & 7) == 7 && ((v >> 2) & 1)) { r -= 14; g -= 10; b -= 8; }
				set_texel(tex, u, v, r, g, b);
			}
		break;
	case WK_STARG:
		for (v = 0; v < 64; v++)
			for (u = 0; u < 64; u++) {
				int r = 104, g = 112, b = 96;
				if (((v & 7) == 0) || (((u + ((v >> 3) & 1) * 8) & 15) == 0))
					{ r = 58; g = 62; b = 52; }
				else { r += (int)(rnd_next() % 17) - 8; g += (int)(rnd_next() % 17) - 8; }
				set_texel(tex, u, v, r, g, b);
			}
		break;
	case WK_GSTONE:
		for (v = 0; v < 64; v++)
			for (u = 0; u < 64; u++) {
				int r = 128, g = 128, b = 126;
				if (((v & 15) == 0) || (((u + ((v >> 4) & 1) * 8) & 23) == 0))
					{ r = 70; g = 70; b = 68; }
				else { r += (int)(rnd_next() % 21) - 10; g = r; b = r; }
				set_texel(tex, u, v, r, g, b);
			}
		break;
	case WK_BROWN:
		for (v = 0; v < 64; v++)
			for (u = 0; u < 64; u++) {
				int r = 122, g = 86, b = 56;
				if ((u & 15) == 0) { r = 70; g = 48; b = 30; }
				else if ((u & 15) == 8) { r += 18; g += 12; b += 8; }
				if ((v & 31) == 30) { r = 66; g = 46; b = 28; }
				set_texel(tex, u, v, r, g, b);
			}
		break;
	case WK_METAL:
		for (v = 0; v < 64; v++)
			for (u = 0; u < 64; u++) {
				int r = 88, g = 88, b = 94;
				if ((u & 7) < 2) { r -= 26; g -= 26; b -= 26; }
				if ((u & 31) == 16 && (v & 15) == 8) { r = 40; g = 40; b = 44; }
				if (v == 63) { r = 48; g = 48; b = 52; }
				set_texel(tex, u, v, r, g, b);
			}
		break;
	case WK_DOOR:
		for (v = 0; v < 64; v++)
			for (u = 0; u < 64; u++) {
				int r = 122, g = 104, b = 70;
				if (u < 2 || u > 61 || v < 2 || v > 61) { r = 60; g = 50; b = 34; }
				else if (u == 31 || u == 32) { r = 52; g = 44; b = 30; }
				else if (u == 10 || u == 21 || u == 42 || u == 53) { r -= 30; g -= 26; b -= 18; }
				if (v == 40 && u > 24 && u < 40) { r = 180; g = 40; b = 30; }
				set_texel(tex, u, v, r, g, b);
			}
		break;
	case WK_DOORTRACK:
		for (v = 0; v < 64; v++)
			for (u = 0; u < 64; u++) {
				int r, g, b;
				if ((u & 7) < 3) { r = 30; g = 30; b = 32; }
				else { r = 96; g = 96; b = 100; }
				set_texel(tex, u, v, r, g, b);
			}
		break;
	case WK_STEP:
		for (v = 0; v < 64; v++)
			for (u = 0; u < 64; u++) {
				int r = 96, g = 118, b = 70;
				if (((v & 7) == 0) || ((u & 7) == 0)) { r = 54; g = 66; b = 40; }
				set_texel(tex, u, v, r, g, b);
			}
		break;
	case WK_COMP:
		for (v = 0; v < 64; v++)
			for (u = 0; u < 64; u++) {
				int r = 42, g = 46, b = 54;
				if (v > 12 && v < 52 && u > 8 && u < 56) {
					if ((v & 7) == 0) { r = 24; g = 26; b = 30; }
					else if ((v & 3) == 1 && (rnd_next() & 7) > 4) {
						r = (rnd_next() & 4) ? 200 : 60;
						g = 200; b = 60;
					}
				}
				set_texel(tex, u, v, r, g, b);
			}
		break;
	case WK_EXIT:
		for (v = 0; v < 64; v++)
			for (u = 0; u < 64; u++) {
				int r = 60, g = 60, b = 60;
				if (u > 16 && u < 48 && v > 20 && v < 44) { r = 20; g = 20; b = 20; }
				if (u > 20 && u < 44 && v > 26 && v < 38) {
					r = 40; g = 200; b = 60;
					if (v == 26 || v == 37 || u == 20 || u == 43) { r = 12; g = 90; b = 24; }
				}
				set_texel(tex, u, v, r, g, b);
			}
		break;
	case WK_SKIN:
		for (v = 0; v < 64; v++)
			for (u = 0; u < 64; u++) {
				int r = 100 + (int)(rnd_next() % 31) - 15;
				int g = 52 + (int)(rnd_next() % 15) - 7;
				int b = 44 + (int)(rnd_next() % 15) - 7;
				set_texel(tex, u, v, r, g, b);
			}
		break;
	default: /* WK_PLAIN */
		for (v = 0; v < 64; v++)
			for (u = 0; u < 64; u++) {
				int r = 96, g = 96, b = 96;
				if ((v & 31) == 0 || (u & 31) == 0) { r = 70; g = 70; b = 70; }
				else { r += (int)(rnd_next() % 13) - 6; g = r; b = r; }
				set_texel(tex, u, v, r, g, b);
			}
		break;
	}
}

void render_init(void)
{
	int i;
	g_fb = (byte *)malloc(SCREENW * SCREENH);
	g_sin_tbl = (int *)malloc(sizeof(int) * 4096);
	g_walls = (byte *)malloc(WK_COUNT * 4096);
	if (!g_fb || !g_sin_tbl || !g_walls)
		return;
	if (g_fb)
		memset(g_fb, 0, SCREENW * SCREENH);

	for (i = 0; i <= 1024; i++) {
		int s = isin_quad(i);
		g_sin_tbl[i] = s;
		if (i < 1024) {
			g_sin_tbl[2048 - i] = s;
			g_sin_tbl[2048 + i] = -s;
			if (i > 0)
				g_sin_tbl[4096 - i] = -s;
		}
	}
	g_sin_tbl[3072] = -65536;

	for (i = 0; i < WK_COUNT; i++)
		gen_texture(i);

	{
		byte c0 = (byte)pal_nearest(18, 26, 66);
		byte c1 = (byte)pal_nearest(44, 78, 150);
		byte c2 = (byte)pal_nearest(118, 146, 182);
		for (i = 0; i < VIEWH; i++) {
			if (i < VIEWH / 4)
				g_skyband[i] = c0;
			else if (i < VIEWH / 2)
				g_skyband[i] = c1;
			else
				g_skyband[i] = c2;
		}
	}

	g_colwhite = (byte)pal_nearest(240, 240, 240);
	g_colred = (byte)pal_nearest(252, 60, 52);
	g_colgold = (byte)pal_nearest(220, 168, 64);
	g_colgray = (byte)pal_nearest(150, 150, 150);
	g_colgreen = (byte)pal_nearest(80, 220, 80);
	g_coldark = (byte)pal_nearest(48, 48, 48);
	g_colblue = (byte)pal_nearest(80, 120, 252);
	g_colbrick = (byte)pal_nearest(150, 70, 40);
}

/* ---------------- primitives ---------------- */

void scr_fill(byte c) { memset(g_fb, c, SCREENW * SCREENH); }

void scr_rect(int x, int y, int w, int h, byte c)
{
	int iy, ix;
	if (x < 0) { w += x; x = 0; }
	if (y < 0) { h += y; y = 0; }
	if (x + w > SCREENW) w = SCREENW - x;
	if (y + h > SCREENH) h = SCREENH - y;
	if (w <= 0 || h <= 0)
		return;
	for (iy = 0; iy < h; iy++) {
		byte *row = g_fb + (y + iy) * SCREENW + x;
		for (ix = 0; ix < w; ix++)
			row[ix] = c;
	}
}

void scr_text(int x, int y, const char *s, byte c)
{
	int cx = x;
	while (*s) {
		char ch = *s++;
		if (ch == '\n') { y += 8; cx = x; continue; }
		if (ch < 32 || ch > 126) ch = '?';
		{
			const unsigned char *g = font8x8[ch - 32];
			int r, b;
			for (r = 0; r < 8; r++) {
				unsigned char bits = g[r];
				if (y + r < 0 || y + r >= SCREENH)
					continue;
				for (b = 0; b < 8; b++)
					if (bits & (1 << b))
						if (cx + b >= 0 && cx + b < SCREENW)
							g_fb[(y + r) * SCREENW + cx + b] = c;
			}
		}
		cx += 8;
	}
}

void scr_text_big(int x, int y, const char *s, byte c, int scale)
{
	int cx = x;
	while (*s) {
		char ch = *s++;
		if (ch < 32 || ch > 126) ch = '?';
		{
			const unsigned char *g = font8x8[ch - 32];
			int r, b, sx, sy;
			for (r = 0; r < 8; r++) {
				unsigned char bits = g[r];
				for (b = 0; b < 8; b++)
					if (bits & (1 << b))
						for (sy = 0; sy < scale; sy++)
							for (sx = 0; sx < scale; sx++) {
								int py = y + r * scale + sy;
								int px = cx + b * scale + sx;
								if (py >= 0 && py < SCREENH && px >= 0 && px < SCREENW)
									g_fb[py * SCREENW + px] = c;
							}
			}
		}
		cx += 8 * scale;
	}
}

void scr_text_center(int y, const char *s, byte c)
{
	int len = 0;
	const char *p = s;
	while (*p++) len++;
	scr_text((SCREENW - len * 8) / 2, y, s, c);
}

void scr_tint(byte c)
{
	int i, n = VIEWW * VIEWH;
	for (i = 0; i < n; i++) {
		byte *p = &g_fb[i];
		if (*p != c)
			*p = (byte)(((*p) + c) >> 1);
	}
}

void draw_sprite_patch(int spr, int x, int y, int lightrow, int flip)
{
	const sprite_t *sp = g_sprites[spr];
	int sx;
	const byte *cmap;
	if (!sp || spr < 0 || spr >= SPR_COUNT)
		return;
	if (lightrow < 0) lightrow = 0;
	if (lightrow > 31) lightrow = 31;
	cmap = g_colormap + ((31 - lightrow) << 8);
	for (sx = 0; sx < sp->w; sx++) {
		int dx = flip ? (x + sp->w - 1 - sx) : (x + sx);
		int dy;
		if (dx < 0 || dx >= SCREENW)
			continue;
		for (dy = 0; dy < sp->h; dy++) {
			byte c = sp->pix[sx * sp->h + dy];
			int py = y + dy;
			if (!c || py < 0 || py >= SCREENH)
				continue;
			g_fb[py * SCREENW + dx] = cmap[c];
		}
	}
}

/* ---------------- world rendering ---------------- */

static int g_allclosed;

static int side_of(const node_t *nd, int x, int y)
{
	/* cross(dir, P-N) = dx*(py-ny) - dy*(px-nx); > 0 -> side 1 */
	return (((nd->dx >> 16) * ((y - nd->y) >> 16)) -
			((nd->dy >> 16) * ((x - nd->x) >> 16))) > 0
			   ? 1
			   : 0;
}

static void col_closed(int cx)
{
	if (g_ceilclip[cx] > g_floorclip[cx]) {
		g_opencols--;
		if (g_opencols <= 0)
			g_allclosed = 1;
	}
}

/* cast one flat into column cx rows y0..y1 (2-row stepped for 2x speed) */
static void cast_flat_col(int cx, int y0, int y1, int planez, const byte *flat,
						  int lightrow_base)
{
	int y;
	int dz = planez - vw_z;
	int dx8 = g_rdx[cx] >> 6, dy8 = g_rdy[cx] >> 6;
	byte *fb;
	if (y0 > y1 || !flat)
		return;
	{
		int a = dz < 0 ? -dz : dz;
		if (a < (4 << 16))
			return;
		if (a > (1024 << 16))
			a = 1024 << 16;
		dz = dz < 0 ? -a : a;
		dz >>= 8;	/* units<<8, signed */
	}
	fb = g_fb + y0 * SCREENW + cx;
	for (y = y0; y <= y1; y += 2, fb += SCREENW * 2) {
		int dist12 = dz * g_invrow[y];
		int dist16, wx, wy, u, v, lightrow;
		byte pix;
		if (dist12 > (512 << 12))
			dist12 = 512 << 12;
		if (dist12 < 0)
			continue;
		dist16 = dist12 << 4;
		wx = vw_x + ((dx8 * (dist16 >> 6)) >> 2);
		wy = vw_y + ((dy8 * (dist16 >> 6)) >> 2);
		u = (wx >> 16) & 63;
		v = (wy >> 16) & 63;
		lightrow = lightrow_base - (dist16 >> 22);
		if (lightrow < 0) lightrow = 0;
		if (lightrow > 31) lightrow = 31;
		pix = g_colormap[((31 - lightrow) << 8) + flat[(v << 6) + u]];
		*fb = pix;
		if (y + 1 <= y1)
			fb[SCREENW] = pix;
	}
}

static void fill_sky(int cx, int y0, int y1)
{
	int y;
	int scroll = (((int)vw_angle * SCREENW) >> 16) & ~7;
	if (y0 < 0) y0 = 0;
	if (y1 >= VIEWH) y1 = VIEWH - 1;
	for (y = y0; y <= y1; y++) {
		byte c = g_skyband[y];
		if (((((cx + scroll) >> 3) + y) & 15) == 0 && y < VIEWH / 2)
			c += 1;
		g_fb[y * SCREENW + cx] = c;
	}
}

/* textured wall column; v0 = texel v at row ytop (16.16), dv per row */
static void draw_wall_col(int x, int ytop, int ybot, int u16, int v0, int dv,
						  const byte *tex, const byte *crow)
{
	int y, v;
	byte *fb;
	const byte *tcol;
	if (ytop < 0) {
		v0 += -ytop * dv;
		ytop = 0;
	}
	if (ybot >= VIEWH)
		ybot = VIEWH - 1;
	if (ytop > ybot)
		return;
	fb = g_fb + ytop * SCREENW + x;
	tcol = tex + (((u16 >> 16) & 63) << 6);
	v = v0;
	for (y = ytop; y <= ybot; y++, fb += SCREENW) {
		*fb = crow[tcol[(v >> 16) & 63]];
		v += dv;
	}
}

/* per-column wall/flat draw for one seg */
static void render_seg(int segi)
{
	const seg_t *sg = &segs[segi];
	const linedef_t *ld = &linedefs[sg->linedef];
	int side = sg->dir ? 1 : 0;
	int sidenum = ld->sidenum[side];
	const sidedef_t *sd;
	const sector_t *front, *back = 0;
	int has_back = 0;
	int ax = vertexes[sg->v1].x, ay = vertexes[sg->v1].y;
	int sx = vertexes[sg->v2].x - ax, sy = vertexes[sg->v2].y - ay;
	int prx = ax - vw_x, pry = ay - vw_y;
	int udx14, udy14;			/* unit dir 2.14 */
	int seglen16;				/* 16 * seg length in units */
	int u_anchor;
	int col1, col2, cx;

	if (sidenum < 0 || sidenum >= numsidedefs)
		return;
	sd = &sidedefs[sidenum];
	if (sd->sector < 0 || sd->sector >= numsectors)
		return;
	front = &sectors[sd->sector];
	if (ld->sidenum[side ^ 1] >= 0 && ld->sidenum[side ^ 1] < numsidedefs) {
		const sidedef_t *sd2 = &sidedefs[ld->sidenum[side ^ 1]];
		if (sd2->sector >= 0 && sd2->sector < numsectors)
			back = &sectors[sd2->sector];
	}
	if (back == front) {
		if (!sd->hasmid)
			return;	/* invisible internal trigger/sound line */
		back = 0;
	}
	has_back = (back != 0);

	/* Back-face cull in world space: camera must be on front (right) side of seg A->B */
	if (((prx >> 16) * (sy >> 16)) - ((pry >> 16) * (sx >> 16)) >= 0)
		return;

	seglen16 = sg->seglen16;
	udx14 = sg->udx14;
	udy14 = sg->udy14;
	u_anchor = sd->xoff + sg->segofs;

	/* clip endpoints to the near plane in camera space for column bounds */
	{
		int x1 = ((prx >> 8) * (g_sinA >> 8)) - ((pry >> 8) * (g_cos >> 8));
		int z1 = ((prx >> 8) * (g_cos >> 8)) + ((pry >> 8) * (g_sinA >> 8));
		int x2 = (((prx + sx) >> 8) * (g_sinA >> 8)) -
				 (((pry + sy) >> 8) * (g_cos >> 8));
		int z2 = (((prx + sx) >> 8) * (g_cos >> 8)) +
				 (((pry + sy) >> 8) * (g_sinA >> 8));
		int zs1, zs2, xs1, xs2;

		if (z1 < NEARZ && z2 < NEARZ)
			return;
		if (z1 < NEARZ) {
			int dz = (z2 - z1) >> 8;
			if (dz > 0) {
				int tt = (((NEARZ - z1) >> 8) << 12) / dz;
				x1 += ((x2 - x1) >> 12) * tt;
			}
			z1 = NEARZ;
		} else if (z2 < NEARZ) {
			int dz = (z1 - z2) >> 8;
			if (dz > 0) {
				int tt = (((NEARZ - z2) >> 8) << 12) / dz;
				x2 += ((x1 - x2) >> 12) * tt;
			}
			z2 = NEARZ;
		}
		zs1 = z1 >> 4; zs2 = z2 >> 4;
		if (zs1 < 1) zs1 = 1;
		if (zs2 < 1) zs2 = 1;
		xs1 = x1 >> 4; xs2 = x2 >> 4;
		if (xs1 > zs1 << 2) xs1 = zs1 << 2;
		if (xs1 < -(zs1 << 2)) xs1 = -(zs1 << 2);
		if (xs2 > zs2 << 2) xs2 = zs2 << 2;
		if (xs2 < -(zs2 << 2)) xs2 = -(zs2 << 2);
		col1 = CENTERX + (xs1 * PROJ) / zs1;
		col2 = CENTERX + (xs2 * PROJ) / zs2;
	}
	if (col1 > col2)
		return;
	if (col2 < 0 || col1 >= SCREENW)
		return;

	for (cx = col1; cx <= col2; cx++) {
		int ct, cb;
		int pdx, pdy, den, tnum, snum;
		int t, s, z, zq;
		int u16, dv;
		int ceil_row, floor_row, bceil_row = 0, bfloor_row = 0;
		int lightrow;
		const byte *crow;

		if (cx < 0 || cx >= SCREENW)
			continue;
		ct = g_ceilclip[cx];
		cb = g_floorclip[cx];
#ifdef SEG_DEBUG
		if (cx == DBGCOL && dbg_n < 60) {
			extern int dbg_n;
			printf("seg %d dir %d ct %d cb %d cols[%d..%d] front s%d back %s\n",
				   segi, side, ct, cb, col1, col2, front - sectors,
				   back ? "Y" : "N");
			dbg_n++;
		}
#endif
		if (ct > cb)
			continue;

		/* ray/seg intersection, all operands uniform scale */
		pdx = prx >> 16;	/* units */
		pdy = pry >> 16;
		den = ((g_rdx[cx] * udy14) - (g_rdy[cx] * udx14)) >> 14;	/* units<<14 */
		if (den < 64 && den > -64)
			continue;	/* ray nearly parallel to seg */
		tnum = pdx * udy14 - pdy * udx14;			/* units<<14 */
		t = tnum / den;								/* units */
		if (t < 1 || t > 4096)
			continue;	/* behind near plane or absurd */
		snum = pdx * g_rdy[cx] - pdy * g_rdx[cx];	/* units<<14 */
		s = snum / den;								/* units */
		if (s < 0) s = 0;
		{
			int smax = (seglen16 >> 4) + 16;
			if (s > smax) s = smax;
		}
#ifdef SEG_DEBUG
		if (cx == DBGCOL && dbg_n < 120) {
			extern int dbg_n;
			printf("   hit seg %d t %d s %d z %d rows c%d f%d bc%d bf%d lrow %d\n",
				   segi, t, s, z, ceil_row, floor_row, bceil_row,
				   bfloor_row, lightrow);
			dbg_n++;
		}
#endif

		z = t << 16;
		zq = z >> 8;
		if (zq < 256) zq = 256;
		u16 = (u_anchor + s) << 16;
		dv = z / PROJ;	/* texel v step per row, 16.16 */

		ceil_row = CENTERY - (int)((((front->ceilz - vw_z) >> 8) * PROJ) / zq);
		floor_row = CENTERY - (int)((((front->floorz - vw_z) >> 8) * PROJ) / zq);
		if (has_back) {
			bceil_row = CENTERY - (int)((((back->ceilz - vw_z) >> 8) * PROJ) / zq);
			bfloor_row = CENTERY - (int)((((back->floorz - vw_z) >> 8) * PROJ) / zq);
		}

		lightrow = vw_sector_light - (z >> 23);
		if (lightrow < 0) lightrow = 0;
		if (lightrow > 31) lightrow = 31;
		crow = g_colormap + ((31 - lightrow) << 8);

		if (!has_back) {
			/* 1-sided solid wall: draw ceiling above, floor below, wall in between */
			int cy1 = ceil_row - 1;
			int fy0 = floor_row + 1;
			if (cy1 > cb) cy1 = cb;
			if (ct <= cy1) {
				if (front->ceilpic == FLAT_SKY)
					fill_sky(cx, ct, cy1);
				else if (front->ceilpic >= 0)
					cast_flat_col(cx, ct, cy1, front->ceilz,
								  g_flats[front->ceilpic], vw_sector_light);
				ct = cy1 + 1;
			}
			if (fy0 < ct) fy0 = ct;
			if (fy0 <= cb) {
				if (front->floorpic >= 0 && front->floorpic != FLAT_SKY)
					cast_flat_col(cx, fy0, cb, front->floorz,
								  g_flats[front->floorpic], vw_sector_light);
				cb = fy0 - 1;
			}
			if (ct <= cb) {
				int v0 = (sd->yoff << 16) + (ct - ceil_row) * dv;
				draw_wall_col(cx, ct, cb, u16, v0, dv,
							  (g_walls + (sd->wk_mid << 12)), crow);
			}
			g_ceilclip[cx] = VIEWH;
			g_floorclip[cx] = -1;
			if (z < g_walldepth[cx])
				g_walldepth[cx] = z;
			col_closed(cx);
			if (g_allclosed)
				return;
		} else {
			/* 2-sided portal */
			int cy1 = ceil_row - 1;
			int fy0 = floor_row + 1;
			if (cy1 > cb) cy1 = cb;
			if (ct <= cy1) {
				if (front->ceilpic == FLAT_SKY)
					fill_sky(cx, ct, cy1);
				else if (front->ceilpic >= 0)
					cast_flat_col(cx, ct, cy1, front->ceilz,
								  g_flats[front->ceilpic], vw_sector_light);
				ct = cy1 + 1;
			}
			if (fy0 < ct) fy0 = ct;
			if (fy0 <= cb) {
				if (front->floorpic >= 0 && front->floorpic != FLAT_SKY)
					cast_flat_col(cx, fy0, cb, front->floorz,
								  g_flats[front->floorpic], vw_sector_light);
				cb = fy0 - 1;
			}
			if (back->ceilz <= front->floorz || back->floorz >= front->ceilz) {
				/* Closed door or closed window */
				if (ct <= cb) {
					byte wk = (back->ceilz < front->ceilz) ? sd->wk_up : sd->wk_lo;
					int v0 = (sd->yoff << 16) + (ct - ceil_row) * dv;
					draw_wall_col(cx, ct, cb, u16, v0, dv,
								  (g_walls + (wk << 12)), crow);
				}
				g_ceilclip[cx] = VIEWH;
				g_floorclip[cx] = -1;
				if (z < g_walldepth[cx])
					g_walldepth[cx] = z;
				col_closed(cx);
				if (g_allclosed)
					return;
			} else {
				if (front->ceilz > back->ceilz) {
					int uy1 = bceil_row - 1;
					if (uy1 > cb) uy1 = cb;
					if (front->ceilpic == FLAT_SKY && back->ceilpic == FLAT_SKY) {
						if (ct <= uy1) {
							fill_sky(cx, ct, uy1);
							ct = uy1 + 1;
						}
					} else if (ct <= uy1) {
						int v0 = ((sd->yoff + ((front->ceilz - back->ceilz) >> 16)) << 16) -
								 ((bceil_row - ct) * dv);
						draw_wall_col(cx, ct, uy1, u16, v0, dv,
									  (g_walls + (sd->wk_up << 12)), crow);
						ct = uy1 + 1;
					}
				}
				if (back->floorz > front->floorz) {
					int ly0 = bfloor_row + 1;
					if (ly0 < ct) ly0 = ct;
					if (ly0 <= cb) {
						int v0 = (sd->yoff << 16) + (ly0 - bfloor_row) * dv;
						draw_wall_col(cx, ly0, cb, u16, v0, dv,
									  (g_walls + (sd->wk_lo << 12)), crow);
						cb = ly0 - 1;
					}
				}
				g_ceilclip[cx] = (short)ct;
				g_floorclip[cx] = (short)cb;
				if (ct > cb && z < g_walldepth[cx])
					g_walldepth[cx] = z;
				col_closed(cx);
				if (g_allclosed)
					return;
			}
		}
	}
}

static void render_subsector(int ss)
{
	int i;
	for (i = 0; i < ssectors[ss].numsegs; i++)
		render_seg(ssectors[ss].firstseg + i);
}

/* Fast frustum check for BSP node bounding box [top, bot, left, right] in units */
static int bbox_visible(const int *bb)
{
	int vx = vw_x >> 16, vy = vw_y >> 16;
	int fc = g_cos >> 8, fs = g_sinA >> 8;
	int i, any_front = 0, all_left = 1, all_right = 1;
	int cx[4], cy[4];
	if (vx >= bb[2] - 16 && vx <= bb[3] + 16 &&
		vy >= bb[1] - 16 && vy <= bb[0] + 16)
		return 1;
	cx[0] = bb[2] - vx; cy[0] = bb[1] - vy;
	cx[1] = bb[3] - vx; cy[1] = bb[1] - vy;
	cx[2] = bb[2] - vx; cy[2] = bb[0] - vy;
	cx[3] = bb[3] - vx; cy[3] = bb[0] - vy;
	for (i = 0; i < 4; i++) {
		int z = (cx[i] * fc + cy[i] * fs) >> 8;
		int x = (cx[i] * fs - cy[i] * fc) >> 8;
		if (z > 1)
			any_front = 1;
		if (x >= -z - 32)
			all_left = 0;
		if (x <= z + 32)
			all_right = 0;
	}
	return any_front && !all_left && !all_right;
}

static void render_bsp(int nodei)
{
	while (!(nodei & 0x8000u)) {
		const node_t *nd = &nodes[nodei];
		int side = side_of(nd, vw_x, vw_y);
		render_bsp(nd->children[side]);
		if (g_allclosed)
			return;
		if (!bbox_visible(nd->bbox[side ^ 1]))
			return;
		nodei = nd->children[side ^ 1];
	}
	render_subsector(nodei & 0x7FFFu);
}

void render_frame(void)
{
	int x;
	g_cos = g_sin_tbl[((vw_angle >> 4) + 1024) & 4095];
	g_sinA = g_sin_tbl[vw_angle >> 4];

	memset(g_fb, 0, SCREENW * VIEWH);

	for (x = 0; x < SCREENW; x++) {
		int sx = (((x - CENTERX) << 14) / PROJ);	/* 2.14 */
		g_rdx[x] = (g_cos >> 2) + ((g_sinA * sx) >> 16);
		g_rdy[x] = (g_sinA >> 2) - ((g_cos * sx) >> 16);
		g_ceilclip[x] = 0;
		g_floorclip[x] = VIEWH - 1;
		g_walldepth[x] = 0x7FFFFFFF;
	}
	for (x = 0; x < VIEWH; x++) {
		int q = CENTERY - x;
		if (q > -2 && q < 2)
			q = (q < 0) ? -2 : 2;
		g_invrow[x] = (PROJ << 4) / q;	/* signed */
	}
	g_opencols = SCREENW;
	g_allclosed = 0;

	if (numnodes > 0)
		render_bsp(numnodes - 1);
	else if (numssectors > 0)
		render_subsector(0);

	/* safety: fill any columns the approximated bounds left open */
	{
		static byte fillc = 0xFF;
		if (fillc == 0xFF)
			fillc = (byte)pal_nearest(40, 36, 32);
		for (x = 0; x < SCREENW; x++) {
			int y;
			if (g_ceilclip[x] <= g_floorclip[x])
				for (y = g_ceilclip[x]; y <= g_floorclip[x]; y++)
					g_fb[y * SCREENW + x] = fillc;
		}
	}

	/* ---- sprites: painter, far to near ---- */
	{
		int i, j;
		int depth[MAXSPRDRAW];
		int order[MAXSPRDRAW];
		for (i = 0; i < g_numsprdraws; i++) {
			sprdraw_t *sd = &g_sprdraws[i];
			int relx = sd->x - vw_x, rely = sd->y - vw_y;
			depth[i] = ((relx >> 8) * (g_cos >> 8)) + ((rely >> 8) * (g_sinA >> 8));
			order[i] = i;
		}
		for (i = 1; i < g_numsprdraws; i++) {
			int key = order[i], kd = depth[key];
			j = i - 1;
			while (j >= 0 && depth[order[j]] < kd) {
				order[j + 1] = order[j];
				j--;
			}
			order[j + 1] = key;
		}
		for (i = 0; i < g_numsprdraws; i++) {
			sprdraw_t *sd = &g_sprdraws[order[i]];
			int z = depth[order[i]];
			int xs = ((sd->x - vw_x) >> 8) * (g_sinA >> 8) -
					 ((sd->y - vw_y) >> 8) * (g_cos >> 8);
			const sprite_t *sp = g_sprites[sd->spr];
			int zu, scale, cc, x0, x1, rowbot, rowtop, h, col;
			int lightrow;
			const byte *cmap;
			if (!sp || z < NEARZ)
				continue;
			zu = z >> 16;
			if (zu < 1) zu = 1;
			{
				int xsc = xs >> 16;
				int lim = zu << 2;
				if (xsc > lim) xsc = lim;
				if (xsc < -lim) xsc = -lim;
				cc = CENTERX + (xsc * PROJ) / zu;
			}
			scale = (PROJ << 16) / zu;		/* 16.16 px per texel */
			h = (sp->h * scale) >> 16;
			if (h <= 0 || h > VIEWH * 4)
				continue;
			x0 = cc - (((sp->w >> 1) * scale) >> 16);
			x1 = x0 + ((sp->w * scale) >> 16);
			rowbot = CENTERY + (int)((((vw_z - sd->z) >> 8) * PROJ) / (zu << 8));
			rowtop = rowbot - h + 1;
			lightrow = sd->fullbright ? 31 : (sd->light - (z >> 23));
			if (lightrow < 0) lightrow = 0;
			if (lightrow > 31) lightrow = 31;
			cmap = g_colormap + ((31 - lightrow) << 8);
			for (col = x0; col < x1; col++) {
				int texx, yy0, yy1, sy;
				const byte *texcol;
				int span = x1 - x0;
				if (col < 0 || col >= SCREENW || span <= 0)
					continue;
				if (z >= g_walldepth[col])
					continue;
				texx = ((col - x0) * sp->w) / span;
				if (sd->flip)
					texx = sp->w - 1 - texx;
				if (texx < 0 || texx >= sp->w)
					continue;
				texcol = sp->pix + texx * sp->h;
				yy0 = rowtop < 0 ? 0 : rowtop;
				yy1 = rowbot >= VIEWH ? VIEWH - 1 : rowbot;
				for (sy = yy0; sy <= yy1; sy++) {
					int texy = ((sy - rowtop) * sp->h) / h;
					byte c;
					if (texy < 0) texy = 0;
					if (texy >= sp->h) texy = sp->h - 1;
					c = texcol[texy];
					if (c)
						g_fb[sy * SCREENW + col] = cmap[c];
				}
			}
		}
	}
	g_numsprdraws = 0;
}

/* ---------------- HUD & weapon ---------------- */

void draw_weapon(int weapon, int frame, int bobx, int boby)
{
	int base_spr = SPR_NONE, flash_spr = SPR_NONE;
	switch (weapon) {
	case WP_FIST:
		base_spr = SPR_PUNG;
		break;
	case WP_PISTOL:
		base_spr = (frame == 2) ? SPR_PISGB : SPR_PISG;
		if (frame == 1) flash_spr = SPR_PISF;
		break;
	case WP_SHOTGUN:
		base_spr = (frame == 2) ? SPR_SHTGB : SPR_SHTG;
		if (frame == 1) flash_spr = SPR_SHTF;
		break;
	}
	if (base_spr >= 0 && g_sprites[base_spr]) {
		const sprite_t *sp = g_sprites[base_spr];
		int x = CENTERX - (sp->w / 2) + (bobx >> 2);
		int y = VIEWH - sp->h + (boby >> 2);
		if (y < VIEWH - sp->h)
			y = VIEWH - sp->h;
		draw_sprite_patch(base_spr, x, y, 31, 0);
		if (flash_spr >= 0 && g_sprites[flash_spr]) {
			const sprite_t *fl = g_sprites[flash_spr];
			draw_sprite_patch(flash_spr, CENTERX - (fl->w / 2) + (bobx >> 2),
							  y - fl->h + 8, 31, 0);
		}
	}
}

void draw_hud(int health, int armor, int ammo, int weapon, int keys,
			  int dead)
{
	byte bg = g_coldark;
	byte edge = (byte)(g_coldark + 1);
	int y0 = VIEWH;
	scr_rect(0, y0, SCREENW, HUDH, bg);
	scr_rect(0, y0, SCREENW, 1, edge);
	/* ammo */
	scr_text(4, y0 + 3, "AMMO", g_colgold);
	{
		char buf[8];
		int n = ammo;
		int i = 7;
		buf[i] = 0;
		if (n == 0) buf[--i] = '0';
		while (n > 0 && i > 0) { buf[--i] = (char)('0' + n % 10); n /= 10; }
		scr_text_big(8, y0 + 13, &buf[i], g_colred, 2);
	}
	/* health */
	scr_text(64, y0 + 3, "HEALTH", g_colgold);
	{
		char buf[8];
		int n = health < 0 ? 0 : health;
		int i = 7;
		buf[i] = 0;
		buf[--i] = '%';
		if (n == 0) buf[--i] = '0';
		while (n > 0 && i > 0) { buf[--i] = (char)('0' + n % 10); n /= 10; }
		scr_text_big(64, y0 + 13, &buf[i], health < 25 ? g_colred : g_colwhite, 2);
	}
	/* face */
	{
		int spr = dead ? SPR_FACE_DEAD : SPR_FACE_OK;
		if (g_sprites[spr]) {
			const sprite_t *sp = g_sprites[spr];
			draw_sprite_patch(spr, CENTERX - sp->w / 2, y0 + 2, 31, 0);
		}
	}
	/* keys */
	scr_rect(226, y0 + 4, 8, 6, keys & 1 ? g_colblue : g_coldark);
	scr_rect(226, y0 + 13, 8, 6, keys & 2 ? g_colgold : g_coldark);
	scr_rect(226, y0 + 22, 8, 6, keys & 4 ? g_colred : g_coldark);
	/* armor */
	scr_text(248, y0 + 3, "ARMOR", g_colgold);
	{
		char buf[8];
		int n = armor;
		int i = 7;
		buf[i] = 0;
		buf[--i] = '%';
		if (n == 0) buf[--i] = '0';
		while (n > 0 && i > 0) { buf[--i] = (char)('0' + n % 10); n /= 10; }
		scr_text_big(248, y0 + 13, &buf[i], g_colgreen, 2);
	}
	(void)weapon;
}

void draw_message(const char *msg)
{
	if (msg && *msg)
		scr_text(2, 2, msg, g_colgold);
}
