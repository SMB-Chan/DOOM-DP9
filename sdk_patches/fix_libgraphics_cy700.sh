#!/bin/bash
set -euo pipefail

export DEVKITPRO=/opt/exword/devkitPro
export DEVKITSH4=/opt/exword/devkitPro/devkitSH4
export PATH="$DEVKITSH4/bin:$DEVKITPRO/tools/bin:/usr/local/bin:$PATH"

LIBG=/opt/exword/devkitPro/libdataplus/lib/libgraphics.a
if [ ! -f "${LIBG}.orig" ]; then
    cp -a "$LIBG" "${LIBG}.orig"
fi

mkdir -p /tmp/fix_lcdc
cd /tmp/fix_lcdc
rm -f *.o *.c
sh-elf-ar x "${LIBG}.orig"

cat > lcdc_cy700.c << 'EOF'
#include <stdint.h>

static uint16_t g_height = 0x0140; /* 320 */
static uint16_t g_width  = 0x0210; /* 528 */
static void *g_vram      = (void *)0xACA00000UL;

void lcdc_set_vram_address(void *addr)
{
    g_vram = addr;
}

void *lcdc_get_vram_address(void)
{
    if (g_vram == (void *)0xACA00000UL) {
        uint32_t sc = *(volatile uint32_t *)0x74000000UL;
        if (sc < 0x8C000000UL) {
            g_vram = (void *)0xAC200000UL;
        }
    }
    return g_vram;
}

void lcdc_get_dimensions(uint16_t *width, uint16_t *height)
{
    if (width)  *width  = g_width;
    if (height) *height = g_height;
}

uint16_t lcdc_read_word(void)
{
    return *(volatile uint16_t *)0xB4000000UL;
}

void lcdc_write_word(uint16_t val)
{
    *(volatile uint16_t *)0xB4000000UL = val;
    __asm__ volatile("synco" ::: "memory");
}

void lcdc_write_cmd(uint16_t cmd)
{
    volatile uint8_t *p = (volatile uint8_t *)0xA405013CUL;
    *p &= (uint8_t)~0x10;
    __asm__ volatile("synco" ::: "memory");
    *(volatile uint16_t *)0xB4000000UL = cmd;
    __asm__ volatile("synco" ::: "memory");
    *p |= 0x10;
    __asm__ volatile("synco" ::: "memory");
}

void lcdc_set_window(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    g_width = w;
    g_height = h;
    (void)x;
    (void)y;
}

__asm__(
    ".section .text.sys_0x01_local,\"ax\",@progbits\n"
    ".align 2\n"
    "_sys_0x01_local:\n"
    "    mov #1, r0\n"
    "    mov.l 1f, r1\n"
    "    mov.l @r1, r1\n"
    "    jmp @r1\n"
    "    nop\n"
    ".align 2\n"
    "1:  .long 0x74000000\n"
);
extern void sys_0x01_local(int x, int y, int w, int h);

void lcdc_copy_vram(void)
{
    sys_0x01_local(-48, 0, (int)g_width, (int)g_height);
}
EOF

sh-elf-gcc -mb -m4a-nofpu -O2 -ffunction-sections -fdata-sections -c lcdc_cy700.c -o lcdc.o
sh-elf-ar rcs "$LIBG" init.o text.o drawing.o sprite.o color.o lcdc.o
sh-elf-objdump -d -r lcdc.o
