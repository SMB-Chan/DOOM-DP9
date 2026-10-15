/*
 * DOOM EX-word (CY700) - core definitions
 * Embedded-WAD DOOM engine for CASIO EX-word XD-K4800 (DATAPLUS 9)
 * 528x320 RGB565 LCD, DOOM view 320x200 (8-bit paletted) centered on LCD.
 */
#ifndef DOOMDEF_H
#define DOOMDEF_H

typedef signed char sbyte;
typedef unsigned char byte;
typedef signed short int16t;
typedef unsigned short uint16t;
typedef signed int int32t;
typedef unsigned int uint32t;

/* Fixed point 16.16 */
typedef int fixed_t;
#define FRACBITS 16
#define FRACUNIT 0x10000
#define TOFIX(v) ((fixed_t)((v) * FRACUNIT))
#define FIX2F(f) ((f) / (double)65536.0)

/* Angles: 0..65535 = 0..360deg */
typedef unsigned short angle_t;
#define ANG2RAD(a) (((double)(a)) * 6.283185307179586 / 65536.0)

/* Screen */
#define SCREENW 320
#define SCREENH 200
#define VIEWW 320
#define VIEWH 168
#define HUDH 32
#define CENTERX (SCREENW / 2)
#define CENTERY (VIEWH / 2)

/* Player */
#define PLAYERRADIUS 16
#define EYEHEIGHT 41
#define MELEERANGE 64

/* Map limits */
#define MAXVERTEXES 2048
#define MAXSEGS 4096
#define MAXSSECTORS 1024
#define MAXNODES 1024
#define MAXSECTORS 256
#define MAXLINEDEFS 1024
#define MAXSIDEDEFS 1536
#define MAXTHINGS 320
#define MAXVISPLANES 64
#define MAXDOORS 96
#define MAXLUMP 96

/* Wall texture kinds (procedurally generated 64x64 textures) */
enum {
	WK_PLAIN,		/* flat mid-gray tech */
	WK_STARTAN,		/* tan tech panels */
	WK_STARG,		/* green-gray stone blocks */
	WK_BROWN,		/* brown tech */
	WK_GSTONE,		/* gray stone blocks */
	WK_METAL,		/* dark metal / support */
	WK_DOOR,		/* door */
	WK_DOORTRACK,	/* door track */
	WK_STEP,		/* step / small */
	WK_COMP,		/* computer panel */
	WK_EXIT,		/* exit switch face */
	WK_SKIN,		/* marble / skin */
	WK_COUNT
};

/* Flat ids */
enum {
	FLAT_NONE = -1,
	FLAT_FLOOR4_8 = 0,
	FLAT_FLOOR7_1,
	FLAT_FLAT14,
	FLAT_CEIL3_5,
	FLAT_NUKAGE3,
	FLAT_COUNT,
	FLAT_SKY = 127
};

/* Game states */
enum {
	GS_TITLE,
	GS_PLAY,
	GS_DEAD,
	GS_INTERMISSION,
	GS_HELP
};

/* Thing states */
enum {
	TH_INACTIVE,
	TH_WALK,
	TH_ATTACK,
	TH_PAIN,
	TH_DEAD,
	TH_CORPSE
};

/* Weapons */
enum {
	WP_FIST,
	WP_PISTOL,
	WP_SHOTGUN,
	WP_COUNT
};

typedef struct {
	const char *name;	/* lump name */
	int type;
	int hp;
	int speed;			/* fixed units/tic */
	int radius;
	int damage_min, damage_max;
	int attack_range;	/* fixed */
	int attack_tics;	/* cooldown */
	int flags;
} mobjinfo_t;

/* thing flags */
#define TF_SHOOTABLE 1
#define TF_SOLID 2
#define TF_COUNTKILL 4
#define TF_PICKUP 8

typedef struct {
	int type;			/* index into mobj table or -1 for decorative/item */
	int x, y;			/* fixed */
	int angle;
	int hp;
	int state;
	int animtic;
	int atkcool;
	int alert;
	int itemkind;		/* for pickups */
	int alive;
} thing_t;

typedef struct {
	int floorz, ceilz;	/* fixed */
	sbyte floorpic;		/* flat id */
	sbyte ceilpic;		/* flat id or FLAT_SKY */
	byte light;
	byte special;
	short tag;
	short soundwarn;
} sector_t;

typedef struct {
	int x, y;			/* fixed vertex */
} vertex_t;

typedef struct {
	short v1, v2;
	short flags;
	short special;
	short tag;
	short sidenum[2];
	int bx0, by0, bx1, by1;	/* bbox fixed */
	byte specialkind;		/* classified special for game logic */
} linedef_t;

typedef struct {
	short sector;
	short xoff;
	short yoff;
	byte wk_up, wk_lo, wk_mid;	/* wall kinds */
	byte hasmid;
} sidedef_t;

typedef struct {
	short v1, v2;
	short linedef;
	short segofs;
	short seglen16;
	short udx14, udy14;
	byte dir;
} seg_t;

typedef struct {
	short numsegs, firstseg;
} subsector_t;

typedef struct {
	int x, y, dx, dy;	/* fixed */
	int bbox[2][4];
	int children[2];
} node_t;

/* Door */
typedef struct {
	short sector;
	byte state;		/* 0 closed 1 opening 2 open 3 closing */
	byte kind;		/* 0 normal DR, 1 open-stay (D1) */
	byte locked;	/* 0 none, 2 blue 3 yellow 6 red (doom key bitfield) */
	int timer;
	int ceilpos;	/* target ceiling when open */
} door_t;

#endif /* DOOMDEF_H */
