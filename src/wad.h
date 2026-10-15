#ifndef WAD_H
#define WAD_H

#include "doomdef.h"

/* Decompressed embedded WAD access.
 * All multi-byte values in the WAD are little-endian; the SH host is
 * big-endian, so every read goes through rd16/rd32. */

int wad_init(void);			/* decompress + index lumps; 0 on success */
const unsigned char *wad_lump(const char *name, int *size_out);

/* Parsed map (E1M1) */
extern int numvertexes, numsegs, numssectors, numnodes, numsectors,
	numlinedefs, numsidedefs, numthings;
extern vertex_t *vertexes;
extern seg_t *segs;
extern subsector_t *ssectors;
extern node_t *nodes;
extern sector_t *sectors;
extern linedef_t *linedefs;
extern sidedef_t *sidedefs;

/* Map things, raw */
typedef struct {
	short x, y, angle, type, flags;
} rawthing_t;
extern rawthing_t *mapthings;

/* Palette / colormap */
extern const unsigned char *g_palette;	/* 256*3 RGB */
extern const unsigned char *g_colormap;	/* 34*256 */

/* Sprites: patch converted to column-major 8-bit mask (0 = transparent) */
typedef struct {
	short w, h;
	short left, top;
	byte *pix;		/* w*h, column-major: pix[x*h + y] */
} sprite_t;
extern sprite_t *g_sprites[];
int sprite_index(const char *name);	/* -1 if missing; resolves at load */
void wad_load_sprites(void);

/* Flats: 64x64 bytes, FLAT_COUNT entries; NULL where absent */
extern byte *g_flats[FLAT_COUNT];
int flat_by_name(const unsigned char *name8);	/* FLAT_NONE if unknown */

#endif
