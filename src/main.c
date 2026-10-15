/* DOOM EX-word - platform + entry (CY700 / XD-K4800)
 * Device: TMU0 timing, keypad input, LCD blit to 528x320 VRAM.
 * DOOM_HOST_TEST: headless harness that dumps PPM frames. */
#include <stdlib.h>
#include <string.h>
#include "doomdef.h"
#include "wad.h"
#include "render.h"
#include "game.h"

#ifdef DOOM_HOST_TEST
#include <stdio.h>
#endif

#ifndef DOOM_HOST_TEST
#include <sh4a/input/keypad.h>
#include <sh4a/input/exword_keys.h>
#include <graphics/lcdc.h>
#include <syscalls/syscalls.h>
#include "font8x8.h"

/* ---- CPU clock turbo (exact CY700_OS.BIN 0x8c0807d0 sequence) ---- */
#define REG_FRQCR  (*(volatile unsigned long *)0xA4150000UL)
#define REG_LSTATUS (*(volatile unsigned long *)0xA4150060UL)
static unsigned long g_saved_frqcr = 0;

static void cpg_turbo_on(void)
{
	unsigned long v = REG_FRQCR;
	g_saved_frqcr = v;
	v = (v & 0x7F0FFFFFUL) | 0x00100000UL;
	REG_FRQCR = v;
	REG_FRQCR = v | 0x80000000UL;
	while (REG_LSTATUS & 1UL)
		;
}

static void cpg_turbo_off(void)
{
	if (g_saved_frqcr) {
		unsigned long v = (REG_FRQCR & 0x7F0FFFFFUL) | (g_saved_frqcr & 0x00F00000UL);
		REG_FRQCR = v;
		REG_FRQCR = v | 0x80000000UL;
		while (REG_LSTATUS & 1UL)
			;
	}
}

/* ---- video ---- */

static uint16t g_pal565[256];

static void video_init(void)
{
	int i;
	uint32t *vram32 = (uint32t *)lcdc_get_vram_address();
	for (i = 0; i < 256; i++)
		g_pal565[i] = (uint16t)(((g_palette[i * 3] & 0xF8) << 8) |
								 ((g_palette[i * 3 + 1] & 0xFC) << 3) |
								 (g_palette[i * 3 + 2] >> 3));
	{
		/* clear the whole LCD once using 32-bit stores */
		int n = (528 * 320) / 2;
		for (i = 0; i < n; i++)
			vram32[i] = 0x00000000UL;
	}
}

/* Blit 320x200 paletted framebuffer scaled 1.5x to 480x300 centered on 528x320 LCD
 * using 32-bit aligned stores (4 src pixels -> 6 dst pixels = 3 uint32 stores). */
static void blit_frame(void)
{
	uint16t *vram = (uint16t *)lcdc_get_vram_address();
	const int x0 = (528 - 480) / 2;	/* 24 (even -> 4-byte aligned) */
	const int y0 = (320 - 300) / 2;	/* 10 */
	int sy, dy = y0;
	for (sy = 0; sy < SCREENH; sy += 2) {
		const byte *src0 = g_fb + sy * SCREENW;
		const byte *src1 = src0 + SCREENW;
		uint32t *dst0 = (uint32t *)(vram + dy * 528 + x0);
		uint32t *dst1 = dst0 + (528 / 2);
		uint32t *dst2 = dst1 + (528 / 2);
		int sx;
		for (sx = 0; sx < SCREENW; sx += 4) {
			uint32t a0 = g_pal565[src0[sx]];
			uint32t a1 = g_pal565[src0[sx + 1]];
			uint32t a2 = g_pal565[src0[sx + 2]];
			uint32t a3 = g_pal565[src0[sx + 3]];
			uint32t b0 = g_pal565[src1[sx]];
			uint32t b1 = g_pal565[src1[sx + 1]];
			uint32t b2 = g_pal565[src1[sx + 2]];
			uint32t b3 = g_pal565[src1[sx + 3]];
			uint32t w0 = (a0 << 16) | a0;
			uint32t w1 = (a1 << 16) | a2;
			uint32t w2 = (a2 << 16) | a3;
			uint32t w3 = (b0 << 16) | b0;
			uint32t w4 = (b1 << 16) | b2;
			uint32t w5 = (b2 << 16) | b3;
			dst0[0] = w0;
			dst0[1] = w1;
			dst0[2] = w2;
			dst1[0] = w0;
			dst1[1] = w4;
			dst1[2] = w2;
			dst2[0] = w3;
			dst2[1] = w4;
			dst2[2] = w5;
			dst0 += 3;
			dst1 += 3;
			dst2 += 3;
		}
		dy += 3;
	}
	lcdc_copy_vram();
}

/* ---- input ---- */

static byte prev_use, prev_wnext, prev_back, prev_map, prev_god, prev_kfa;

static void poll_input(void)
{
	/* Call keypad_read() 4x to immediately latch through libsh4a's 2-bit debounce counter */
	keypad_read();
	keypad_read();
	keypad_read();
	keypad_read();
	memset(&g_input, 0, sizeof(g_input));
	if (get_key_state(KEY_CHAR_W) || get_key_state(KEY_UP))
		g_input.forward = 1;
	if (get_key_state(KEY_CHAR_S) || get_key_state(KEY_DOWN))
		g_input.forward = -1;
	if (get_key_state(KEY_CHAR_A))
		g_input.sidestep = -1;
	if (get_key_state(KEY_CHAR_D))
		g_input.sidestep = 1;
	if (get_key_state(KEY_LEFT) || get_key_state(KEY_CHAR_Q))
		g_input.turn = 1;		/* turn left (CCW) */
	if (get_key_state(KEY_RIGHT) || get_key_state(KEY_CHAR_P))
		g_input.turn = -1;		/* turn right (CW) */
	if (get_key_state(KEY_SHIFT) || get_key_state(KEY_CHAR_R))
		g_input.run = 1;
	if (get_key_state(KEY_ENTER) || get_key_state(KEY_CHAR_F) || get_key_state(KEY_CHAR_Z))
		g_input.fire = 1;
	if (get_key_state(KEY_BACKSPACE) || get_key_state(KEY_CHAR_E) || get_key_state(KEY_CHAR_X)) {
		if (!prev_use)
			g_input.use = 1;
		prev_use = 1;
	} else
		prev_use = 0;
	if (get_key_state(KEY_SYMBOL) || get_key_state(KEY_PAGE_UP) || get_key_state(KEY_PAGE_DOWN)) {
		if (!prev_wnext)
			g_input.weapon_next = 1;
		prev_wnext = 1;
	} else
		prev_wnext = 0;
	if (get_key_state(KEY_ZOOM) || get_key_state(KEY_CHAR_M) || get_key_state(KEY_JUMP)) {
		if (!prev_map)
			g_input.automap_toggle = 1;
		prev_map = 1;
	} else
		prev_map = 0;
	if (get_key_state(KEY_CHAR_G)) {
		if (!prev_god)
			g_input.god_toggle = 1;
		prev_god = 1;
	} else
		prev_god = 0;
	if (get_key_state(KEY_CHAR_V)) {
		if (!prev_kfa)
			g_input.idkfa = 1;
		prev_kfa = 1;
	} else
		prev_kfa = 0;
	if (get_key_state(KEY_BACK)) {
		if (!prev_back) {
			if (g_state == GS_PLAY)
				g_state = GS_TITLE;
			else
				g_input.quit = 1;
		}
		prev_back = 1;
	} else
		prev_back = 0;
	if (get_key_state(KEY_POWER))
		g_input.quit = 1;
}
#endif /* !DOOM_HOST_TEST */

/* ---------------- screens ---------------- */

static void draw_title(int blink)
{
	scr_fill(0);
	scr_text_big(96, 40, "DOOM", g_colred, 4);
	scr_text_center(96, "EX-WORD EDITION", g_colgold);
	scr_text_center(116, "E1M1 - HANGAR", g_colwhite);
	if (blink)
		scr_text_center(140, "PRESS ENTER TO START", g_colwhite);
	scr_text_center(172, "W/S MOVE  A/D STRAFE  ARROWS TURN", g_colgray);
	scr_text_center(182, "ENTER FIRE  BKSP USE  SHIFT RUN", g_colgray);
	scr_text_center(192, "SYMBOL WEAPON  BACK TITLE  POWER QUIT", g_colgray);
}

static void draw_intermission(void)
{
	char buf[40];
	scr_fill(0);
	scr_text_big(72, 32, "LEVEL COMPLETE", g_colgold, 2);
	scr_text_center(72, "", g_colwhite);
	{

		{
			int v = g_kills, i = 7;
			char t[8];
			t[7] = 0;
			if (v == 0) t[--i] = '0';
			while (v > 0 && i > 0) { t[--i] = (char)('0' + v % 10); v /= 10; }
			strcpy(buf, "KILLS: ");
			strcat(buf, &t[i]);
		}
		{
			int v = g_kills_total, i = 7;
			char t[8];
			t[7] = 0;
			if (v == 0) t[--i] = '0';
			while (v > 0 && i > 0) { t[--i] = (char)('0' + v % 10); v /= 10; }
			strcat(buf, " / ");
			strcat(buf, &t[i]);
		}
		scr_text_center(96, buf, g_colwhite);
		{
			int v = g_items, i = 7;
			char t[8];
			t[7] = 0;
			if (v == 0) t[--i] = '0';
			while (v > 0 && i > 0) { t[--i] = (char)('0' + v % 10); v /= 10; }
			strcpy(buf, "ITEMS: ");
			strcat(buf, &t[i]);
			v = g_items_total;
			i = 7;
			t[7] = 0;
			if (v == 0) t[--i] = '0';
			while (v > 0 && i > 0) { t[--i] = (char)('0' + v % 10); v /= 10; }
			strcat(buf, " / ");
			strcat(buf, &t[i]);
		}
		scr_text_center(112, buf, g_colwhite);
	}
	scr_text_center(150, "THE HALLOWED HALLS OF THE UAC", g_colgray);
	scr_text_center(170, "PRESS ENTER TO PLAY AGAIN", g_colwhite);
}

/* ---------------- game states ---------------- */

static int g_prev_fire = 0;

static void handle_state_input(void)
{
#ifdef DOOM_HOST_TEST
	return;		/* harness drives states directly */
#else
	if (g_state == GS_TITLE) {
		if (g_input.fire && !g_prev_fire) {
			game_start();
		}
	} else if (g_state == GS_PLAY) {
		if (g_exit_result) {
			g_state = GS_INTERMISSION;
			g_exit_result = 0;
		}
	} else if (g_state == GS_INTERMISSION) {
		if (g_input.fire && !g_prev_fire) {
			g_state = GS_TITLE;
		}
	}
	g_prev_fire = g_input.fire;
#endif
}

/* ---------------- entry ---------------- */

#ifdef DOOM_HOST_TEST

static void dump_ppm(const char *path)
{
	FILE *f = fopen(path, "wb");
	int i;
	fprintf(f, "P6\n320 200\n255\n");
	for (i = 0; i < SCREENW * SCREENH; i++) {
		byte c = g_fb[i];
		fputc(g_palette[c * 3], f);
		fputc(g_palette[c * 3 + 1], f);
		fputc(g_palette[c * 3 + 2], f);
	}
	fclose(f);
}

int main(int argc, char **argv)
{
	int i;
	(void)argc; (void)argv;
	if (wad_init() != 0) {
		fprintf(stderr, "wad_init failed\n");
		return 1;
	}
	render_init();
	wad_load_sprites();
	game_load();

	draw_title(1);
	dump_ppm("/tmp/doom_title.ppm");

	game_start();
	for (i = 0; i < 40; i++) {
		g_input.forward = 1;
		game_update();
	}
	g_input.forward = 0;
	game_render();
	dump_ppm("/tmp/doom_walk.ppm");

	for (i = 0; i < 60; i++) {
		g_input.turn = 1;
		game_update();
	}
	g_input.turn = 0;
	for (i = 0; i < 30; i++) {
		g_input.forward = 1;
		game_update();
	}
	g_input.forward = 0;
	game_render();
	dump_ppm("/tmp/doom_turn.ppm");

	g_input.fire = 1;
	game_update();
	g_input.fire = 0;
	game_update();
	game_render();
	dump_ppm("/tmp/doom_fire.ppm");

	{
		extern int numthings;
		printf("host test done: %d map things\n", numthings);
	}
	return 0;
}

#else /* device */

#include "../memmgr.h"

/* raw VRAM error display, independent of heap/render init */
static void fatal_msg(const char *s)
{
	volatile uint16t *vram = (volatile uint16t *)lcdc_get_vram_address();
	int x0 = 100, y0 = 150;
	int cx = x0, cy = y0, i;
	for (i = 0; i < 528 * 320; i++)
		vram[i] = 0x0000;
	while (*s) {
		char ch = *s++;
		if (ch == '\n') {
			cy += 8;
			cx = x0;
			continue;
		}
		if (ch < 32 || ch > 126)
			ch = '?';
		{
			const unsigned char *g = font8x8[ch - 32];
			int r, b;
			for (r = 0; r < 8; r++) {
				unsigned char bits = g[r];
				for (b = 0; b < 8; b++)
					if (bits & (1 << b)) {
						int px = cx + b, py = cy + r;
						if (px >= 0 && px < 528 && py >= 0 && py < 320)
							vram[py * 528 + px] = 0xFFFF;
					}
			}
		}
		cx += 8;
	}
	lcdc_copy_vram();
	for (;;) {
		keypad_read();
		if (get_key_state(KEY_BACK) || get_key_state(KEY_POWER))
			break;
	}
}

int main(void *ptr)
{
	int frame_count = 0;
	(void)ptr;

	memmgr_init();	/* heap MUST be initialized before any malloc */
	cpg_turbo_on();

	if (wad_init() != 0) {
		cpg_turbo_off();
		fatal_msg("DOOMX: WAD INIT FAILED");
		return -2;
	}
	render_init();
	if (!g_fb) {
		cpg_turbo_off();
		fatal_msg("DOOMX: HEAP LOW");
		return -2;
	}
	wad_load_sprites();
	video_init();
	game_load();
	game_start();
	g_state = GS_TITLE;
	draw_title(1);
	blit_frame();

	/* wait for any residual ENTER key press from menu selection to release */
	for (;;) {
		keypad_read();
		keypad_read();
		keypad_read();
		keypad_read();
		if (!get_key_state(KEY_ENTER))
			break;
	}

	while (1) {
		poll_input();
		if (g_input.quit)
			break;
		handle_state_input();
		game_update();

		if (g_state == GS_TITLE) {
			draw_title(((frame_count >> 4) & 1));
		} else if (g_state == GS_INTERMISSION) {
			draw_intermission();
		} else {
			game_render();
		}
		blit_frame();
		frame_count++;
	}
	cpg_turbo_off();
	return -2;
}

#endif
