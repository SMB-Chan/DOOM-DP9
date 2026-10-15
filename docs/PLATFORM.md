# XD-K4800 platform notes

Updated: 2026-10-15

## Confirmed from device (Test Menu + libexword)

| Source | Field | Value |
|--------|-------|-------|
| Test Menu | model | **CY700** |
| Test Menu | BIOS ver | **01.00** |
| Test Menu | OS ver | **01.00** |
| libexword `model` | Model | gy131,ON,0100 |
| libexword `model` | Sub | gy999 |
| libexword `model` | Extended | **CY700** |
| libexword `model` | Capabilities | SW P F C |
| libexword `capacity` | Internal | 100 MiB (104857600) |
| USB | VID:PID | 07cf:6101 (CESG502) |
| Official | Display | 528×320 TFT |
| Official | Year / line | 2015 K-series |

## Interpretation

- Firmware family ID is **CY700** (matches dictscript-era `CY*` NOR naming).
- BIOS/OS both at **01.00** — stock, no custom flash observed.
- Internal storage root via USB Library mode: `_INTERNAL_00\`
- SD was empty/not inserted during probe.

## Executable / hardware notes (Confirmed from `CY700_OS.BIN` & `NDUMP v3`)

- Add-on native binary: `*.d01` with magic `CASIODICPLUGIN10` (SuperH SH-4A big-endian code), loaded at **`0x8FDD8000`** (with SDRAM alias at `0x8C9D8000`).
- Display: **528×320** RGB565 TFT; VRAM buffer @ **`0xACA00000`** on `CY700` (`528 × 320 × 2 = 337,920` bytes; `lcdc_copy_vram` = syscall `0x01(-48, 0, 528, 320)`; note: `0xAC200000` is the P2 alias of `0x8C200000` inside the running `CY700` OS kernel—do **not** write to `0xAC200000`!).
- Memory map (`CY700` / XD-K4800):
  - `0x80000000`: Unmapped / guard page on `CY700` (do **not** dereference; NAND-boot architecture replaces linear Area 0 NOR).
  - `0x8C000000`–`0x8C58FFFF`: Main OS firmware in Area 3 SDRAM (~5.56 MiB, ends with `CASIODICS04AL100` / `"SH辞書"` header at `0x8C58FF80`).
  - `0x8C1D0000`–`0x8C1F0000`: Built-in Factory Test / Diagnostic Menu (`NAND_FLASH CHECK`, `BATTTEST`, `ACCESS CHECK`, `CASIOIPL_V00`).
  - `0x8C417078`: Syscall dispatcher entry (`mov.l @(r0,r1),r0; jmp @r0`), using table at **`0x8C48A5FC`**.
  - `0x8C600000`–`0x8C7FFFFF`: Kernel data / OS strings (`CASIOPVOS800U`, `Cepheus5`, etc.).
- Filesystem drive names (`0x8C19BFC4`): `drv0`, `sys0`, `data0`, `data1`, `data2`, `crd0`, `crd1`.
- Update & add-on strings in OS image:
  - SD update module path: `\\crd0\\UPDADN2.BIN` (`CASIO CY70x ADDIN MODULE FOR RELEASE`).
  - Official built-in `.d01` references (`0x8C45F326`+): `kaiso.d01`, `song.d01`.
  - SD authentication / key files: `\\crd0\\CASIO\\octavia.bin`, `demonstr.ter`, `admaster.ter`, `adcasio.inf`, key string `casio192837465tu`.

## Native Code Execution & Apps (Working on Device)

- **Native `.d01` execution on DP9 (`CY700`) is 100% working!**
- Root causes of previous bounce:
  1. `libexword` 64-bit `long` XOR bug in `src/dict.c` corrupted uploaded `.htm` files.
  2. Strict `CRLF` line endings required in `diction.htm`, `infodp*.htm`, and `infogt*.htm`.
  3. Syscall entry address on `CY700` moved to `0x8C417078` (resolved dynamically at runtime by scanning `0x8C380000`–`0x8C500000`).
- Working native apps installed on XD-K4800:
  - **`NDUMP` (v3)**: Dumped full 8.0 MiB `CY700_OS.BIN`, `SYSTBL.BIN`, `SC01CODE.BIN`, and `NDIAG.TXT` (`D:\EXword\dumps\ndump_v3\`).
  - **`MEMOA` (EX-word Memo Pad)**: Interactive text editor with 528×320 RGB565 GUI, QWERTY/cursor/file shortcuts, and load/save to `\\drv0\\MEMO.TXT` and `\\crd0\\MEMO.TXT` (`D:\EXword\memo_app\`).
  - **`DOOMX` (DOOM EX-word)**: Native DOOM for E1M1 — column-based BSP renderer, embedded LZSS-compressed WAD subset (E1M1 + sprites/flats/palette, 82 KB), procedural wall textures, monsters/items/doors/exit. Builds via `scripts/build_and_install_doom_cy700.sh` → `addons/DOOMX/doom.d01` (117 KB). Source: `D:\EXword\addons_src\DOOM_cy700\` (see its README.md).

## Map data format (from dumped doom1.wad shareware)

The shareware `doom1.wad` map lumps use the **short-coordinate variant**:

| Lump | Stride | Format |
|------|--------|--------|
| VERTEXES | 4 | `short x, short y` (units, NOT fixed 16.16) |
| SEGS | 12 | standard (v1, v2, angle, linedef, dir, offset — offsets in units) |
| SSECTORS | 4 | standard |
| NODES | **28** | `short x,y,dx,dy` + 8×`short` bbox + 2×`unsigned short` children (bit15 = subsector) |
| LINEDEFS | 14 | standard |
| SIDEDEFS | 30 | standard |
| SECTORS | 26 | standard |
| THINGS | 10 | standard (DoomEd numbers: zombie 3004, imp 3001, demon 3002, sergeant 9, barrel 2035) |

Note: DOOM COLORMAP rows are darkest-last (row 0 = bright, 31 = dark).
