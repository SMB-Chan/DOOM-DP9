#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "wad.h"
#include "embedded_wad.h"

static unsigned char *g_wad = 0;

/* little-endian readers */
static int strncmp_local(const char *a, const char *b, int n)
{
	while (n > 0 && *a && *a == *b) {
		a++;
		b++;
		n--;
	}
	if (n == 0)
		return 0;
	return (unsigned char)*a - (unsigned char)*b;
}

static short rd16(const unsigned char *p) { return (short)(p[0] | (p[1] << 8)); }
static int rd32(const unsigned char *p)
{
	return (int)((unsigned int)p[0] | ((unsigned int)p[1] << 8) |
				 ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24));
}

/* LZSS decode: flag byte, bit=1 -> 2-byte code ((off-1)<<4)|(len-3),
 * bit=0 -> literal. Window 4096, len 3..18. */
static int lzss_decode(const unsigned char *src, int srclen, unsigned char *dst, int dstlen)
{
	int i = 0, n = 0;
	while (n < dstlen) {
		unsigned char flags;
		int bit;
		if (i >= srclen)
			return -1;
		flags = src[i++];
		for (bit = 0; bit < 8 && n < dstlen; bit++) {
			if (flags & (1 << bit)) {
				int v, off, len, k;
				if (i + 1 >= srclen)
					return -1;
				v = src[i] | (src[i + 1] << 8);
				i += 2;
				off = (v >> 4) + 1;
				len = (v & 0xF) + 3;
				for (k = 0; k < len && n < dstlen; k++) {
					dst[n] = dst[n - off];
					n++;
				}
			} else {
				if (i >= srclen)
					return -1;
				dst[n++] = src[i++];
			}
		}
	}
	return n == dstlen ? 0 : -1;
}

typedef struct {
	char name[9];
	const unsigned char *data;
	int size;
} lumpref_t;

static lumpref_t g_lumps[MAXLUMP];
static int g_numlumps = 0;

const unsigned char *wad_lump(const char *name, int *size_out)
{
	int i;
	for (i = 0; i < g_numlumps; i++) {
		if (strcmp(g_lumps[i].name, name) == 0) {
			if (size_out)
				*size_out = g_lumps[i].size;
			return g_lumps[i].data;
		}
	}
	return 0;
}

const unsigned char *g_palette = 0;
const unsigned char *g_colormap = 0;

int numvertexes, numsegs, numssectors, numnodes, numsectors,
	numlinedefs, numsidedefs, numthings;
vertex_t *vertexes;
seg_t *segs;
subsector_t *ssectors;
node_t *nodes;
sector_t *sectors;
linedef_t *linedefs;
sidedef_t *sidedefs;
rawthing_t *mapthings;

int flat_by_name(const unsigned char *name8)
{
	/* only the embedded flats exist; anything else -> special-cased */
	static const char *names[FLAT_COUNT] = {
		"FLOOR4_8", "FLOOR7_1", "FLAT14", "CEIL3_5", "NUKAGE3"
	};
	int f;
	if (name8[0] == 'F' && strncmp_local((const char *)name8, "F_SKY1", 6) == 0)
		return FLAT_SKY;
	for (f = 0; f < FLAT_COUNT; f++) {
		if (strncmp_local((const char *)name8, names[f], 8) == 0)
			return f;
	}
	return FLAT_NONE;
}

static int isqrt32_wad(unsigned int v)
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

/* wall kind from texture name (walls are procedurally textured) */
static byte wall_kind(const unsigned char *name8)
{
	const char *s = (const char *)name8;
	if (s[0] == '-')
		return WK_PLAIN;
	if (strncmp_local(s, "STARTAN", 7) == 0) return WK_STARTAN;
	if (strncmp_local(s, "STARG", 5) == 0 || strncmp_local(s, "STARGR", 6) == 0) return WK_STARG;
	if (strncmp_local(s, "STONE", 5) == 0) return WK_GSTONE;
	if (strncmp_local(s, "GST", 3) == 0) return WK_GSTONE;
	if (strncmp_local(s, "BROWN", 5) == 0 || strncmp_local(s, "BRODIS", 6) == 0) return WK_BROWN;
	if (strncmp_local(s, "DOOR", 4) == 0) return WK_DOOR;
	if (strncmp_local(s, "DOORTRAK", 8) == 0 || strncmp_local(s, "DOORSTOP", 8) == 0) return WK_DOORTRACK;
	if (strncmp_local(s, "STEP", 4) == 0) return WK_STEP;
	if (strncmp_local(s, "COMP", 4) == 0 || strncmp_local(s, "TEKWALL", 7) == 0 ||
		strncmp_local(s, "COMPUTE", 7) == 0 || strncmp_local(s, "LITE", 4) == 0) return WK_COMP;
	if (strncmp_local(s, "SUPPORT", 7) == 0 || strncmp_local(s, "MIDGRATE", 8) == 0 ||
		strncmp_local(s, "BIGDOOR", 7) == 0) return WK_METAL;
	if (strncmp_local(s, "EXITSIGN", 8) == 0 || strncmp_local(s, "EXIT", 4) == 0 ||
		strncmp_local(s, "SW1", 3) == 0 || strncmp_local(s, "SW2", 3) == 0) return WK_EXIT;
	if (strncmp_local(s, "MARBLE", 6) == 0 || strncmp_local(s, "SKIN", 4) == 0 ||
		strncmp_local(s, "SP_FACE", 7) == 0) return WK_SKIN;
	return WK_PLAIN;
}

int wad_init(void)
{
	int rc, i, nlumps, dirofs;
	const unsigned char *p;

	g_wad = (unsigned char *)malloc(EMBEDDED_WAD_RAW_SIZE);
	if (!g_wad)
		return -1;
	rc = lzss_decode(g_embedded_wad_lzss, EMBEDDED_WAD_COMP_SIZE,
					 g_wad, EMBEDDED_WAD_RAW_SIZE);
	if (rc != 0)
		return -2;

	if (g_wad[0] != 'I' || g_wad[1] != 'W' || g_wad[2] != 'A' || g_wad[3] != 'D')
		return -3;
	nlumps = rd16(g_wad + 4);
	if (nlumps > MAXLUMP)
		nlumps = MAXLUMP;
	dirofs = rd32(g_wad + 8);
	for (i = 0; i < nlumps; i++) {
		const unsigned char *e = g_wad + dirofs + i * 16;
		int pos = rd32(e), size = rd32(e + 4);
		int k;
		for (k = 0; k < 8; k++) {
			char c = (char)e[8 + k];
			if (c == 0)
				break;
			g_lumps[g_numlumps].name[k] = c;
		}
		g_lumps[g_numlumps].name[k] = 0;
		g_lumps[g_numlumps].data = g_wad + pos;
		g_lumps[g_numlumps].size = size;
		g_numlumps++;
	}

	g_palette = wad_lump("PLAYPAL", 0);
	g_colormap = wad_lump("COLORMAP", 0);
	if (!g_palette || !g_colormap)
		return -4;

	/* ---- map ---- */
	p = wad_lump("VERTEXES", &numvertexes);
	numvertexes /= 4;
	if (!p || numvertexes > MAXVERTEXES) return -5;
	vertexes = (vertex_t *)malloc(sizeof(vertex_t) * numvertexes);
	/* map coords are SHORTS in units (short-format WAD variant) */
	for (i = 0; i < numvertexes; i++) {
		vertexes[i].x = rd16(p + i * 4) << FRACBITS;
		vertexes[i].y = rd16(p + i * 4 + 2) << FRACBITS;
	}

	p = wad_lump("LINEDEFS", &numlinedefs);
	numlinedefs /= 14;
	if (!p || numlinedefs > MAXLINEDEFS) return -6;
	linedefs = (linedef_t *)malloc(sizeof(linedef_t) * numlinedefs);
	for (i = 0; i < numlinedefs; i++) {
		const unsigned char *l = p + i * 14;
		linedef_t *ld = &linedefs[i];
		ld->v1 = rd16(l);
		ld->v2 = rd16(l + 2);
		ld->flags = rd16(l + 4);
		ld->special = rd16(l + 6);
		ld->tag = rd16(l + 8);
		ld->sidenum[0] = rd16(l + 10);
		ld->sidenum[1] = rd16(l + 12);
		ld->specialkind = 0;
		{
			int x0 = vertexes[ld->v1].x, y0 = vertexes[ld->v1].y;
			int x1 = vertexes[ld->v2].x, y1 = vertexes[ld->v2].y;
			if (x0 < x1) { ld->bx0 = x0; ld->bx1 = x1; } else { ld->bx0 = x1; ld->bx1 = x0; }
			if (y0 < y1) { ld->by0 = y0; ld->by1 = y1; } else { ld->by0 = y1; ld->by1 = y0; }
		}
	}

	p = wad_lump("SIDEDEFS", &numsidedefs);
	numsidedefs /= 30;
	if (!p || numsidedefs > MAXSIDEDEFS) return -7;
	sidedefs = (sidedef_t *)malloc(sizeof(sidedef_t) * numsidedefs);
	for (i = 0; i < numsidedefs; i++) {
		const unsigned char *s = p + i * 30;
		sidedefs[i].sector = rd16(s + 28);
		sidedefs[i].xoff = rd16(s);
		sidedefs[i].yoff = rd16(s + 2);
		sidedefs[i].wk_up = wall_kind(s + 4);
		sidedefs[i].wk_lo = wall_kind(s + 12);
		sidedefs[i].wk_mid = wall_kind(s + 20);
		sidedefs[i].hasmid = (s[20] != '-' && s[20] != 0);
	}

	p = wad_lump("SEGS", &numsegs);
	numsegs /= 12;
	if (!p || numsegs > MAXSEGS) return -8;
	segs = (seg_t *)malloc(sizeof(seg_t) * numsegs);
	for (i = 0; i < numsegs; i++) {
		const unsigned char *s = p + i * 12;
		int v1 = rd16(s), v2 = rd16(s + 2);
		int sx = vertexes[v2].x - vertexes[v1].x;
		int sy = vertexes[v2].y - vertexes[v1].y;
		int dx4 = sx >> 14, dy4 = sy >> 14;
		int sl16 = isqrt32_wad((unsigned int)(dx4 * dx4 + dy4 * dy4)) << 2;
		if (sl16 <= 0) sl16 = 1;
		segs[i].v1 = (short)v1;
		segs[i].v2 = (short)v2;
		segs[i].linedef = rd16(s + 6);
		segs[i].dir = (byte)rd16(s + 8);
		segs[i].segofs = rd16(s + 10);
		segs[i].seglen16 = (short)sl16;
		segs[i].udx14 = (short)((sx << 2) / sl16);
		segs[i].udy14 = (short)((sy << 2) / sl16);
	}

	p = wad_lump("SSECTORS", &numssectors);
	numssectors /= 4;
	if (!p || numssectors > MAXSSECTORS) return -9;
	ssectors = (subsector_t *)malloc(sizeof(subsector_t) * numssectors);
	for (i = 0; i < numssectors; i++) {
		ssectors[i].numsegs = rd16(p + i * 4);
		ssectors[i].firstseg = rd16(p + i * 4 + 2);
	}

	p = wad_lump("NODES", &numnodes);
	numnodes /= 28;
	if (!p || numnodes > MAXNODES) return -10;
	nodes = (node_t *)malloc(sizeof(node_t) * numnodes);
	/* short-format NODES: x,y,dx,dy as shorts, 8 short bbox (top,bot,left,right in units),
	 * 2 short children (bit15 = subsector) */
	for (i = 0; i < numnodes; i++) {
		const unsigned char *n = p + i * 28;
		node_t *nd = &nodes[i];
		nd->x = rd16(n) << FRACBITS;
		nd->y = rd16(n + 2) << FRACBITS;
		nd->dx = rd16(n + 4) << FRACBITS;
		nd->dy = rd16(n + 6) << FRACBITS;
		nd->bbox[0][0] = rd16(n + 8);	/* top (ymax) */
		nd->bbox[0][1] = rd16(n + 10);	/* bottom (ymin) */
		nd->bbox[0][2] = rd16(n + 12);	/* left (xmin) */
		nd->bbox[0][3] = rd16(n + 14);	/* right (xmax) */
		nd->bbox[1][0] = rd16(n + 16);
		nd->bbox[1][1] = rd16(n + 18);
		nd->bbox[1][2] = rd16(n + 20);
		nd->bbox[1][3] = rd16(n + 22);
		nd->children[0] = rd16(n + 24) & 0xFFFF;
		nd->children[1] = rd16(n + 26) & 0xFFFF;
	}

	p = wad_lump("SECTORS", &numsectors);
	numsectors /= 26;
	if (!p || numsectors > MAXSECTORS) return -11;
	sectors = (sector_t *)malloc(sizeof(sector_t) * numsectors);
	for (i = 0; i < numsectors; i++) {
		const unsigned char *s = p + i * 26;
		sector_t *sec = &sectors[i];
		sec->floorz = rd16(s) << FRACBITS;
		sec->ceilz = rd16(s + 2) << FRACBITS;
		sec->floorpic = (sbyte)flat_by_name(s + 4);
		sec->ceilpic = (sbyte)flat_by_name(s + 12);
		sec->light = (byte)rd16(s + 20);
		sec->special = (byte)rd16(s + 22);
		sec->tag = rd16(s + 24);
		sec->soundwarn = 0;
		if (sec->floorpic == FLAT_NONE) sec->floorpic = FLAT_FLOOR4_8;
	}

	p = wad_lump("THINGS", &numthings);
	numthings /= 10;
	if (!p) return -12;
	{
		int cnt = 0;
		mapthings = (rawthing_t *)malloc(sizeof(rawthing_t) * (numthings > MAXTHINGS ? MAXTHINGS : numthings));
		for (i = 0; i < numthings && cnt < MAXTHINGS; i++) {
			const unsigned char *t = p + i * 10;
			int flags = rd16(t + 8);
			if ((flags & 7) == 0)
				continue;	/* not in any single-player skill */
			mapthings[cnt].x = rd16(t);
			mapthings[cnt].y = rd16(t + 2);
			mapthings[cnt].angle = rd16(t + 4);
			mapthings[cnt].type = rd16(t + 6);
			mapthings[cnt].flags = flags;
			cnt++;
		}
		numthings = cnt;
	}

	/* no REJECT/BLOCKMAP lumps were embedded; renderer/game do not need them */
	return 0;
}

/* ---------------- sprites ---------------- */

#define MAXSPRITES 48

typedef struct {
	const char *name;
	int index;
} sprdef_t;

static const sprdef_t sprite_names[] = {
	{ "PISFA0", 0 }, { "PISGA0", 1 }, { "PISGB0", 2 },
	{ "SHTFA0", 3 }, { "SHTGA0", 4 }, { "SHTGB0", 5 },
	{ "CHGGA0", 6 }, { "MISGA0", 7 }, { "PUNGA0", 8 },
	{ "POSSA1", 9 }, { "POSSB1", 10 }, { "POSSH0", 11 }, { "POSSL0", 12 },
	{ "SPOSA1", 13 }, { "SPOSB1", 14 }, { "SPOSH0", 15 }, { "SPOSL0", 16 },
	{ "TROOA1", 17 }, { "TROOB1", 18 }, { "TROOM0", 19 },
	{ "SARGA1", 20 }, { "SARGB1", 21 }, { "SARGN0", 22 },
	{ "BAR1A0", 23 }, { "BEXPA0", 24 },
	{ "STIMA0", 25 }, { "MEDIA0", 26 }, { "ARM1A0", 27 }, { "ARM2A0", 28 },
	{ "CLIPA0", 29 }, { "SHELA0", 30 }, { "SHOTA0", 31 },
	{ "BKEYA0", 32 }, { "YKEYA0", 33 }, { "RKEYA0", 34 },
	{ "BON1A0", 35 }, { "BON2A0", 36 },
	{ "STFST01", 37 }, { "STFDEAD0", 38 }, { "COLUA0", 39 },
	{ 0, -1 }
};

/* semantic sprite ids */
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

static const char *semantic_names[SPR_COUNT] = {
	"PISFA0", "PISGA0", "PISGB0",
	"SHTFA0", "SHTGA0", "SHTGB0",
	"CHGGA0", "MISGA0", "PUNGA0",
	"POSSA1", "POSSB1", "POSSH0", "POSSL0",
	"SPOSA1", "SPOSB1", "SPOSH0", "SPOSL0",
	"TROOA1", "TROOB1", "TROOM0",
	"SARGA1", "SARGB1", "SARGN0",
	"BAR1A0", "BEXPA0",
	"STIMA0", "MEDIA0", "ARM1A0", "ARM2A0",
	"CLIPA0", "SHELA0", "SHOTA0",
	"BKEYA0", "YKEYA0", "RKEYA0",
	"BON1A0", "BON2A0",
	"STFST01", "STFDEAD0", "COLUA0"
};

static sprite_t g_sprstore[MAXSPRITES];
sprite_t *g_sprites[MAXSPRITES];

int sprite_index(const char *name)
{
	int i;
	for (i = 0; i < SPR_COUNT; i++)
		if (strcmp(semantic_names[i], name) == 0)
			return i;
	return SPR_NONE;
}

byte *g_flats[FLAT_COUNT];

void wad_load_sprites(void)
{
	int i, k;
	for (k = 0; k < SPR_COUNT; k++) {
		int sz;
		const unsigned char *p = wad_lump(semantic_names[k], &sz);
		g_sprites[k] = 0;
		if (!p || sz < 8)
			continue;
		{
			int w = rd16(p), h = rd16(p + 2);
			sprite_t *sp;
			if (w <= 0 || h <= 0 || w > 256 || h > 200)
				continue;
			sp = &g_sprstore[k];
			sp->w = (short)w;
			sp->h = (short)h;
			sp->left = rd16(p + 4);
			sp->top = rd16(p + 6);
			sp->pix = (byte *)malloc(w * h);
			if (!sp->pix)
				continue;
			memset(sp->pix, 0, w * h);
			{
				const unsigned char *cofs = p + 8;
				int x;
				for (x = 0; x < w; x++) {
					int colofs = rd32(cofs + x * 4);
					const unsigned char *cp;
					if (colofs <= 0 || colofs >= sz)
						continue;
					cp = p + colofs;
					while (*cp != 0xFF) {
						int topd = cp[0];
						int len = cp[1];
						int j;
						if (topd + len > h)
							break;
						for (j = 0; j < len; j++)
							sp->pix[x * h + topd + j] = cp[3 + j];
						cp += len + 4;
					}
				}
			}
			g_sprites[k] = sp;
		}
	}

	for (i = 0; i < FLAT_COUNT; i++)
		g_flats[i] = 0;
	{
		static const char *fnames[FLAT_COUNT] = {
			"FLOOR4_8", "FLOOR7_1", "FLAT14", "CEIL3_5", "NUKAGE3"
		};
		for (i = 0; i < FLAT_COUNT; i++) {
			int sz;
			const unsigned char *p = wad_lump(fnames[i], &sz);
			if (p && sz >= 4096) {
				g_flats[i] = (byte *)malloc(4096);
				memcpy(g_flats[i], p, 4096);
			}
		}
	}
	(void)k;
	(void)sprite_names;
}
