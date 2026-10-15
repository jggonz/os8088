#!/usr/bin/env python3
"""Pixel checks for Color presets on QEMU's BIOS mode 10h/12h renderer.

Build the image first. --ega and --mono-smoke want a knob kernel, built into
a PRIVATE tree so build/ stays the shipping one (CLAUDE.md's knob trap):
  make BUILD=build-ega VIDEO=ega
  OS88_TREE=$PWD/build-ega OS88_BUILD=$PWD/build-ega \
      python3 tests/colorpresets_qemu.py --ega --image build-ega/os8088-360.img
and the same with `KERN_SMALL=1 VIDEO=cga` and --mono-smoke. This is not an
EGA hardware emulation: QEMU's standard VGA runs the actual EGA BIOS mode 10h.
The debugger calls the existing guest functions and restores CPU state and
the temporary trampoline after each call. No instrumentation ships in the OS.
"""
import argparse
import json
import os
from pathlib import Path
import socket
import shutil
import struct
import subprocess
import sys
import time

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools'))
sys.path.insert(0, str(ROOT / 'tests'))
import os88sym
import os88build
import os88qemu

PALETTE = [(0,0,0),(0,0,170),(0,170,0),(0,170,170),(170,0,0),
           (170,0,170),(170,85,0),(170,170,170),(85,85,85),(85,85,255),
           (85,255,85),(85,255,255),(255,85,85),(255,85,255),(255,255,85),(255,255,255)]
# QEMU's BIOS DAC gives 168/87 for reference 170/85. Allow its two-unit
# component quantization without changing any production palette or index.
def matches(pixel, index):
    return all(abs(a-b) <= 3 for a,b in zip(pixel, PALETTE[index]))
def index(pixel):
    return min(range(16), key=lambda i: sum((a-b)**2 for a,b in zip(pixel, PALETTE[i])))
# Color's eight rows, read from the one table the boot reader and THMPAL.DRV
# both assemble (drivers/thmpal/thmrows.inc, SPEC.md 76.14.14) - so this test
# cannot disagree with them about a colour.
def _rows():
    import re
    src = (ROOT/'drivers'/'thmpal'/'thmrows.inc').read_text()
    body = src[src.index('%macro THM_PRESET_ROWS'):src.index('%endmacro')]
    rows = []
    for line in body.splitlines():
        m = re.match(r'\s*db\s+([^;]+)', line)
        if m:
            rows.append([int(v.strip(), 0) for v in m.group(1).split(',')])
    assert len(rows) == 8 and all(len(r) == 10 for r in rows), rows
    return rows
ROWS = _rows()
# ID, desktop, active title, inactive title, menu XOR mask, inactive ink.
PRESETS = [(i, r[5], r[3], r[7], r[9], r[8]) for i, r in enumerate(ROWS)]
# Customize has three radio rows after the eight preset rows: Solid, Stripes,
# Windows. The 95 checkbox sits beside the Windows row.
STYLE_Y = 3 + len(PRESETS)*11 + 8 + 10
STYLE_PITCH = 16
CLASSIC_X = 91

def port():
    with socket.socket() as s:
        s.bind(('127.0.0.1', 0))
        return s.getsockname()[1]

class Guest:
    def __init__(self, args):
        qp, gp = port(), port()
        # build/*.pid, so os88test's sweep finds it after a timed-out row
        self.pidfile = str(Path(os88build.at('build')).resolve()/f'colorpresets-{qp}.pid')
        os88qemu.own(pidfile=self.pidfile,sock=None)
        self.proc = subprocess.Popen([args.qemu, '-machine', 'pc,vmport=off',
            '-m', '640', '-vga', 'std', '-display', 'none', '-S',
            '-drive', f'file={Path(args.image).resolve()},format=raw,if=floppy',
            '-qmp', f'tcp:127.0.0.1:{qp},server=on,wait=off',
            '-gdb', f'tcp:127.0.0.1:{gp}', '-pidfile', self.pidfile, '-no-reboot'],
            stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        try:
            for _ in range(100):
                try:
                    self.q = socket.create_connection(('127.0.0.1', qp), timeout=2)
                    break
                except OSError:
                    if self.proc.poll() is not None:
                        raise RuntimeError(self.proc.stderr.read().decode())
                    time.sleep(.05)
            else:
                raise RuntimeError('QMP did not start')
            self.f = self.q.makefile('rwb')
            self.f.readline()
            self.qmp('qmp_capabilities')
            self.g = socket.create_connection(('127.0.0.1', gp), timeout=10)
            self.rsp('?')
            self.qmp('cont')
            time.sleep(3)
            self.qmp('stop')
            # Enter on the UI task's normal stack, outside the graphics lock.
            # Injecting disk/UI work on the idle task trips its stack guard.
            entry = os88sym.linear('ui_task.loop')
            assert self.rsp(f'Z0,{entry:x},1') == 'OK'
            assert self.rsp('c').startswith(('T05', 'S05'))
            assert self.rsp(f'z0,{entry:x},1') == 'OK'
        except BaseException:
            self.close()
            raise

    def close(self):
        self.proc.terminate()
        self.proc.wait(timeout=10)
        Path(self.pidfile).unlink(missing_ok=True)
        for name in ('f','q','g'):
            resource = getattr(self,name,None)
            if resource is not None:
                resource.close()

    def qmp(self, name, **kw):
        self.f.write((json.dumps({'execute': name, 'arguments': kw})+'\n').encode())
        self.f.flush()
        while True:
            reply = json.loads(self.f.readline())
            if 'event' not in reply:
                if 'error' in reply:
                    raise RuntimeError(reply)
                return reply.get('return')

    def rsp(self, command):
        data = command.encode()
        self.g.sendall(b'$'+data+b'#'+f'{sum(data)&255:02x}'.encode())
        while True:
            c = self.g.recv(1)
            if not c:
                raise EOFError('GDB disconnected before reply')
            if c == b'$':
                break
        reply = bytearray()
        while True:
            c = self.g.recv(1)
            if not c:
                raise EOFError('GDB disconnected during reply')
            if c == b'#':
                break
            reply += c
        self.g.recv(2)
        self.g.sendall(b'+')
        result = reply.decode()
        if result.startswith('E'):
            raise RuntimeError(f'GDB {command[:50]}: {result}')
        return result

    def read(self, address, size):
        return bytes.fromhex(self.rsp(f'm{address:x},{size:x}'))

    def write(self, address, data):
        assert self.rsp(f'M{address:x},{len(data):x}:{data.hex()}') == 'OK'

    def call(self, name=None, api=None, ax=0, bx=None, si=None, bp=None, cx=None, dx=None, di=None, near=False, count=None):
        """Call a guest entry on its real stack, retaining interrupt state.

        Slot 0010 is restored before normal execution. Far/cold entries use
        a far CALL; resident entries a near CALL. The breakpoint precedes any
        guest interrupt, and the entire CPU register packet is restored.
        """
        saved = self.rsp('g')
        target = api if api is not None else os88sym.syms()[name]
        seg = 0x60 if api is not None else os88sym.segment_of(name)
        address = (os88sym.linear('cpf_cp_open') if near else 0x610)
        offset = address-seg*16 if near else 0x10
        code = (b'\xe8'+struct.pack('<H', (target-offset-3)&65535) if (seg == 0x60 and api is None) or near
                else b'\x9a'+struct.pack('<HH', target, seg))
        old = self.read(address, 8)
        self.write(address, code+b'\x90'*(8-len(code)))
        stop = address+len(code)
        assert self.rsp(f'Z0,{stop:x},1') == 'OK'
        regs = bytearray.fromhex(saved)
        for index, value in {0: ax, 8: offset, 10: seg if near else 0x60, 12: 0x60, 13: 0x60}.items():
            struct.pack_into('<I', regs, index*4, value)
        if bx is not None:
            struct.pack_into('<I', regs, 3*4, bx)
        if si is not None:
            struct.pack_into('<I', regs, 6*4, si)
        for index,value in [(5,bp),(1,cx),(2,dx),(7,di)]:
            if value is not None:
                struct.pack_into('<I',regs,index*4,value)
        flags = struct.unpack_from('<I', regs, 9*4)[0]
        struct.pack_into('<I', regs, 9*4, flags | 0x200)
        counted = os88sym.linear(count) if count else None
        self.last_count = 0
        if counted is not None:
            assert self.rsp(f'Z0,{counted:x},1') == 'OK'
        try:
            assert self.rsp('G'+regs.hex()) == 'OK'
            while True:
                assert self.rsp('c').startswith(('T05', 'S05'))
                result = bytes.fromhex(self.rsp('g'))
                pc = (struct.unpack_from('<I',result,10*4)[0] * 16
                      + struct.unpack_from('<I',result,8*4)[0])
                if pc == stop:
                    break
                assert pc == counted, ('unexpected breakpoint',pc,counted)
                self.last_count += 1
                assert self.last_count < 1000, 'unexpected drawing loop'
                assert self.rsp('s').startswith(('T05', 'S05'))
        except BaseException:
            self.qmp('stop')
            print(self.qmp('human-monitor-command', **{'command-line':'info registers'}), flush=True)
            raise
        finally:
            if counted is not None:
                self.rsp(f'z0,{counted:x},1')
            self.rsp(f'z0,{stop:x},1')
            self.write(address, old)
            self.rsp('G'+saved)
        return struct.unpack_from('<I',result)[0]

    def frame(self, path):
        # UI passes may show the pointer; exclude this transient overlay
        # from application-content comparisons consistently.
        self.call('cursor_hide')
        try:
            self.qmp('screendump', filename=str(path.resolve()))
        finally:
            self.call('cursor_show')
        return Image.open(path).convert('RGB')

    def ui_pass(self):
        """Let the normal UI loop consume a posted repaint, without forcing it."""
        entry = os88sym.linear('ui_task.loop')
        assert self.rsp('s').startswith(('T05', 'S05'))
        assert self.rsp(f'Z0,{entry:x},1') == 'OK'
        try:
            assert self.rsp('c').startswith(('T05', 'S05'))
        finally:
            assert self.rsp(f'z0,{entry:x},1') == 'OK'

THM_SET = 0x046D                 # OSAPI_THM_SET (apps/os88api.inc)
THM_COLOR = 2

def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--qemu', default='qemu-system-i386')
    ap.add_argument('--image', default=os88build.at('build/os8088-360.img'))
    ap.add_argument('--ega', action='store_true')
    ap.add_argument('--visual-only', action='store_true', help='skip save/reboot phase')
    ap.add_argument('--mono-smoke', action='store_true', help='small kernel forced CGA; use OS88_BUILD')
    args = ap.parse_args()
    os88sym.default_defines(*(['KERN_SMALL','VID_FORCE=3'] if args.mono_smoke else
                           ['KERN_BIG']+(['VID_FORCE=4'] if args.ega else [])))
    symbols = os88sym.syms()  # verifies exact identity against kernel.bin
    out = Path(os88build.at('build'))/('color-cga' if args.mono_smoke else 'color-ega' if args.ega else 'color-vga')
    out.mkdir(exist_ok=True)
    source = Path(args.image)
    args.image = str(out/'test.img')
    shutil.copyfile(source,args.image)  # all guest writes stay in this copy
    lin = os88sym.linear
    g = Guest(args)
    try:
        mode = g.read(0x449, 1)[0]
        if args.mono_smoke:
            assert mode == 6
            g.call('fmf_files_open')
            before = g.frame(out/'before.ppm')
            g.call(api=THM_SET,ax=THM_COLOR)
            after = g.frame(out/'after.ppm')
            assert before.tobytes() == after.tobytes()
            assert g.read(lin('thm_kind'),1) == b'\0'
            assert set(after.getdata()) <= {(0,0,0),(255,255,255)}
            print('PASS kern_small CGA mode 06: File Manager boots; Color API no-ops; monochrome pixels unchanged')
            return
        eq = os88sym.equates()
        crow = lin('thm_tab') + THM_COLOR*10       # the Color row (THM_CROW)
        cfg = lin('drv_cfg') + eq['CFG_THMP']      # 'TP': id, style, colours
        bits = lin('thm_cstrp')                    # stripes / 95 / Windows

        def install(pid, style=None):
            """THMPAL.DRV's tv_install: the row over the Color row, the id in
            TP and the style bits into [thm_cstrp]."""
            g.write(crow, bytes(ROWS[pid]))
            g.write(cfg, bytes([pid]))
            if style is not None:
                g.write(bits, bytes([style]))

        def set_preset(pid, style=None):
            install(pid, style)
            g.call(api=THM_SET, ax=THM_COLOR)

        def top_window():
            slot = g.read(lin('wm_zord')+g.read(lin('wm_zn'),1)[0]-1,1)[0]
            ptr = symbols['wm_wins']+slot*eq['WIN_SIZE']
            raw = g.read(0x600+ptr,eq['WIN_SIZE'])
            return ptr, raw, struct.unpack_from('<HHHH',raw,2)

        assert mode == (0x10 if args.ega else 0x12), f'BIOS video mode {mode:02x}'
        assert g.read(lin('vid_kind'),1)[0] == (3 if args.ega else 0)
        set_preset(0, 0)  # isolate solid-preset checks from saved preferences
        g.call('fmf_files_open')
        g.call('cpf_cp_open')
        # Inspect the real window table, not theme table values alone.
        windows = []
        base = lin('wm_wins')
        for slot in range(eq['MAX_WIN']):
            raw = g.read(base+slot*eq['WIN_SIZE'], eq['WIN_SIZE'])
            if int.from_bytes(raw[:2],'little') & 3 == 3:
                windows.append((symbols['wm_wins']+slot*eq['WIN_SIZE'],
                    *struct.unpack_from('<HHHH',raw,2)))
        assert len(windows) >= 2, windows
        # Keep the real File Manager in front and move Control Panel aside.
        fm, cp = windows[:2]
        g.write(base+(cp[0]-symbols['wm_wins'])+2, struct.pack('<HH',350,100))
        g.call('wm_raise', bx=fm[0])
        for pid, desk, title, inactive, xorm, iink in PRESETS:
            set_preset(pid)
            g.ui_pass()
            image = g.frame(out/f'preset-{pid}.ppm')
            w,h = image.size
            assert matches(image.getpixel((w-10,h-45)), desk), (pid,'desktop')
            fx,fy,fw,fh = fm[1:]
            # Blank interior band in the real File Manager: never chrome gray.
            assert matches(image.getpixel((fx+fw-25,fy+fh-45)),15), (pid,'File Manager paper')
            assert matches(image.getpixel((fx+fw-40,fy+2)),title), (pid,'active title')
            assert matches(image.getpixel((450,102)),inactive), (pid,'inactive title')
            assert matches(image.getpixel((w-40,3)),7), (pid,'menu paper')
            assert g.read(lin('thm_xorm'),1)[0] == xorm, (pid,'live XOR mask')
            # The real menu's first item, XORed with the mask menu_drop takes,
            # and XORed again: SPEC.md 76.14.14's bar is its own undo.
            for label,value in [('menu_x1',450),('menu_y1',30),
                    ('menu_x2',620),('menu_y2',48),
                    ('menu_iptr',symbols['menu_items_logo']),('menu_dseg',0)]:
                g.write(lin(label),struct.pack('<H',value))
            g.write(lin('menu_xorm'), g.read(lin('thm_xorm'),1))
            g.call('menu_item_xor',ax=0)
            selected = g.frame(out/f'selection-{pid}.ppm')
            for px in range(452, 619, 7):       # the cell: x1+1..x2-1, y1+1..+16
                for py in range(31, 47, 3):
                    want = index(image.getpixel((px,py))) ^ xorm
                    assert matches(selected.getpixel((px,py)),want), (pid,'XOR mask',px,py)
            g.call('menu_item_xor',ax=0)
            restored = g.frame(out/f'restored-{pid}.ppm')
            assert restored.tobytes() == image.tobytes(), (pid,'XOR left a mark')
            image.save(out/f'preset-{pid}.png')
            print(f'PASS mode {mode:02x}, preset {pid}: desktop, titles, white File Manager, menu XOR bar on and off',flush=True)
        # Windows wears the selected preset (76.14.13): its title pair and its
        # buttons take the Color row. Check the bar and the minimize face.
        fx,fy,fw,fh = fm[1:]
        for pid, desk, title, *_ in PRESETS:
            set_preset(pid, 4)
            assert g.read(lin('thm_kind'),1) == b'\3', ('Windows style not live',pid)
            assert g.read(lin('thm_tbgc'),1) == bytes([title]), ('Windows active title role',pid)
            g.call('wm_paint_all')
            image = g.frame(out/f'windows-preset-{pid}.ppm')
            assert matches(image.getpixel((image.width-10,image.height-45)),desk), ('Windows desktop color',pid)
            assert matches(image.getpixel((fx+100,fy+5)),title), ('Windows title bar color',pid)
            assert matches(image.getpixel((fx+fw-46,fy+4)),title), ('Windows minimize button color',pid)
        set_preset(0, 0)
        assert g.read(lin('thm_kind'),1) == b'\2'
        print('PASS Windows title style: selected preset colors reach active bars and button faces',flush=True)

        # --- THE CHOOSER, through the panel's own Customize button ----------
        def press_customize():
            """cpf_cp_onclick then cpf_cp_onup on Customize (control id 8): the
            path a mouse click takes, so cp_thmv_open in CTRL.DRV is what runs."""
            g.write(lin('cp_sel'), bytes([eq['CP_ITHM']]))
            raw = g.read(0x600+cp[0],eq['WIN_SIZE'])
            wx,wy = struct.unpack_from('<HH',raw,2)
            bx_ = wx + 1 + eq['CP_RX'] + eq['CPH_CZX'] + 20
            by_ = wy + eq['TITLE_H'] + eq['CPH_CZY'] + 8
            g.call('cpf_cp_onclick', near=True, si=cp[0], cx=bx_, dx=by_)
            g.call('cpf_cp_onup', near=True, si=cp[0], cx=bx_, dx=by_)

        def chooser():
            ptr,raw,(xx,yy,www,hhh) = top_window()
            return (ptr, xx, yy,
                    struct.unpack_from('<H',raw,eq['W_ONCLICK'])[0],
                    struct.unpack_from('<H',raw,eq['W_ONKEY'])[0])

        row = lin('thmv_row')

        # Open the actual APPS folder using its FAT12 directory cluster.
        disk = source.read_bytes()
        root = (struct.unpack_from('<H',disk,14)[0] + disk[16]*struct.unpack_from('<H',disk,22)[0])*512
        entries = [disk[i:i+32] for i in range(root,root+struct.unpack_from('<H',disk,17)[0]*32,32)]
        apps = next(e for e in entries if e[:11] == b'APPS       ')
        g.write(lin('dsk_cwd'),apps[26:28])
        g.call('fmf_files_open')
        for pid,*_ in PRESETS:
            set_preset(pid)
            g.call('wm_paint_all')
            raw = g.read(0x600+fm[0],eq['WIN_SIZE'])
            x,y,ww,hh = struct.unpack_from('<HHHH',raw,2)
            frame = g.frame(out/f'folder-{pid}.ppm')
            assert matches(frame.getpixel((x+ww-25,y+hh-45)),15), (pid,'folder paper')
        # Launch existing Paint, then compare every pixel of its content.
        g.write(lin('ld_lname'),b'PAINT.O88\0')
        result = g.call('ld_run_name_x',near=True,si=symbols['ld_lname'])
        assert result & 255 == 0, ('Paint launch',result)
        paint_win,raw,(x,y,ww,hh) = top_window()
        # Exclude the WM-owned resize grip at bottom right, which is chrome.
        rect = (x+1,y+19,x+ww-17,y+hh-1)
        g.call('wm_paint_all')
        expected = g.frame(out/'paint-original.ppm').crop(rect).tobytes()
        for pid,*_ in PRESETS:
            set_preset(pid)
            g.call('wm_paint_all')
            frame = g.frame(out/f'paint-{pid}.ppm')
            assert frame.crop(rect).tobytes() == expected, (pid,'Paint content/palette changed')
        print('PASS all presets: APPS folder white; existing Paint content/palette pixel-identical',flush=True)

        def style(n):
            """Click the Title bar style group's row n: Solid, Stripes, Windows."""
            g.call('wm_pkgcall',bp=onclick,si=ch,cx=xx+15,dx=yy+19+STYLE_Y+n*STYLE_PITCH+8,
                   count='thm_set')

        def box95():
            g.call('wm_pkgcall',bp=onclick,si=ch,cx=xx+1+CLASSIC_X+5,
                   dx=yy+19+STYLE_Y+2*STYLE_PITCH+8)

        set_preset(2, 0)                       # Color, Light, Solid
        press_customize()
        assert struct.unpack('<H',g.read(row+2,2))[0], 'Customize did not load THMPAL.DRV'
        ch, xx, yy, onclick, onkey = chooser()
        frame = g.frame(out/'customize.ppm')
        overlay = Path(os88build.at('build/thmpal.bin')).read_bytes()
        assert b'Light\0' in overlay and b'Classic\0' in overlay and b'Color Theme\0' in overlay
        assert b'95\0' in overlay and b'Ocean\0' not in overlay
        # Click every visible row: its row lands in the Color row, its id in TP.
        for r,(pid,*_) in enumerate(PRESETS):
            g.call('wm_pkgcall',bp=onclick,si=ch,cx=xx+25,dx=yy+19+3+r*11+5,count='font_run_x')
            assert g.last_count == 0, ('row callback drew text itself',r,g.last_count)
            assert g.read(cfg,1) == bytes([pid]), ('list order',r,pid)
            assert list(g.read(crow,10)) == ROWS[pid], ('row not installed',pid)
            g.ui_pass()
            preview = g.frame(out/f'list-preview-{pid}.ppm')
            assert matches(preview.getpixel((preview.width-10,preview.height-45)),PRESETS[pid][1]), ('posted preview',pid)
        # Escape is Cancel: the row, the id, the style and the kind come back.
        g.call('wm_pkgcall',bp=onkey,si=ch,ax=27)
        assert g.read(cfg,1) == b'\2' and list(g.read(crow,10)) == ROWS[2], 'Cancel did not restore the row'
        assert g.read(lin('thm_kind'),1) == b'\2' and g.read(bits,1) == b'\0'
        print('PASS chooser: opens through the panel, every row installs, Escape restores',flush=True)

        # The Title bar style group and 95 (76.14.13), ONE resolve per click.
        press_customize()
        ch, xx, yy, onclick, onkey = chooser()
        style(1)                               # Stripes
        assert g.last_count == 1, ('Stripes resolved more than once',g.last_count)
        assert g.read(bits,1) == b'\1' and g.read(lin('thm_strp'),1) == b'\1'
        g.call('wm_paint_all')
        preview = g.frame(out/'color-theme-stripes.ppm')
        assert matches(preview.getpixel((xx+35,yy+3)),15), 'Color Theme stripe ink'
        assert matches(preview.getpixel((xx+35,yy+4)),PRESETS[2][2]), 'Color Theme stripe ground'
        style(2)                               # Windows
        assert g.last_count == 1, ('Windows resolved more than once',g.last_count)
        assert g.read(lin('thm_kind'),1) == b'\3', 'Windows style was not applied'
        assert g.read(bits,1)[0] & 4
        light_title = PRESETS[2][2]
        assert g.read(lin('thm_tbgc'),1) == bytes([light_title])
        box95()
        assert g.read(bits,1)[0] & 6 == 6, '95 was not stored with the Windows style'
        assert g.read(lin('thm_kind'),1) == b'\3', '95 deselected Windows'
        button_face = g.read(lin('thm_bgc'),1)[0]
        g.call('wm_paint_all')
        www = struct.unpack_from('<H', g.read(0x600+ch, eq['WIN_SIZE']), 6)[0]
        classic = g.frame(out/'windows-95-buttons.ppm')
        assert matches(classic.getpixel((xx+www-65,yy+3)),light_title), '95 lost the preset title colour'
        assert matches(classic.getpixel((xx+www-46,yy+4)),button_face), '95 did not restore main button colours'
        box95()
        assert g.read(bits,1)[0] & 2 == 0, '95 did not turn off'
        g.call('wm_paint_all')
        modern = g.frame(out/'windows-preset-buttons.ppm')
        assert matches(modern.getpixel((xx+www-46,yy+4)),light_title), 'preset-coloured buttons did not return'
        box95()
        g.call('wm_pkgcall',bp=onclick,si=ch,cx=xx+25,dx=yy+19+3+3*11+5,count='thm_set')  # Forest
        assert g.last_count == 1, ('a preset under Windows resolved more than once',g.last_count)
        assert g.read(lin('thm_kind'),1) == b'\3', 'a preset choice left Windows'
        assert g.read(bits,1)[0] & 6 == 6, 'a preset choice reset Windows/95'
        assert g.read(lin('thm_tbgc'),1) == bytes([PRESETS[3][2]]), 'Windows did not take the new preset'
        style(0)                               # Solid
        assert g.read(lin('thm_kind'),1) == b'\2', 'Solid did not leave Windows'
        assert g.read(lin('thm_strp'),1) == b'\0' and g.read(bits,1)[0] & 5 == 0
        g.call('wm_pkgcall',bp=onkey,si=ch,ax=27)
        assert g.read(lin('thm_kind'),1) == b'\2' and g.read(bits,1) == b'\0' and g.read(cfg,1) == b'\2', \
            'Cancel did not restore Light, Solid'
        print('PASS title styles: Solid, Stripes, Windows and 95 preview once each; Cancel restores',flush=True)

        # Apply under Windows/95, reopen: the state stands and a no-op Cancel
        # sets nothing; a second Customize press does not re-bank a preview.
        press_customize()
        ch, xx, yy, onclick, onkey = chooser()
        style(2); box95()
        g.call('wm_pkgcall',bp=onclick,si=ch,cx=xx+25,dx=yy+19+3+3*11+5)  # Forest
        g.call('wm_pkgcall',bp=onkey,si=ch,ax=13)  # Return is Apply
        assert g.read(lin('thm_kind'),1) == b'\3' and g.read(cfg,1) == b'\3' and g.read(bits,1)[0] & 6 == 6
        press_customize()
        ch, xx, yy, onclick, onkey = chooser()
        g.call('wm_pkgcall',bp=onkey,si=ch,ax=27,count='thm_set')
        assert g.last_count == 0, ('a no-op Cancel re-set the theme',g.last_count)
        assert g.read(lin('thm_kind'),1) == b'\3' and g.read(cfg,1) == b'\3'
        press_customize()
        ch, xx, yy, onclick, onkey = chooser()
        g.call('wm_pkgcall',bp=onclick,si=ch,cx=xx+25,dx=yy+19+3+6*11+5)  # Midnight
        press_customize()
        g.call('wm_pkgcall',bp=onkey,si=ch,ax=27)
        assert g.read(cfg,1) == b'\3', 'a second Customize press re-banked the preview'
        set_preset(4, 0)
        print('PASS Apply/reopen keeps Windows/95 and the preset; no-op Cancel; no re-bank',flush=True)
        for kind,paper in [(0,15),(1,0)]:
            g.write(lin('cp_dirty'),b'\0\0')
            g.call(api=THM_SET,ax=kind)
            assert g.read(lin('cp_dirty'),2) == b'\1\1', 'mode API did not post repaint/save'
            assert g.read(lin('thm_strp'),1) == b'\1', 'Bright/Dark stripe regression'
            g.ui_pass()
            frame = g.frame(out/f'mode-{kind}.ppm')
            assert matches(frame.getpixel((frame.width-40,3)),paper), ('Bright/Dark menu',kind)
            assert frame.crop(rect).tobytes() == expected, ('Bright/Dark Paint content',kind)
        print('PASS Bright/Dark: existing menu behavior and unchanged Paint content',flush=True)
        # An untouched Customize on Color repaints and owes nothing (76.14.14).
        g.call(api=THM_SET,ax=THM_COLOR)
        g.write(lin('cp_dirty'),b'\0\0')
        press_customize()
        ch, xx, yy, onclick, onkey = chooser()
        g.call('wm_pkgcall',bp=onkey,si=ch,ax=27,count='thm_set')
        assert g.last_count == 0, ('a no-op Cancel re-set the theme',g.last_count)
        assert g.read(lin('cp_dirty'),2) == b'\0\0', 'a no-op Customize owed a repaint'
        print('PASS work counts: row callbacks draw no text; a no-op Customize sets nothing',flush=True)
        g.write(lin('ld_lname'),b'NOTEPAD.O88\0')
        assert g.call('ld_run_name_x',near=True,si=symbols['ld_lname']) & 255 == 0
        _,raw,(x,y,ww,hh) = top_window()
        rect = (x+1,y+19,x+ww-17,y+hh-1)
        g.call('wm_paint_all')
        expected = g.frame(out/'document-original.ppm').crop(rect).tobytes()
        for pid,*_ in PRESETS:
            set_preset(pid)
            g.call('wm_paint_all')
            frame = g.frame(out/f'document-{pid}.ppm')
            assert matches(frame.getpixel((x+ww-25,y+40)),15), (pid,'document paper')
            assert frame.crop(rect).tobytes() == expected, (pid,'document content changed')
        print('PASS all presets: existing Notepad document white and pixel-identical',flush=True)
        # Closing the panel reaps the chooser - with nothing owed to SYSTEM.CFG.
        press_customize()
        assert struct.unpack('<H',g.read(row+2,2))[0]
        count = g.read(lin('wm_zn'),1)[0]
        g.write(lin('cp_wdirty'),b'\0')
        g.call('app_close_win', bx=cp[0])
        assert g.read(row+2,2) == b'\0\0', 'panel close retained THMPAL.DRV'
        assert g.read(lin('wm_zn'),1)[0] < count - 1, 'panel close retained the chooser'
        # A loaded image with a mismatched private ABI is refused and freed.
        g.call('cpf_cp_open')
        _,raw,_ = top_window()
        cp = (top_window()[0],) + struct.unpack_from('<HHHH',raw,2)
        press_customize()
        segment = struct.unpack('<H',g.read(row+2,2))[0]
        g.write(segment*16+10,b'\xff\xff')
        ch, xx, yy, onclick, onkey = chooser()
        g.call('wm_pkgcall',bp=onkey,si=ch,ax=13)
        press_customize()
        assert g.read(row+2,2) == b'\0\0', 'incompatible overlay retained'
        assert g.read(row+14,1) == b'\3', 'incompatible overlay error missing'
        print('PASS overlay: reaped on panel close, private ABI rejected and freed',flush=True)
    finally:
        g.close()
    if args.visual_only:
        return
    # Real save through the existing Control Panel writer, then a fresh boot.
    for pid,*_ in PRESETS:
        shutil.copyfile(source,args.image)
        g = Guest(args)
        try:
            eq = os88sym.equates()
            crow = lin('thm_tab') + THM_COLOR*10
            cfg = lin('drv_cfg') + eq['CFG_THMP']
            g.call('cpf_cp_open')
            # Coexist with upstream's independent version-1 port-pin keys.
            g.write(lin('drv_cfg')+eq['CFG_SPIN'],b'\x98')
            g.write(lin('drv_cfg')+eq['CFG_EPIN'],b'\x50')
            style_bits = [0, 1, 4, 6, 1, 5, 0, 7][pid]   # Solid/Stripes/Windows/95
            g.write(crow, bytes(ROWS[pid]))
            g.write(cfg, bytes([pid]))
            g.write(bits, bytes([style_bits]))
            g.call(api=THM_SET, ax=THM_COLOR)
            g.call('cpf_cp_flush_close')
            assert g.read(lin('cp_wdirty'),1) == b'\0'
            assert g.read(lin('drv_cfg')+eq['CFG_THM'],1) == b'\2', 'Windows style overwrote the saved Color'
        finally:
            g.close()
        g = Guest(args)
        try:
            assert g.read(lin('thm_kind'),1) == (b'\3' if style_bits & 4 else b'\2')
            assert g.read(cfg,1) == bytes([pid])
            assert g.read(bits,1) == bytes([style_bits]), (pid,'style bits')
            assert g.read(crow,10) == bytes(ROWS[pid]), (pid,'boot did not restore the row')
            if not style_bits & 4:
                assert g.read(lin('thm_strp'),1) == bytes([style_bits & 1])
            assert g.read(lin('thm_xorm'),1) == bytes([ROWS[pid][9]])
            assert g.read(lin('drv_cfg')+eq['CFG_SPIN'],1) == b'\x98'
            assert g.read(lin('drv_cfg')+eq['CFG_EPIN'],1) == b'\x50'
            print(f'PASS preset {pid}: SYSTEM.CFG row, style {style_bits} and SP/EP save/reboot',flush=True)
        finally:
            g.close()

if __name__ == '__main__':
    main()
