#!/usr/bin/env python3
"""Build roms/litho-demo.gb — an original MIT Game Boy demo for LithoOS tests.

A tiny DMG program: a LITHOOS title, a bouncing gem sprite, and a D-pad that
moves it. No third-party assets. Header checksums are computed here.
"""
from pathlib import Path

ROM_SIZE = 32768
rom = bytearray(ROM_SIZE)

def put(addr, data):
    if isinstance(data, int):
        rom[addr] = data & 0xFF
        return
    rom[addr:addr+len(data)] = data

# Entry: 0x0100 NOP; JP 0x0150
put(0x0100, bytes([0x00, 0xC3, 0x50, 0x01]))

# VBlank (and other) ISRs must RETI, otherwise HALT jumps into the header
# and the program restarts every frame (title never sticks).
for vec in (0x0040, 0x0048, 0x0050, 0x0058, 0x0060):
    put(vec, 0xD9)  # RETI

# Standard Nintendo logo (required header bytes for a well-formed GB ROM)
logo = bytes.fromhex(
    "CEED6666CC0D000B03730083000C000D"
    "0008111F8889000EDCCC6EE6DDDDD999"
    "BBBB67636E0EECCCDDDC999FBBB9333E"
)
put(0x0104, logo)

title = b"LITHO DEMO"
put(0x0134, title + bytes(15 - len(title)))
put(0x0143, 0x00)  # DMG
put(0x0144, 0x00)
put(0x0145, 0x00)
put(0x0146, 0x00)  # SGB flag
put(0x0147, 0x00)  # ROM only
put(0x0148, 0x00)  # 32 KiB
put(0x0149, 0x00)  # no RAM
put(0x014A, 0x01)  # non-Japanese
put(0x014B, 0x33)
put(0x014C, 0x00)  # version

# --- Program at 0x0150 ---
# Tile 1: a filled diamond (8x8)
# Tile 2-11: 8x8 caps for L I T H O   D E M  (we'll use a tiny 8-wide font)
# Keep the program compact.

# Helper to emit GB machine code
code = bytearray()

def emit(*bs):
    for b in bs:
        if isinstance(b, (bytes, bytearray)):
            code.extend(b)
        else:
            code.append(b & 0xFF)

# Copy 16 tiles (256 bytes) from tiledata (after the code, we'll place at 0x0500)
# We'll write a relocatable stub:
#   di
#   ld sp, $FFFE
#   ; copy tiles
#   ld hl, TILESRC
#   ld de, $8000
#   ld bc, TILELEN
# copy: ld a,[hl+]; ld [de],a; inc de; dec bc; ld a,b; or c; jr nz,copy
#   ; clear VRAM tilemap 9800
#   ... write "LITHOOS" at row 2, and a gem tile that we move via SCX/sprite
#   ld a,$E4 / ld ($FF47),a  BGP
#   ld a,$D0 / ld ($FF48),a  OBP0
#   ; LCDC on: BG+OBJ 8000 addressing, $91
#   ld a,$93
#   ld ($FF40),a
# loop:
#   halt
#   ; read joyp, move sprite X/Y stored in HRAM
#   jr loop

# I'll write this more carefully as assembled bytes with known addresses.

# Place tile data at 0x0500, map init at 0x0600, code at 0x0150.

def tile_row(bits):
    """8 bits -> 2 bytes of GB 2bpp (both planes same = colors 0 or 3)."""
    return bytes([bits, bits])

def glyph(rows):
    assert len(rows) == 8
    out = b""
    for r in rows:
        if isinstance(r, str):
            bits = 0
            for ch in r:
                bits = (bits << 1) | (1 if ch in "X#" else 0)
            out += tile_row(bits)
        else:
            out += tile_row(r)
    return out

tiles = []
# 0: empty
tiles.append(b"\x00" * 16)
# 1: diamond / gem
tiles.append(glyph([
    "........",
    "...XX...",
    "..XXXX..",
    ".XXXXXX.",
    ".XXXXXX.",
    "..XXXX..",
    "...XX...",
    "........",
]))
# 2: solid block
tiles.append(glyph(["XXXXXXXX"] * 8))
# 3-14: L I T H O O S   D E M O  and a happy dot
font = {
    "L": ["X......","X......","X......","X......","X......","X......","XXXXXX.","........"],
    "I": [".XXXX..","..XX...","..XX...","..XX...","..XX...","..XX...",".XXXX..","........"],
    "T": ["XXXXXX.","..XX...","..XX...","..XX...","..XX...","..XX...","..XX...","........"],
    "H": ["X....X.","X....X.","X....X.","XXXXXX.","X....X.","X....X.","X....X.","........"],
    "O": [".XXXX..","X....X.","X....X.","X....X.","X....X.","X....X.",".XXXX..","........"],
    "S": [".XXXX..","X......","X......",".XXXX..",".....X.",".....X.",".XXXX..","........"],
    "D": ["XXXXX..","X....X.","X....X.","X....X.","X....X.","X....X.","XXXXX..","........"],
    "E": ["XXXXXX.","X......","X......","XXXXX..","X......","X......","XXXXXX.","........"],
    "M": ["X....X.","XX..XX.","X.XX.X.","X....X.","X....X.","X....X.","X....X.","........"],
    " ": ["........"] * 8,
}
order = "LITHO SDEMO "  # 12 tiles starting at index 3
for ch in "LITHOSDEMO ":
    tiles.append(glyph(font[ch]))

# tile 14 (index 14) already last space? LITHOSDEMO space = 11 chars + empty+diamond+block = 14 tiles (0-13)
# 11 letters/space from "LITHOSDEMO " = 11, plus 3 = 14 tiles (0-13). Good.

tile_blob = b"".join(tiles)
assert len(tile_blob) == 16 * len(tiles)
TILE_SRC = 0x0500
put(TILE_SRC, tile_blob)
TILE_LEN = len(tile_blob)

# Assemble code at 0x0150
# We'll manually assemble.

def ld_sp(imm):
    return bytes([0x31, imm & 0xFF, (imm >> 8) & 0xFF])
def ld_hl(imm):
    return bytes([0x21, imm & 0xFF, (imm >> 8) & 0xFF])
def ld_de(imm):
    return bytes([0x11, imm & 0xFF, (imm >> 8) & 0xFF])
def ld_bc(imm):
    return bytes([0x01, imm & 0xFF, (imm >> 8) & 0xFF])
def ld_a(imm):
    return bytes([0x3E, imm & 0xFF])
def ld_ann(addr):  # LD A,(nn)
    return bytes([0xFA, addr & 0xFF, (addr >> 8) & 0xFF])
def ld_nna(addr):  # LD (nn),A
    return bytes([0xEA, addr & 0xFF, (addr >> 8) & 0xFF])

# HRAM: FF80 = spr_x, FF81 = spr_y, FF82 = dir

prog = bytearray()
def P(*xs):
    for x in xs:
        if isinstance(x, (bytes, bytearray, list, tuple)):
            prog.extend(x)
        else:
            prog.append(x & 0xFF)

P(0xF3)          # DI
P(ld_sp(0xFFFE))

# Wait a bit / turn LCD off if needed (starts off after boot? After header skip, LCD is off in peanut reset)
# Copy tiles to VRAM 8000
P(ld_hl(TILE_SRC))
P(ld_de(0x8000))
P(ld_bc(TILE_LEN))
copy_loop = 0x0150 + len(prog)
# ld a,[hl+]   2A
# ld [de],a    12
# inc de       13
# dec bc       0B
# ld a,b       78
# or c         B1
# jr nz, copy  20 xx
P(0x2A, 0x12, 0x13, 0x0B, 0x78, 0xB1)
# jr nz back 7 bytes before this jr (the copy body is 6, jr is 2, total 8 back from next)
P(0x20, 0xF8)  # 0xF8 = -8, points to 2A

# Clear tilemap at 9800 (32*32 = 1024 bytes) to 0
P(ld_hl(0x9800))
P(ld_bc(0x0400))
clr = len(prog)
P(0xAF)        # xor a
P(0x22)        # ld [hl+],a
P(0x0B)        # dec bc
P(0x78, 0xB1)  # ld a,b / or c
P(0x20, 0xF9)  # jr nz, -7 (from after this to xor? wait xor is before... 
# body: AF 22 0B 78 B1 20 xx  = after 20 xx, -7 lands on 22, skipping xor after first. BAD.
# Use: xor a once, then loop ld [hl+],a; dec bc; ld a,b; or c; jr nz; need to keep a=0.
# Better: 
#   xor a
# loop: ld [hl+],a ; dec bc; ld e,a; ld a,b; or c; ld a,e; jr nz
# Simpler: just write known map without full clear: we'll write a few rows only, VRAM is 0 after reset? peanut vram is zeroed in gb_reset? memset? gb_reset may not clear vram. I'll fix the loop:

# I already emitted a bad clear. Let me rebuild prog more carefully from scratch.

prog = bytearray()

def rel(target_index, from_after):
    d = target_index - from_after
    if d < -128 or d > 127:
        raise ValueError(d)
    return d & 0xFF

P(0xF3)
P(ld_sp(0xFFFE))
P(ld_hl(TILE_SRC))
P(ld_de(0x8000))
P(ld_bc(TILE_LEN))
copy_start = len(prog)
P(0x2A, 0x12, 0x13, 0x0B, 0x78, 0xB1)
P(0x20, rel(copy_start, len(prog)+2))

# Clear 9800-9BFF: a stays 0
P(ld_hl(0x9800))
P(ld_bc(0x0400))
P(0xAF)  # xor a
clear_start = len(prog)
P(0x22)        # ld [hl+],a   a is 0
P(0x0B)        # dec bc
P(0x5F)        # ld e,a       save 0
P(0x78, 0xB1)  # ld a,b / or c
P(0x7B)        # ld a,e       restore 0
P(0x20, rel(clear_start, len(prog)+2))

# Write "LITHOOS" on row 4 (y=4), x=6: tiles L I T H O O S = indices
# L=3 I=4 T=5 H=6 O=7 O=7 S=8
# tilemap row 4 starts at 9800 + 4*32 = 9880, +6 = 9886
msg = [3, 4, 5, 6, 7, 7, 8]  # LITHOOS
P(ld_hl(0x9886))
for t in msg:
    P(ld_a(t), 0x22)  # ld a,n; ld [hl+],a

# "DEMO" on row 6, x=8: D E M O = 9,10,11,7
P(ld_hl(0x98C8))
for t in (9, 10, 11, 7):
    P(ld_a(t), 0x22)

# Visible frame: fill row 0 and row 17 of the tilemap with solid tile 2.
P(ld_hl(0x9800))
P(ld_a(2))
for _ in range(20):
    P(0x22)  # ld [hl+],a
P(ld_hl(0x9800 + 17 * 32))
P(ld_a(2))
for _ in range(20):
    P(0x22)
# Left and right columns, rows 1-16.
for row in range(1, 17):
    P(ld_hl(0x9800 + row * 32))
    P(ld_a(2), 0x22)
    P(ld_hl(0x9800 + row * 32 + 19))
    P(ld_a(2), 0x22)

# Palette BGP = E4 (classic), OBP0 = E4
P(ld_a(0xE4), ld_nna(0xFF47))
P(ld_a(0xE4), ld_nna(0xFF48))

# Sprite 0: gem tile 1, at (80, 80). OAM at FE00
# Wait, must write OAM via DMA or during vblank. On startup LCD off, we can write OAM.
# Y, X, tile, flags
P(ld_a(80), ld_nna(0xFE00))  # Y (GB sprite Y is +16, so 80+16=96? Hardware Y is screen+16)
# GB sprite Y register = screen_y + 16, X = screen_x + 8
P(ld_a(80+16), ld_nna(0xFE00))
P(ld_a(80+8), ld_nna(0xFE01))
P(ld_a(1), ld_nna(0xFE02))
P(ld_a(0), ld_nna(0xFE03))

# Init HRAM pos: screen x=80, y=80 at FF80/FF81, vx=1 FF82, vy=1 FF83
P(ld_a(80), ld_nna(0xFF80))
P(ld_a(80), ld_nna(0xFF81))
P(ld_a(1), ld_nna(0xFF82))
P(ld_a(1), ld_nna(0xFF83))

# Turn on LCD: LCDC = 0x93 (BG on, OBJ on, 8000 tiles, bgmap 9800)
P(ld_a(0x93), ld_nna(0xFF40))

# Enable VBlank interrupt only, then EI + halt loop
P(ld_a(0x01), ld_nna(0xFFFF))  # IE
P(0xAF, ld_nna(0xFF0F))        # clear IF
P(0xFB)                        # EI

# Main loop at vblank: move gem, bounce, apply joypad
loop = len(prog)
P(0x76)  # HALT
# After halt, one interrupt. Move.

# Read joypad: write P15=0 (directions) to FF00, read
# LD A,$20 / LD ($FF00),A / LD A,($FF00) / CPL / AND $0F
# bits: 0 right, 1 left, 2 up, 3 down (when P15 selected... actually:
# P14 (bit4=0) selects D-pad: bits 0-3 = right, left, up, down (active 0)
# P15 (bit5=0) selects buttons: A,B,select,start

# Apply d-pad to position
P(ld_a(0x20), ld_nna(0xFF00))
P(0x00, 0x00, 0x00)  # nops for settle
P(ld_ann(0xFF00))
P(0x2F)      # cpl
P(0xE6, 0x0F)  # and 0F
# a = pressed directions. Save in d
P(0x57)  # ld d,a

# load x,y from HRAM
P(ld_ann(0xFF80))
P(0x5F)  # ld e,a  x
P(ld_ann(0xFF81))
P(0x6F)  # ld l,a  y  (h unused)

# if right (bit0): inc e (max 152)
P(0x7A, 0xE6, 0x01, 0x28, 0x06)  # ld a,d; and 1; jr z +6
P(0x1C)  # inc e
# cp 152; jr c skip; ld e,152
P(0x7B, 0xFE, 152, 0x38, 0x02, ld_a(152), 0x5F)

# This relative jump math is getting error-prone. I'll use a simpler animation:
# ignore mid-instruction jr lengths by building a bounce-only loop first (no joy),
# then add joy with computed relatives.

# REWRITE loop as bounce only + joy with careful labels.

# Everything up to LCD on / EI is good. Restart loop from here.
head = bytes(prog)
prog = bytearray()
# (will concatenate)

def assemble_loop():
    """Returns loop body bytes; uses labels dict filled in two passes."""
    pass

# Two-pass mini assembler
labels = {}
fixes = []  # (idx_of_rel_byte, label)

def pass_emit():
    out = bytearray()
    def B(*xs):
        for x in xs:
            if isinstance(x, (bytes, bytearray)):
                out.extend(x)
            else:
                out.append(x & 0xFF)
    def L(name):
        labels[name] = len(out)
    def JR(op, name):
        fixes.append((len(out)+1, name, len(out)+2))
        B(op, 0)
    def JRNZ(name):
        JR(0x20, name)
    def JRZ(name):
        JR(0x28, name)
    def JR_(name):
        JR(0x18, name)
    def JRC(name):
        JR(0x38, name)
    def JRNC(name):
        JR(0x30, name)

    L("loop")
    B(0x76)  # HALT

    # Auto-bounce vx,vy in FF82/FF83, pos in FF80/FF81
    # x += vx; if x<8 or x>152: vx = -vx; clamp
    # y similarly 16..128
    # Then if joypad right, x++ etc (extra nudge)

    # x = x + vx
    B(ld_ann(0xFF80), 0x47)   # ld a,(x) ; ld b,a
    B(ld_ann(0xFF82), 0x80)   # ld a,(vx); add a,b
    B(0xEA, 0x80, 0xFF)       # ld (x),a
    B(0xFE, 8)
    JRC("flipx")
    B(0xFE, 152)
    JRNC("flipx")
    JR_("doney")
    L("flipx")
    B(ld_ann(0xFF82), 0x2F, 0x3C, ld_nna(0xFF82))  # vx = -vx  (cpl; inc a)
    # clamp x
    B(ld_ann(0xFF80))
    B(0xFE, 8, 0x30, 0x04)  # cp 8; jr nc +4
    B(ld_a(8), ld_nna(0xFF80))
    B(ld_ann(0xFF80))
    B(0xFE, 152, 0x38, 0x04)
    B(ld_a(152), ld_nna(0xFF80))
    L("doney")

    # y = y + vy
    B(ld_ann(0xFF81), 0x47)
    B(ld_ann(0xFF83), 0x80)
    B(ld_nna(0xFF81))
    B(0xFE, 24)
    JRC("flipy")
    B(0xFE, 120)
    JRNC("flipy")
    JR_("dojoy")
    L("flipy")
    B(ld_ann(0xFF83), 0x2F, 0x3C, ld_nna(0xFF83))
    B(ld_ann(0xFF81))
    B(0xFE, 24, 0x30, 0x04)
    B(ld_a(24), ld_nna(0xFF81))
    B(ld_ann(0xFF81))
    B(0xFE, 120, 0x38, 0x04)
    B(ld_a(120), ld_nna(0xFF81))
    L("dojoy")

    # D-pad nudges
    B(ld_a(0x20), ld_nna(0xFF00))
    B(0x00, 0x00)
    B(ld_ann(0xFF00), 0x2F, 0xE6, 0x0F, 0x57)  # d = pressed

    # right
    B(0x7A, 0xE6, 0x01)
    JRZ("nright")
    B(ld_ann(0xFF80), 0x3C, ld_nna(0xFF80))
    L("nright")
    # left
    B(0x7A, 0xE6, 0x02)
    JRZ("nleft")
    B(ld_ann(0xFF80), 0x3D, ld_nna(0xFF80))
    L("nleft")
    # up
    B(0x7A, 0xE6, 0x04)
    JRZ("nup")
    B(ld_ann(0xFF81), 0x3D, ld_nna(0xFF81))
    L("nup")
    # down
    B(0x7A, 0xE6, 0x08)
    JRZ("ndown")
    B(ld_ann(0xFF81), 0x3C, ld_nna(0xFF81))
    L("ndown")

    # Update sprite OAM from pos: Y=y+16, X=x+8
    B(ld_ann(0xFF81), 0xC6, 16, ld_nna(0xFE00))
    B(ld_ann(0xFF80), 0xC6, 8,  ld_nna(0xFE01))

    # Simple CH1 beep when A pressed (for audio CRC to be interesting)
    B(ld_a(0x10), ld_nna(0xFF00))
    B(0x00, 0x00)
    B(ld_ann(0xFF00), 0x2F, 0xE6, 0x01)
    JRZ("nobeep")
    # NR10-NR14 a short pulse
    B(ld_a(0x80), ld_nna(0xFF26))  # NR52 on
    B(ld_a(0x77), ld_nna(0xFF24))  # NR50 vol
    B(ld_a(0x11), ld_nna(0xFF25))  # NR51 ch1 both
    B(ld_a(0x00), ld_nna(0xFF10))
    B(ld_a(0x80), ld_nna(0xFF11))
    B(ld_a(0xF3), ld_nna(0xFF12))
    B(ld_a(0xC1), ld_nna(0xFF13))
    B(ld_a(0x87), ld_nna(0xFF14))
    L("nobeep")

    JR_("loop")
    return out

# Two passes
fixes = []
labels = {}
body = pass_emit() if False else None

# define properly
def build():
    global labels, fixes
    labels = {}
    fixes = []
    fixes_abs = []
    out = bytearray()
    def B(*xs):
        for x in xs:
            if isinstance(x, (bytes, bytearray)):
                out.extend(x)
            else:
                out.append(x & 0xFF)
    def L(name):
        labels[name] = len(out)
    def JR(op, name):
        fixes.append((len(out)+1, name, len(out)+2))
        B(op, 0)
    def JRZ(name): JR(0x28, name)
    def JRNZ(name): JR(0x20, name)
    def JR_(name): JR(0x18, name)
    def JRC(name): JR(0x38, name)
    def JRNC(name): JR(0x30, name)

    L("loop")
    B(0x76)

    B(ld_ann(0xFF80), 0x47)
    B(ld_ann(0xFF82), 0x80)
    B(ld_nna(0xFF80))
    B(0xFE, 8)
    JRC("flipx")
    B(0xFE, 152)
    JRNC("flipx")
    JR_("doney")
    L("flipx")
    B(ld_ann(0xFF82), 0x2F, 0x3C, ld_nna(0xFF82))
    B(ld_ann(0xFF80))
    B(0xFE, 8)
    JRNC("xlo_ok")
    B(ld_a(8), ld_nna(0xFF80))
    L("xlo_ok")
    B(ld_ann(0xFF80))
    B(0xFE, 152)
    JRC("xhi_ok")
    B(ld_a(152), ld_nna(0xFF80))
    L("xhi_ok")
    L("doney")

    B(ld_ann(0xFF81), 0x47)
    B(ld_ann(0xFF83), 0x80)
    B(ld_nna(0xFF81))
    B(0xFE, 24)
    JRC("flipy")
    B(0xFE, 120)
    JRNC("flipy")
    JR_("dojoy")
    L("flipy")
    B(ld_ann(0xFF83), 0x2F, 0x3C, ld_nna(0xFF83))
    B(ld_ann(0xFF81))
    B(0xFE, 24)
    JRNC("ylo_ok")
    B(ld_a(24), ld_nna(0xFF81))
    L("ylo_ok")
    B(ld_ann(0xFF81))
    B(0xFE, 120)
    JRC("yhi_ok")
    B(ld_a(120), ld_nna(0xFF81))
    L("yhi_ok")
    L("dojoy")

    B(ld_a(0x20), ld_nna(0xFF00))
    B(0x00, 0x00)
    B(ld_ann(0xFF00), 0x2F, 0xE6, 0x0F, 0x57)

    B(0x7A, 0xE6, 0x01)
    JRZ("nright")
    B(ld_ann(0xFF80), 0x3C, ld_nna(0xFF80))
    L("nright")
    B(0x7A, 0xE6, 0x02)
    JRZ("nleft")
    B(ld_ann(0xFF80), 0x3D, ld_nna(0xFF80))
    L("nleft")
    B(0x7A, 0xE6, 0x04)
    JRZ("nup")
    B(ld_ann(0xFF81), 0x3D, ld_nna(0xFF81))
    L("nup")
    B(0x7A, 0xE6, 0x08)
    JRZ("ndown")
    B(ld_ann(0xFF81), 0x3C, ld_nna(0xFF81))
    L("ndown")

    B(ld_ann(0xFF81), 0xC6, 16, ld_nna(0xFE00))
    B(ld_ann(0xFF80), 0xC6, 8,  ld_nna(0xFE01))

    B(ld_a(0x10), ld_nna(0xFF00))
    B(0x00, 0x00)
    B(ld_ann(0xFF00), 0x2F, 0xE6, 0x01)
    JRZ("nobeep")
    B(ld_a(0x80), ld_nna(0xFF26))
    B(ld_a(0x77), ld_nna(0xFF24))
    B(ld_a(0x11), ld_nna(0xFF25))
    B(ld_a(0x00), ld_nna(0xFF10))
    B(ld_a(0x80), ld_nna(0xFF11))
    B(ld_a(0xF3), ld_nna(0xFF12))
    B(ld_a(0xC1), ld_nna(0xFF13))
    B(ld_a(0x87), ld_nna(0xFF14))
    L("nobeep")

    # JP loop (too far for JR)
    addr = labels["loop"]  # will be patched after; placeholder
    B(0xC3, 0, 0)
    fixes_abs.append((len(out)-2, "loop"))

    # patch relatives
    for idx, name, after in fixes:
        d = labels[name] - after
        if not -128 <= d <= 127:
            raise SystemExit(f"rel out of range {name}: {d}")
        out[idx] = d & 0xFF
    # Absolute JP targets are offsets in this loop body; caller adds base.
    for idx, name in fixes_abs:
        # stored as body-relative; converted later
        pass
    return out, labels, fixes_abs

loop_body, loop_labels, loop_abs = build()
base = 0x0150 + len(head)
for idx, name in loop_abs:
    dest = base + loop_labels[name]
    loop_body[idx] = dest & 0xFF
    loop_body[idx+1] = (dest >> 8) & 0xFF
full = bytes(head) + bytes(loop_body)
if 0x0150 + len(full) >= TILE_SRC:
    raise SystemExit(f"code overflow: {hex(0x0150+len(full))} vs {hex(TILE_SRC)}")
put(0x0150, full)
print(f"code {len(full)} bytes, ends at {hex(0x0150+len(full))}")

# Header checksum: ~sum(0x0134..0x014C) + 1  (x = x - byte - 1)
x = 0
for i in range(0x0134, 0x014D):
    x = (x - rom[i] - 1) & 0xFF
put(0x014D, x)

# Global checksum: sum of all bytes except 014E-F
rom[0x014E] = 0
rom[0x014F] = 0
s = sum(rom) & 0xFFFF
put(0x014E, bytes([(s >> 8) & 0xFF, s & 0xFF]))

out = Path("/workspace/lithoos/roms/litho-demo.gb")
out.write_bytes(bytes(rom))
print("wrote", out, "size", len(rom), "header_ck", f"{x:02X}", "global", f"{s:04X}")
