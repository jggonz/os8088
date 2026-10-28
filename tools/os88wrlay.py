"""os88wrlay: WaveRack's three layouts and two themes, as DATA (SPEC.md 123.11).

Imported by tools/os88wrart.py, which emits what this builds into
build/wrart.inc beside the pictures, and runs selfcheck() as part of its own.

Every rectangle WaveRack draws or hit-tests is written HERE, once: the keys,
the sliders, the drop-downs, the check box, the fields, the LCD, the meter's
well and cells, the waveform and its overview, and every static panel, rule,
label and picture as an op list the package interprets at a full paint
(wrface.inc, wf_deco). The asm reads coordinates out of the layout record and
never writes a number of its own, so the measurement below is a measurement
of what ships:

  - every rect inside its layout's content box;
  - no two CONTROLS overlapping (a control is anything a press can land on,
    or a field an element draws into);
  - every static label, picture and rule clear of every control, unless it is
    declared inside one (the PEAK caption inside the meter's well);
  - every element's text inside its container, at 8 px a system-font
    character and 6 px a small-caps one (wd_capsw's arithmetic);
  - every key's picture and caption, every drop-down's longest item and every
    field's longest text fitting with a margin;
  - each layout's window inside the screen it was cut for: FULL in 640 x 480,
    MID in 640 x 350 (and Hercules' 720 x 348), COMPACT in 640 x 200, with
    the menu bar, the dock and the title bar counted.

COORDINATES are content-relative and INCLUSIVE (x1, y1, x2, y2), CDRack's
convention.
"""

MBAR_H, TITLE_H, DOCK_H = 20, 18, 24

# =============================================================================
# THE ROLES AND THE THEMES (crdraw.inc's shape: a byte a role; 80h + a pattern
# for a dithered surface)
# =============================================================================
ROLES = [
    # name        colour     black and white
    ('BG',        'CLGRAY',  'CWHITE'),   # the faceplate
    ('HI',        'CWHITE',  'CBLACK'),   # a bevel's lit edge (B&W: a rule)
    ('SH',        'CDGRAY',  'CBLACK'),   # ...and its shaded one
    ('GLINT',     'CWHITE',  'CWHITE'),   # a groove's lit line (B&W: none)
    ('LABEL',     'CBLACK',  'CBLACK'),   # lettering on the faceplate
    ('LOGO',      'CBLACK',  'CBLACK'),
    ('SCREW',     'CDGRAY',  'CBLACK'),
    ('GRILLE',    'CDGRAY',  'CBLACK'),
    ('LCD',       'CBLACK',  'CBLACK'),   # the LCD's glass, the waveform's,
    ('DIGIT',     'CLGREEN', 'CWHITE'),   # ...its seven segments
    ('LCDTXT',    'CLCYAN',  'CWHITE'),   # ...the filename and format line
    ('WAVE',      'CLGREEN', 'CWHITE'),   # the waveform's ink
    ('GRID',      'CDGRAY',  'CWHITE'),   # its grid (B&W: sparse dots)
    ('RULER',     'CLCYAN',  'CWHITE'),   # its ruler
    ('SELBG',     'CDGRAY',  'CWHITE'),   # a selection's ground (B&W: the
                                          # span is the band INVERTED, wave 3)
    ('HANDLE',    'CLCYAN',  'CWHITE'),   # its handles and edges (wave 3)
    ('PLAYHEAD',  'CYELLOW', 'CWHITE'),   # (wave 3)
    ('LEDOFF',    'CDGRAY',  'CWHITE'),   # a dark cell (B&W: outlined)
    ('LEDG',      'CLGREEN', 'CWHITE'),   # lit, by zone
    ('LEDY',      'CYELLOW', 'CWHITE'),
    ('LEDR',      'CLRED',   'CWHITE'),
    ('BTN',       'CLGRAY',  'CWHITE'),   # a key's face
    ('BTNEDGE',   'CBLACK',  'CBLACK'),
    ('BTNTXT',    'CBLACK',  'CBLACK'),
    ('BTNDIS',    'CDGRAY',  'CBLACK'),   # greyed (B&W: half the ink)
    ('BTNON',     'CBLACK',  'CBLACK'),   # latched: a dark face...
    ('BTNONE',    'CLGREEN', 'CBLACK'),
    ('BTNONT',    'CLGREEN', 'CWHITE'),   # ...and lit ink
    ('REC',       'CLRED',   'CBLACK'),   # Record's dot
    ('SLINK',     'CBLACK',  'CBLACK'),   # a slider's groove, ticks, thumb
    ('FIELD',     'CWHITE',  'CWHITE'),   # the selection field
    ('FIELDTXT',  'CBLACK',  'CBLACK'),
    ('STAT',      'CLGRAY',  'CWHITE'),   # the status field
    ('STATTXT',   'CBLACK',  'CBLACK'),
]
ROLE = {n: i for i, (n, _, _) in enumerate(ROLES)}

# --- the static-part ops (wrface.inc, wf_deco): db op, ra, rb, arg;
#     dw a, b, c, d, p - a, b, c, d are AX, BX, CX, DX as crdraw.inc's
#     primitives take them
OPS = ['END', 'FILL', 'RAISED', 'SUNKEN', 'GROOVE', 'HL', 'VL', 'CAPS',
       'TEXT', 'PIC', 'GRILLE', 'CELLS', 'VGROOVE']
OP = {n: i for i, n in enumerate(OPS)}

# --- the buttons, in the record's order (waverack.asm's B_*)
BUTTONS = ['REC', 'STOP', 'PAUSE', 'PLAY', 'OPEN', 'SAVE', 'ZOUT', 'ZIN',
           'TRIM', 'DEL', 'SAVEAS']

# --- the drop-downs: Input, Rate, Bits, Channels; how many items each holds
DROPS = ['IN', 'RATE', 'BITS', 'CH']
DROP_N = [1, 3, 1, 1]

# --- the layout record: (field, words), in order. WL_<field> is its offset
FIELDS = [
    ('W', 1), ('H', 1), ('DECO', 1), ('ELEMS', 1), ('BTNS', 1), ('ICONS', 1),
    ('CAPS', 1), ('SIDE', 1), ('CMP', 1),
    ('DSET', 1), ('LCD', 4), ('FN', 2), ('FNCH', 1), ('TIM', 2), ('TOT', 2),
    ('FMT', 2), ('FMTCAPS', 1), ('FMTCH', 1),
    ('MCAP', 2), ('MCAPW', 1), ('MCAPS', 1),
    ('CELL', 2), ('CELLW', 1), ('CELLH', 1), ('CELLP', 1), ('PEAK', 4),
    ('GSL', 4), ('VSL', 4),
    ('WAVE', 4), ('RULH', 1), ('OVW', 4),
    ('SELT', 2), ('SELCH', 1), ('SELPFX', 1),
    ('STT', 2), ('STCH', 1),
    ('DROPS', 16), ('DRIT', 4),
    ('LOOP', 4), ('LBOX', 2), ('LTXT', 2), ('LLBL', 1), ('CHK', 1), ('UNCHK', 1),
]

# the element procs every layout walks (wrface.inc), (proc, param)
ELEMS = [('wf_e_fname', 0), ('wf_e_time', 0), ('wf_e_total', 0),
         ('wf_e_fmt', 0), ('wf_e_mcap', 0), ('wf_e_meter', 0),
         ('wf_e_peak', 0), ('wf_e_gain', 0), ('wf_e_vol', 0),
         ('wf_e_wave', 0), ('wf_e_ovw', 0), ('wf_e_wsel', 0), ('wf_e_cur', 0),
         ('wf_e_sel', 0),
         ('wf_e_stat', 0), ('wf_e_drop', 0), ('wf_e_drop', 1),
         ('wf_e_drop', 2), ('wf_e_drop', 3), ('wf_e_loop', 0)]

# the longest text each field is ever handed (waverack.asm's strings): the
# measurement holds the asm to these
STATUS_MAX = 22                 # wr_s_* status messages (SPEC.md 123.2)
SEL_VALUE = 13                  # 'SS.cc - SS.cc' (SPEC.md 123.9)
FNAME_CH = 12                   # an 8.3 name
FMT_TEXT = '22,050 Hz 16-bit Stereo'   # the longest format line
MCAPS_FULL = ['LEVEL', 'LEVEL  REC', 'LEVEL  PLAY']
MCAPS_CMP = ['LEVEL', 'REC', 'PLAY']
THUMB_W = 7                     # a slider's thumb (wrface.inc WF_THUMB)


def R(x1, y1, x2, y2):
    return (x1, y1, x2, y2)


class Layout:
    def __init__(self, name, w, h, cmp_, side, dset, screen):
        self.name, self.w, self.h = name, w, h
        self.cmp, self.side, self.dset, self.screen = cmp_, side, dset, screen
        self.ops = []           # (op, ra, rb, arg, a, b, c, d, p)
        self.controls = []      # (name, rect)
        self.statics = []       # (name, rect, inside-control-name or None)
        self.inner = []         # (name, rect, container rect name)
        self.f = {}             # the record's fields
        self.btns, self.pics, self.bcaps = [None] * 11, [0] * 11, [0] * 11

    # --- static ops ---------------------------------------------------------
    def op(self, name, ra=0, rb=0, arg=0, a=0, b=0, c=0, d=0, p=0):
        self.ops.append((OP[name], ra, rb, arg, a, b, c, d, p))

    def raised(self, r, role='BG'):
        self.op('RAISED', ROLE[role], 0, 0, *r)

    def sunken(self, r, role='LCD'):
        self.op('SUNKEN', ROLE[role], 0, 0, *r)

    def groove(self, r):
        self.op('GROOVE', 0, 0, 0, *r)
        self.statics.append(('groove', (r[0], r[1], r[2] + 1, r[3] + 1), 'frame'))

    def vgroove(self, x, y1, y2):
        self.op('VGROOVE', 0, 0, 0, x, y1, 0, y2)
        self.statics.append(('vgroove', (x, y1, x + 1, y2), None))

    def caps(self, x, y, text, ink='LABEL', paper='BG', gap=0, inside=None):
        self.op('CAPS', ROLE[ink], ROLE[paper], gap, x, y, 0, 0, ('s', text))
        self.statics.append(('caps %r' % text,
                             (x, y, x + self.art.caps_width(text, gap) - 1, y + 6),
                             inside))

    def text(self, x, y, s, ink='LABEL', ground='BG'):
        self.op('TEXT', ROLE[ink], ROLE[ground], 0, x, y, 0, 0, ('s', s))
        self.statics.append(('text %r' % s, (x, y, x + 8 * len(s) - 1, y + 7), None))

    def pict(self, x, y, name, ink='LABEL', paper='BG'):
        b = self.bm[name]
        self.op('PIC', ROLE[ink], ROLE[paper], 0, x, y, 0, 0, ('p', name))
        self.statics.append(('pic %s' % name,
                             (x, y, x + len(b[0]) - 1, y + len(b) - 1), None))

    def grille(self, r, pitch=3):
        self.op('GRILLE', ROLE['GRILLE'], ROLE['BG'], pitch, *r)
        self.statics.append(('grille', r, None))

    # --- controls -----------------------------------------------------------
    def ctl(self, name, r):
        self.controls.append((name, r))

    def button(self, i, r, pic, cap):
        self.btns[i] = r
        self.pics[i] = pic
        self.bcaps[i] = cap
        self.ctl('key ' + BUTTONS[i], r)


def build_full(art, bm):
    L = Layout('full', 608, 400, 0, 0, 'df', (640, 480))
    L.art, L.bm = art, bm
    L.raised(R(0, 0, 607, 399))
    for x, y in ((4, 6), (593, 6), (4, 385), (593, 385)):
        L.pict(x, y, 'screw', 'SCREW')
    L.pict(24, 7, 'logo', 'LOGO')
    lx = 24 + len(bm['logo'][0]) + 10
    L.vgroove(lx, 9, 25)
    L.caps(lx + 12, 13, 'DIGITAL SOUND RECORDER', gap=2)
    lcd_and_meter(L, art, lcd=R(8, 33, 311, 118), fn_y=41, tim_y=55, fmt_y=99,
                  lgrp=R(320, 36, 599, 76), well=R(326, 43, 593, 60),
                  cell_y=46, cell_h=12, scale_y=64,
                  ggrp=R(320, 84, 599, 118), gsl=R(350, 92, 568, 109))
    keys(L, y1=125, y2=168, zy1=133, zy2=160, gx=294)
    L.f['WAVE'] = R(10, 177, 597, 284)
    L.f['RULH'] = 14
    L.sunken(R(8, 175, 599, 286))
    L.ctl('wave', R(8, 175, 599, 286))
    L.f['OVW'] = R(10, 293, 597, 312)
    L.sunken(R(8, 291, 599, 314))
    L.ctl('overview', R(8, 291, 599, 314))
    edit_row(L, art, 321, 342, sel_x2=247)
    L.groove(R(8, 349, 598, 372))
    format_row(L, art, 352, 369, loop_txt='Loop selection', input_lbl=True,
               drops=[(220, 339), (346, 445), (452, 519), (526, 595)])
    L.grille(R(22, 380, 53, 394))
    bottom_row(L, art, y1=379, y2=396, spk_x=62, vol=R(140, 379, 271, 396),
               stat=R(298, 379, 583, 396), caps_x=82, lo_x=130, hi_x=276)
    return L


def build_mid(art, bm):
    L = Layout('mid', 608, 272, 0, 0, 'dm', (640, 350))
    L.art, L.bm = art, bm
    L.raised(R(0, 0, 607, 271))
    lcd_and_meter(L, art, lcd=R(8, 6, 311, 77), fn_y=12, tim_y=24, fmt_y=59,
                  lgrp=R(320, 9, 599, 41), well=R(326, 15, 593, 30),
                  cell_y=17, cell_h=11, scale_y=33,
                  ggrp=R(320, 49, 599, 77), gsl=R(350, 54, 568, 71))
    keys(L, y1=83, y2=112, zy1=86, zy2=109, gx=294)
    L.f['WAVE'] = R(10, 120, 597, 181)
    L.f['RULH'] = 12
    L.sunken(R(8, 118, 599, 183))
    L.ctl('wave', R(8, 118, 599, 183))
    L.f['OVW'] = R(10, 189, 597, 198)
    L.sunken(R(8, 187, 599, 200))
    L.ctl('overview', R(8, 187, 599, 200))
    edit_row(L, art, 205, 220, sel_x2=247)
    format_row(L, art, 224, 239, loop_txt='Loop selection', input_lbl=True,
               drops=[(220, 339), (346, 445), (452, 519), (526, 595)])
    bottom_row(L, art, y1=245, y2=262, spk_x=12, vol=R(90, 245, 221, 262),
               stat=R(298, 245, 583, 262), caps_x=32, lo_x=80, hi_x=226)
    return L


def build_cmp(art, bm):
    L = Layout('cmp', 624, 132, 1, 1, 'dc', (640, 200))
    L.art, L.bm = art, bm
    L.raised(R(0, 0, 623, 131))
    lcd = R(3, 2, 285, 34)
    L.sunken(lcd)
    L.ctl('lcd', lcd)
    L.f['LCD'] = lcd
    L.f['FN'] = (10, 5)
    L.f['FNCH'] = FNAME_CH
    L.inner.append(('filename', (10, 5, 10 + 8 * FNAME_CH - 1, 12), 'lcd'))
    L.f['FMT'] = (118, 6)
    L.f['FMTCAPS'] = 1
    L.f['FMTCH'] = len(FMT_TEXT)
    L.inner.append(('format', (118, 6, 118 + art.caps_width(FMT_TEXT) - 1, 12), 'lcd'))
    tw = art.time_width('dc')
    L.f['TIM'] = (10, 16)
    L.inner.append(('time', (10, 16, 10 + tw - 1, 31), 'lcd'))
    L.f['TOT'] = (10 + tw + 6, 24)
    L.inner.append(('total', (10 + tw + 6, 24, 10 + tw + 6 + 80 - 1, 31), 'lcd'))
    L.f['MCAP'] = (292, 6)
    L.f['MCAPW'] = max(art.caps_width(s) for s in MCAPS_CMP)
    L.f['MCAPS'] = ('caps', MCAPS_CMP)
    L.statics.append(('level caption', (292, 6, 292 + L.f['MCAPW'] - 1, 12), None))
    well = R(324, 2, 621, 15)
    L.sunken(well)
    L.ctl('well', well)
    meter_cells(L, art, x0=328, y0=5, h=8, well=well, peak_x=590, peak_y=6)
    L.caps(292, 22, 'INPUT GAIN (DIGITAL)')
    L.caps(416, 22, 'MIN')
    L.f['GSL'] = R(436, 18, 589, 33)
    L.ctl('gain slider', L.f['GSL'])
    L.caps(594, 22, 'MAX')
    # one row of keys: the picture beside the caption
    xs = [(3, 78), (82, 157), (161, 236), (240, 315), (329, 404), (408, 483)]
    caps = ['REC', 'STOP', 'PAUSE', 'PLAY', 'OPEN', 'SAVE']
    pics = ['h_rec', 'h_stop', 'h_pause', 'h_play', 'h_open', 'h_save']
    for i, ((x1, x2), c, p) in enumerate(zip(xs, caps, pics)):
        L.button(i, R(x1, 37, x2, 51), p, c)
    L.vgroove(321, 38, 50)
    L.button(6, R(528, 37, 573, 51), 'h_minus', None)
    L.button(7, R(577, 37, 621, 51), 'h_plus', None)
    L.f['WAVE'] = R(5, 56, 619, 97)
    L.f['RULH'] = 10
    L.sunken(R(3, 54, 621, 99))
    L.ctl('wave', R(3, 54, 621, 99))
    L.f['OVW'] = R(5, 102, 619, 107)
    L.sunken(R(3, 101, 621, 108))
    L.ctl('overview', R(3, 101, 621, 108))
    # footer row 1: selection, Loop, Trim, Delete, Save As
    sel = R(3, 110, 163, 120)
    L.sunken(sel, 'FIELD')
    L.ctl('selection field', sel)
    L.f['SELT'] = (8, 112)
    L.f['SELPFX'] = ('s', 'Sel: ')
    L.f['SELCH'] = (sel[2] - 1 - 8) // 8
    L.inner.append(('selection text', (8, 112, 8 + 8 * L.f['SELCH'] - 1, 119), 'selection field'))
    loop = R(168, 110, 219, 120)
    L.ctl('loop', loop)
    L.f['LOOP'] = loop
    L.f['LBOX'] = (170, 112)
    L.f['LTXT'] = (180, 112)
    L.f['LLBL'] = ('s', 'Loop')
    L.f['CHK'], L.f['UNCHK'] = ('p', 'h_check'), ('p', 'h_uncheck')
    L.inner.append(('loop box', (170, 112, 170 + 10, 118), 'loop'))
    L.inner.append(('loop label', (180, 112, 180 + 31, 119), 'loop'))
    L.button(8, R(226, 110, 281, 120), None, 'TRIM')
    L.button(9, R(286, 110, 349, 120), None, 'DELETE')
    L.button(10, R(354, 110, 435, 120), None, 'SAVE AS...')
    # footer row 2: Input, rate, bits, channels, VOLUME, status
    L.text(4, 123, 'Input:')
    drop_set(L, art, 122, 131, [(56, 107), (112, 183), (188, 247), (252, 303)],
             items=[['Mic'], ['8 kHz', '11 kHz', '15 kHz'], ['8-bit'], ['Mono']])
    L.caps(312, 124, 'VOL')
    L.f['VSL'] = R(332, 122, 431, 131)
    L.ctl('volume slider', L.f['VSL'])
    stat = R(438, 122, 621, 131)
    L.sunken(stat, 'STAT')
    L.ctl('status field', stat)
    L.f['STT'] = (442, 123)
    L.f['STCH'] = (stat[2] - 1 - 442) // 8
    L.inner.append(('status text', (442, 123, 442 + 8 * L.f['STCH'] - 1, 130), 'status field'))
    return L


def lcd_and_meter(L, art, lcd, fn_y, tim_y, fmt_y, lgrp, well, cell_y, cell_h,
                  scale_y, ggrp, gsl):
    """FULL's and MID's top: the LCD, LEVEL over INPUT GAIN."""
    L.sunken(lcd)
    L.ctl('lcd', lcd)
    L.f['LCD'] = lcd
    x = lcd[0] + 10
    L.f['FN'] = (x, fn_y)
    L.f['FNCH'] = FNAME_CH
    L.inner.append(('filename', (x, fn_y, x + 8 * FNAME_CH - 1, fn_y + 7), 'lcd'))
    dw, dh, _, _, _ = art.digit_metrics(L.dset)
    tw = art.time_width(L.dset)
    L.f['TIM'] = (x, tim_y)
    L.inner.append(('time', (x, tim_y, x + tw - 1, tim_y + dh - 1), 'lcd'))
    L.f['TOT'] = (x + tw + 8, tim_y + dh - 8)
    L.inner.append(('total', (x + tw + 8, tim_y + dh - 8, x + tw + 8 + 80 - 1,
                              tim_y + dh - 1), 'lcd'))
    L.f['FMT'] = (x, fmt_y)
    L.f['FMTCAPS'] = 0
    L.f['FMTCH'] = len(FMT_TEXT)
    L.inner.append(('format', (x, fmt_y, x + 8 * len(FMT_TEXT) - 1, fmt_y + 7), 'lcd'))
    L.groove(lgrp)
    L.f['MCAP'] = (lgrp[0] + 8, lgrp[1] - 3)
    L.f['MCAPW'] = max(art.caps_width(s) for s in MCAPS_FULL)
    L.f['MCAPS'] = ('caps', MCAPS_FULL)
    L.statics.append(('level caption', (lgrp[0] + 8, lgrp[1] - 3,
                                        lgrp[0] + 8 + L.f['MCAPW'] - 1, lgrp[1] + 3), None))
    L.sunken(well)
    L.ctl('well', well)
    meter_cells(L, art, x0=well[0] + 4, y0=cell_y, h=cell_h, well=well,
                peak_x=well[2] - 27, peak_y=cell_y + 5)
    x0 = well[0] + 4
    for i, s in ((0, '-30'), (8, '-20'), (16, '-10'), (24, '0')):
        cx = x0 + 9 * i + 3 if i < 24 else x0 + 9 * 23 + 6
        L.caps(cx - art.caps_width(s) // 2, scale_y, s)
    L.groove(ggrp)
    L.caps(ggrp[0] + 8, ggrp[1] - 3, 'INPUT GAIN (DIGITAL)')
    my = (gsl[1] + gsl[3]) // 2 - 3
    L.caps(ggrp[0] + 8, my, 'MIN')
    L.f['GSL'] = gsl
    L.ctl('gain slider', gsl)
    L.caps(gsl[2] + 6, my, 'MAX')


def meter_cells(L, art, x0, y0, h, well, peak_x, peak_y):
    L.f['CELL'] = (x0, y0)
    L.f['CELLW'], L.f['CELLH'], L.f['CELLP'] = 7, h, 9
    L.op('CELLS')
    L.inner.append(('cells', (x0, y0, x0 + 23 * 9 + 6, y0 + h - 1), 'well'))
    L.caps(peak_x, peak_y, 'PEAK', ink='LCDTXT', paper='LCD', inside='well')
    cx = peak_x + 11
    led = R(cx - 3, peak_y - 5, cx + 3, peak_y - 3) if not L.cmp else \
        R(peak_x - 10, peak_y + 1, peak_x - 5, peak_y + 5)
    L.f['PEAK'] = led
    L.inner.append(('peak led', led, 'well'))


def keys(L, y1, y2, zy1, zy2, gx):
    caps = ['RECORD', 'STOP', 'PAUSE', 'PLAY']
    pics = ['i_rec', 'i_stop', 'i_pause', 'i_play']
    for i in range(4):
        x1 = 8 + i * 70
        L.button(i, R(x1, y1, x1 + 65, y2), pics[i], caps[i])
    L.vgroove(gx, y1 + 3, y2 - 3)
    L.button(4, R(306, y1, 371, y2), 'i_open', 'OPEN')
    L.button(5, R(376, y1, 441, y2), 'i_save', 'SAVE')
    L.button(6, R(500, zy1, 545, zy2), 'i_minus', None)
    L.button(7, R(552, zy1, 597, zy2), 'i_plus', None)


def edit_row(L, art, y1, y2, sel_x2):
    sel = R(8, y1, sel_x2, y2)
    L.sunken(sel, 'FIELD')
    L.ctl('selection field', sel)
    ty = (y1 + y2) // 2 - 3
    L.f['SELT'] = (14, ty)
    L.f['SELPFX'] = ('s', 'Selection:  ')
    L.f['SELCH'] = (sel_x2 - 1 - 14) // 8
    L.inner.append(('selection text', (14, ty, 14 + 8 * L.f['SELCH'] - 1, ty + 7),
                    'selection field'))
    L.button(8, R(256, y1, 367, y2), None, 'TRIM')
    L.button(9, R(376, y1, 487, y2), None, 'DELETE')
    L.button(10, R(496, y1, 599, y2), None, 'SAVE AS...')


def format_row(L, art, y1, y2, loop_txt, input_lbl, drops):
    loop = R(12, y1, 151, y2)
    L.ctl('loop', loop)
    L.f['LOOP'] = loop
    by = (y1 + y2) // 2 - 5
    ty = (y1 + y2) // 2 - 3
    L.f['LBOX'] = (14, by)
    L.f['LTXT'] = (30, ty)
    L.f['LLBL'] = ('s', loop_txt)
    L.f['CHK'], L.f['UNCHK'] = ('p', 'check'), ('p', 'uncheck')
    L.inner.append(('loop box', (14, by, 24, by + 10), 'loop'))
    L.inner.append(('loop label', (30, ty, 30 + 8 * len(loop_txt) - 1, ty + 7), 'loop'))
    L.vgroove(158, y1 - 1, y2 + 1)
    if input_lbl:
        L.text(166, ty, 'Input:')
    drop_set(L, art, y1, y2, drops,
             items=[['Microphone'], ['8,000 Hz', '11,025 Hz', '15,000 Hz'], ['8-bit'], ['Mono']])


def drop_set(L, art, y1, y2, xs, items):
    rects = []
    for i, ((x1, x2), its) in enumerate(zip(xs, items)):
        r = R(x1, y1, x2, y2)
        rects.append(r)
        L.ctl('drop ' + DROPS[i], r)
        assert len(its) == DROP_N[i]
    L.f['DROPS'] = rects
    L.f['DRIT'] = items


def bottom_row(L, art, y1, y2, spk_x, vol, stat, caps_x, lo_x, hi_x):
    my = (y1 + y2) // 2
    L.pict(spk_x, my - 6, 'i_speaker')
    L.caps(caps_x, my - 3, 'VOLUME:')
    L.pict(lo_x, my - 4, 'spklo')
    L.f['VSL'] = vol
    L.ctl('volume slider', vol)
    L.pict(hi_x, my - 4, 'spkhi')
    L.sunken(stat, 'STAT')
    L.ctl('status field', stat)
    L.f['STT'] = (stat[0] + 6, my - 4)
    L.f['STCH'] = (stat[2] - 1 - stat[0] - 6) // 8
    L.inner.append(('status text', (stat[0] + 6, my - 4,
                                    stat[0] + 6 + 8 * L.f['STCH'] - 1, my + 3),
                    'status field'))


def layouts(art, bm):
    return [build_full(art, bm), build_mid(art, bm), build_cmp(art, bm)]


# =============================================================================
# THE MEASUREMENT
# =============================================================================
def overlap(a, b):
    return not (a[2] < b[0] or b[2] < a[0] or a[3] < b[1] or b[3] < a[1])


def inside(a, b, margin=0):
    return (a[0] >= b[0] + margin and a[1] >= b[1] + margin and
            a[2] <= b[2] - margin and a[3] <= b[3] - margin)


def measure(L, art, bm, report):
    bad = 0
    box = R(0, 0, L.w - 1, L.h - 1)

    def fail(msg):
        nonlocal bad
        print("layout %s: %s" % (L.name, msg))
        bad += 1

    sw, sh = L.screen
    fw, fh = L.w + 2, L.h + TITLE_H + 1
    if fw > sw or fh > sh - MBAR_H - DOCK_H:
        fail("its window, %d x %d, does not fit a %d x %d screen's desktop "
             "(%d rows under the menu bar and over the dock)"
             % (fw, fh, sw, sh, sh - MBAR_H - DOCK_H))
    ctl = dict(L.controls)
    for n, r in L.controls:
        if not inside(r, box):
            fail("control %s %r is outside the %d x %d content box" % (n, r, L.w, L.h))
        if r[2] - r[0] < 7 or r[3] - r[1] < 7:
            fail("control %s %r is under 8 x 8 - too small a hit target" % (n, r))
    for i, (na, a) in enumerate(L.controls):
        for nb, b in L.controls[i + 1:]:
            if overlap(a, b):
                fail("controls %s %r and %s %r overlap" % (na, a, nb, b))
    for n, r, ins in L.statics:
        if not inside(r, box):
            fail("static %s %r is outside the content box" % (n, r))
        if ins == 'frame':
            continue
        if ins:
            if not inside(r, ctl[ins], 1):
                fail("static %s %r is not inside %s" % (n, r, ins))
            continue
        for cn, c in L.controls:
            if overlap(r, c):
                fail("static %s %r overlaps control %s %r" % (n, r, cn, c))
    for n, r, cont in L.inner:
        if not inside(r, ctl[cont], 1):
            fail("%s %r is not inside %s %r with its bevel" % (n, r, cont, ctl[cont]))
    # the keys: picture and caption fit, stacked or side by side
    for i, r in enumerate(L.btns):
        if r is None:
            fail("key %s has no rect" % BUTTONS[i])
            continue
        iw, ih = r[2] - r[0] - 1, r[3] - r[1] - 1
        pw = ph = 0
        if L.pics[i]:
            b = bm[L.pics[i]]
            pw, ph = len(b[0]), len(b)
        cw = art.caps_width(L.bcaps[i]) if L.bcaps[i] else 0
        if L.side or not L.pics[i] or not L.bcaps[i]:
            need_w = 3 + pw + (3 if pw and cw else 0) + cw + 3
            need_h = max(ph, 7 if cw else 0) + 2
        else:
            need_w = max(pw, cw) + 4
            need_h = ph + 3 + 7 + 2
        if need_w > iw or need_h > ih:
            fail("key %s: %d x %d of picture and caption in a %d x %d inside"
                 % (BUTTONS[i], need_w, need_h, iw, ih))
        report.append("  key %-6s %3d x %2d inside, needs %3d x %2d  (%s)"
                      % (BUTTONS[i], iw, ih, need_w, need_h, L.bcaps[i] or L.pics[i]))
    # the drop-downs: the longest item, the margin, the arrow cell
    for i, (r, its) in enumerate(zip(L.f['DROPS'], L.f['DRIT'])):
        need = 4 + 8 * max(len(s) for s in its) + 2 + 12 + 1
        if need > r[2] - r[0] + 1:
            fail("drop %s: %r needs %d px, has %d" % (DROPS[i], its, need, r[2] - r[0] + 1))
        if r[3] - r[1] + 1 < 10:
            fail("drop %s is %d rows - its caption needs 10" % (DROPS[i], r[3] - r[1] + 1))
        report.append("  drop %-5s %3d px, longest %-11r needs %3d"
                      % (DROPS[i], r[2] - r[0] + 1, max(its, key=len), need))
    # the fields: the longest text each is ever given
    pfx = L.f['SELPFX'][1]
    if len(pfx) + SEL_VALUE > L.f['SELCH']:
        fail("the selection field holds %d characters; %r + %d is %d"
             % (L.f['SELCH'], pfx, SEL_VALUE, len(pfx) + SEL_VALUE))
    report.append("  selection field %d chars, needs %d (%r + 'SS.cc - SS.cc')"
                  % (L.f['SELCH'], len(pfx) + SEL_VALUE, pfx))
    if L.f['STCH'] < STATUS_MAX:
        fail("the status field holds %d characters; the messages are up to %d"
             % (L.f['STCH'], STATUS_MAX))
    report.append("  status field %d chars, longest message %d"
                  % (L.f['STCH'], STATUS_MAX))
    if L.f['FMTCH'] < len(FMT_TEXT):
        fail("the format line is too short")
    # the sliders' travel
    for n in ('GSL', 'VSL'):
        r = L.f[n]
        if r[2] - r[0] + 1 < 13 * 4 + THUMB_W:
            fail("slider %s is too short for 13 steps" % n)
        if r[3] - r[1] + 1 < 9:
            fail("slider %s is %d rows - its thumb needs 9" % (n, r[3] - r[1] + 1))
    return bad


def selfcheck(bm, art):
    bad = 0
    report = []
    for L in layouts(art, bm):
        report.append("%s %d x %d (window %d x %d on a %d x %d screen):"
                      % (L.name.upper(), L.w, L.h, L.w + 2, L.h + TITLE_H + 1,
                         L.screen[0], L.screen[1]))
        n0 = len(report)
        bad += measure(L, art, bm, report)
        report.insert(n0, "  %d controls, %d static parts, %d element texts - "
                      "no overlap, all inside" % (len(L.controls), len(L.statics),
                                                 len(L.inner)))
    if len(ROLES) != len(set(n for n, _, _ in ROLES)):
        print("two roles share a name")
        bad += 1
    for n, c, b in ROLES:
        if b not in ('CBLACK', 'CWHITE', 'P_DITHER'):
            print("role %s: the black-and-white theme uses only 0, 15 and "
                  "patterns" % n)
            bad += 1
    bad += status_check(report)
    if "--quiet" not in __import__('sys').argv:
        print("\n".join(report))
    return bad


# THE STATUS BAR'S 22 CHARACTERS, held to every message the source carries
# (SPEC.md 123.2): each literal a status label names - wr_s_, wc_s_, ww_s_,
# ww_e, wt_s_, we_s_, ws_s_, wx_s_ - read out of apps/waverack/ itself. A
# message built from a literal and a number ('Overrun: kept to ' + 'M:SS')
# is its own builder's to keep short; this holds every literal
STATUS_PFX = ('wr_s_', 'wc_s_', 'ww_s_', 'ww_e', 'wt_s_', 'we_s_', 'ws_s_',
              'wx_s_')


def status_check(report):
    import glob
    import os
    import re
    src = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..',
                       'apps', 'waverack')
    pat = re.compile(r"^(\w+):\s+db\s+'([^']*)'\s*,\s*0")
    n, longest, bad = 0, 0, 0
    for f in sorted(glob.glob(os.path.join(src, '*.asm')) +
                    glob.glob(os.path.join(src, '*.inc'))):
        for ln in open(f, encoding='latin-1'):
            m = pat.match(ln)
            if not m or not m.group(1).startswith(STATUS_PFX):
                continue
            n += 1
            longest = max(longest, len(m.group(2)))
            if len(m.group(2)) > STATUS_MAX:
                print("status message %s %r is %d characters, past %d"
                      % (m.group(1), m.group(2), len(m.group(2)), STATUS_MAX))
                bad += 1
    if not n:
        print("no status messages found under %s" % src)
        bad += 1
    report.append("status messages: %d in the source, the longest %d of %d"
                  % (n, longest, STATUS_MAX))
    return bad


# =============================================================================
# EMIT
# =============================================================================
def emit(bm, art):
    out = []
    strings = {}

    def sref(s):
        if s not in strings:
            strings[s] = "wrart_s%d" % len(strings)
        return strings[s]

    def val(v):
        if isinstance(v, tuple) and v and v[0] == 's':
            return sref(v[1])
        if isinstance(v, tuple) and v and v[0] == 'p':
            return "wrart_" + v[1]
        return str(v)

    out.append("; --- the roles (R_*) and the two themes, a byte a role ----------")
    for i, (n, _, _) in enumerate(ROLES):
        out.append("R_%s equ %d" % (n, i))
    out.append("WD_NROLES equ %d" % len(ROLES))
    out.append("wrart_tcol:")
    out.append("    db " + ", ".join(c for _, c, _ in ROLES))
    out.append("wrart_tbw:")
    out.append("    db " + ", ".join(b for _, _, b in ROLES))
    out.append("; --- the static-part ops (wf_deco) ---------------------------------")
    for i, n in enumerate(OPS):
        out.append("WO_%s equ %d" % (n, i))
    out.append("WO_SIZE equ 14")
    out.append("; --- the layout record ------------------------------------------")
    off = 0
    for n, w in FIELDS:
        out.append("WL_%s equ %d" % (n, off))
        off += 2 * w
    out.append("WL_SIZE equ %d" % off)
    out.append("WR_NB equ %d" % len(BUTTONS))
    for i, n in enumerate(BUTTONS):
        out.append("B_%s equ %d" % (n, i))
    out.append("WR_NDROP equ %d" % len(DROPS))
    for i, n in enumerate(DROP_N):
        out.append("WR_DROPN%d equ %d" % (i, n))
    out.append("WF_THUMB equ %d" % THUMB_W)
    # each element's key slot (wrface.inc reaches its neighbours' by these)
    seen = set()
    for i, (p, _) in enumerate(ELEMS):
        if p not in seen:
            seen.add(p)
            out.append("WRE_%s equ %d" % (p[5:].upper(), i))
    out.append("WRE_N equ %d" % len(ELEMS))
    Ls = layouts(art, bm)
    band = 0
    for L in Ls:
        # the waveform band (SPEC.md 123.5): the ruler's rows, the lanes'
        # and the overview's, at the stride the widest x phase needs
        r, o = L.f['WAVE'], L.f['OVW']
        assert (r[0], r[2]) == (o[0], o[2]), "the overview shares the x"
        rows = (r[3] - r[1] + 1) + (o[3] - o[1] + 1)
        band = max(band, ((r[2] - r[0] + 1 + 14) // 8) * rows)
        N = L.name.upper()
        out.append("WRL_%s_W equ %d" % (N, L.w))
        out.append("WRL_%s_H equ %d" % (N, L.h))
    out.append("WRL_BANDKB equ %d                 ; the waveform band's claim"
               % ((band + 1023) // 1024))
    out.append("WRL_BANDSZ equ %d" % band)
    for L in Ls:
        N = L.name
        out.append("wrart_l_%s:" % N)
        f = dict(L.f)
        f.update({'W': L.w, 'H': L.h, 'DECO': 'wrart_o_%s' % N,
                  'ELEMS': 'wrart_e_%s' % N, 'BTNS': 'wrart_b_%s' % N,
                  'ICONS': 'wrart_i_%s' % N, 'CAPS': 'wrart_c_%s' % N,
                  'SIDE': L.side, 'CMP': L.cmp, 'DSET': 'wd_ds_%s' % L.dset,
                  'MCAPS': 'wrart_m_%s' % N,
                  'DRIT': ['wrart_d_%s_%d' % (N, i) for i in range(len(DROPS))]})
        for n, w in FIELDS:
            v = f[n]
            if n == 'DROPS':
                v = [c for r in v for c in r]
            if not isinstance(v, (list, tuple)) or (v and v[0] in ('s', 'p')):
                v = [v]
            assert len(v) == w, (L.name, n, v)
            out.append("    dw " + ", ".join(val(x) for x in v) + "   ; WL_" + n)
        out.append("wrart_o_%s:" % N)
        for (o, ra, rb, arg, a, b, c, d, p) in L.ops:
            out.append("    db %d, %d, %d, %d" % (o, ra, rb, arg))
            out.append("    dw %d, %d, %d, %d, %s" % (a, b, c, d, val(p) if p else 0))
        out.append("    db WO_END, 0, 0, 0")
        out.append("wrart_e_%s:" % N)
        for p, a in ELEMS:
            out.append("    dw %s, %d" % (p, a))
        out.append("    dw 0")
        out.append("wrart_b_%s:" % N)
        for r in L.btns:
            out.append("    dw %d, %d, %d, %d" % r)
        out.append("wrart_i_%s:" % N)
        out.append("    dw " + ", ".join("wrart_" + p if p else "0" for p in L.pics))
        out.append("wrart_c_%s:" % N)
        out.append("    dw " + ", ".join(sref(c) if c else "0" for c in L.bcaps))
        out.append("wrart_m_%s:" % N)
        out.append("    dw " + ", ".join(sref(s) for s in L.f['MCAPS'][1]))
        for i, its in enumerate(L.f['DRIT']):
            out.append("wrart_d_%s_%d:" % (N, i))
            out.append("    dw " + ", ".join(sref(s) for s in its))
    out.append("; --- the strings the layouts name -------------------------------")
    for s, lab in strings.items():
        out.append("%s: db '%s', 0" % (lab, s))
    return out


# =============================================================================
# PREVIEWS - a rough picture of each layout in each theme (--png), so the
# geometry can be LOOKED at before any asm is written. System-font text is a
# grey bar per character; everything else is drawn from the same data.
# =============================================================================
VGA = {'CBLACK': (0, 0, 0), 'CBLUE': (0, 0, 170), 'CGREEN': (0, 170, 0),
       'CCYAN': (0, 170, 170), 'CRED': (170, 0, 0), 'CMAGENTA': (170, 0, 170),
       'CBROWN': (170, 85, 0), 'CLGRAY': (170, 170, 170), 'CDGRAY': (85, 85, 85),
       'CLBLUE': (85, 85, 255), 'CLGREEN': (85, 255, 85), 'CLCYAN': (85, 255, 255),
       'CLRED': (255, 85, 85), 'CLMAGENTA': (255, 85, 255), 'CYELLOW': (255, 255, 85),
       'CWHITE': (255, 255, 255), 'P_DITHER': (128, 128, 128)}
PAL = list(VGA)


def previews(d, bm, art):
    import os
    for L in layouts(art, bm):
        for theme in (1, 2):
            img = [[PAL.index('CLGRAY')] * L.w for _ in range(L.h)]
            col = {r[0]: PAL.index(r[theme]) for r in ROLES}

            def fill(r, c):
                for y in range(max(0, r[1]), min(L.h, r[3] + 1)):
                    for x in range(max(0, r[0]), min(L.w, r[2] + 1)):
                        img[y][x] = c

            def frame(r, c):
                fill((r[0], r[1], r[2], r[1]), c)
                fill((r[0], r[3], r[2], r[3]), c)
                fill((r[0], r[1], r[0], r[3]), c)
                fill((r[2], r[1], r[2], r[3]), c)

            def blit(b, x, y, c):
                for yy, row in enumerate(b):
                    for xx, v in enumerate(row):
                        if v and 0 <= y + yy < L.h and 0 <= x + xx < L.w:
                            img[y + yy][x + xx] = c

            def caps(s, x, y, c, gap=0):
                blit(art.caps_text(s, gap), x, y, c)

            def text(s, x, y, c):
                for i, ch in enumerate(s):
                    if ch != ' ':
                        fill((x + 8 * i + 1, y + 1, x + 8 * i + 6, y + 6), c)
            for (o, ra, rb, arg, a, b, c, dd, p) in L.ops:
                rn = ROLES[ra][0]
                if OPS[o] in ('RAISED', 'SUNKEN'):
                    fill((a, b, c, dd), col[rn])
                    t, s = (col['HI'], col['SH']) if OPS[o] == 'RAISED' else (col['SH'], col['HI'])
                    fill((a, b, c, b), t)
                    fill((a, b, a, dd), t)
                    fill((a, dd, c, dd), s)
                    fill((c, b, c, dd), s)
                elif OPS[o] == 'GROOVE':
                    frame((a, b, c, dd), col['SH'])
                    frame((a + 1, b + 1, c + 1, dd + 1), col['GLINT'])
                elif OPS[o] == 'VGROOVE':
                    fill((a, b, a, dd), col['SH'])
                    fill((a + 1, b, a + 1, dd), col['GLINT'])
                elif OPS[o] == 'CAPS':
                    w = art.caps_width(p[1], arg)
                    fill((a, b, a + w - 1, b + 6), col[ROLES[rb][0]])
                    caps(p[1], a, b, col[rn], arg)
                elif OPS[o] == 'TEXT':
                    text(p[1], a, b, col[rn])
                elif OPS[o] == 'PIC':
                    blit(bm[p[1]], a, b, col[rn])
                elif OPS[o] == 'GRILLE':
                    for y in range(b, dd + 1, arg):
                        fill((a, y, c, y), col[rn])
                elif OPS[o] == 'CELLS':
                    x0, y0 = L.f['CELL']
                    for i in range(24):
                        r = (x0 + 9 * i, y0, x0 + 9 * i + 6, y0 + L.f['CELLH'] - 1)
                        (frame if theme == 2 else fill)(r, col['LEDOFF'])
            # the elements, as an empty document draws them
            x, y = L.f['FN']
            text('NO FILE', x, y, col['LCDTXT'])
            x, y = L.f['TIM']
            dw, dh, cw, pw, gap = art.digit_metrics(L.dset)
            for ch in '00:00.00':
                key = {':': '%s_col' % L.dset, '.': '%s_dot' % L.dset}.get(ch, '%s_0' % L.dset)
                blit(bm[key], x, y, col['DIGIT'])
                x += len(bm[key][0]) + gap
            x, y = L.f['TOT']
            text('/ 00:00.00', x, y, col['LCDTXT'])
            x, y = L.f['FMT']
            if L.f['FMTCAPS']:
                caps(FMT_TEXT, x, y, col['LCDTXT'])
            else:
                text(FMT_TEXT, x, y, col['LCDTXT'])
            x, y = L.f['MCAP']
            fill((x, y, x + L.f['MCAPW'] - 1, y + 6), col['BG'])
            caps('LEVEL', x, y, col['LABEL'])
            frame(L.f['PEAK'], col['LEDOFF'])
            for n, v in (('GSL', 6 / 12), ('VSL', 0.75)):
                r = L.f[n]
                gy = (r[1] + r[3]) // 2
                fill((r[0], gy - 1, r[2], gy + 1), col['SLINK'])
                tx = r[0] + int(v * (r[2] - r[0] - THUMB_W + 1))
                fill((tx, r[1], tx + THUMB_W - 1, r[3]), col['BG'])
                frame((tx, r[1], tx + THUMB_W - 1, r[3]), col['SLINK'])
            w = L.f['WAVE']
            fill((w[0], w[1] + L.f['RULH'] + (w[3] - w[1] - L.f['RULH']) // 2,
                  w[2], w[1] + L.f['RULH'] + (w[3] - w[1] - L.f['RULH']) // 2), col['WAVE'])
            for i in range(51):
                tx = w[0] + i * (w[2] - w[0]) // 50
                fill((tx, w[1] + L.f['RULH'] - (6 if i % 5 == 0 else 3), tx,
                      w[1] + L.f['RULH'] - 1), col['RULER'])
            o = L.f['OVW']
            fill((o[0], (o[1] + o[3]) // 2, o[2], (o[1] + o[3]) // 2), col['WAVE'])
            x, y = L.f['SELT']
            text(L.f['SELPFX'][1], x, y, col['FIELDTXT'])
            x, y = L.f['STT']
            text('Ready'.center(L.f['STCH']), x, y, col['STATTXT'])
            for r, its in zip(L.f['DROPS'], L.f['DRIT']):
                fill(r, col['FIELD'])
                frame(r, col['BTNEDGE'])
                fill((r[2] - 12, r[1], r[2] - 12, r[3]), col['BTNEDGE'])
                text(its[-1], r[0] + 4, (r[1] + r[3] - 7) // 2, col['BTNTXT'])
            x, y = L.f['LBOX']
            blit(bm[L.f['UNCHK'][1]], x, y, col['LABEL'])
            x, y = L.f['LTXT']
            text(L.f['LLBL'][1], x, y, col['LABEL'])
            for i, r in enumerate(L.btns):
                fill(r, col['BTN'])
                frame(r, col['BTNEDGE'])
                pb = bm[L.pics[i]] if L.pics[i] else None
                cw = art.caps_width(L.bcaps[i]) if L.bcaps[i] else 0
                iw, ih = r[2] - r[0] - 1, r[3] - r[1] - 1
                ink = col['BTNDIS'] if i not in (6, 7) else col['BTNDIS']
                if L.side or not pb or not L.bcaps[i]:
                    tw = (len(pb[0]) if pb else 0) + (3 if pb and cw else 0) + cw
                    x = r[0] + 1 + (iw - tw) // 2
                    if pb:
                        blit(pb, x, r[1] + 1 + (ih - len(pb)) // 2,
                             col['REC'] if i == 0 and theme == 1 else ink)
                        x += len(pb[0]) + 3
                    if cw:
                        caps(L.bcaps[i], x, r[1] + 1 + (ih - 7) // 2, ink)
                else:
                    th = len(pb) + 4 + 7
                    y = r[1] + 1 + (ih - th) // 2
                    blit(pb, r[0] + 1 + (iw - len(pb[0])) // 2, y,
                         col['REC'] if i == 0 and theme == 1 else ink)
                    caps(L.bcaps[i], r[0] + 1 + (iw - cw) // 2, y + len(pb) + 4, ink)
            sc = 2
            rows = []
            for row in img:
                line = bytearray()
                for px in row:
                    line += bytes([px]) * sc
                rows.extend([line] * sc)
            art.write_png(os.path.join(d, "layout-%s-%s.png" % (L.name, 'col' if theme == 1 else 'bw')),
                          rows, L.w * sc, L.h * sc, [VGA[n] for n in PAL])
