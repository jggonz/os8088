#!/usr/bin/env python3
"""Pixel checks for Color presets on QEMU's BIOS mode 10h/12h renderer.

Build the image first; --ega requires make VIDEO=ega. This is not an EGA
hardware emulation: QEMU's standard VGA runs the actual EGA BIOS mode 10h.
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
# ID, desktop, active title, inactive title, selection, inactive ink.
PRESETS = [(0,3,1,7,0,0),(1,9,1,8,1,15),(2,7,3,8,3,15),
           (3,10,2,8,2,15),(4,13,5,8,5,15),(5,14,6,8,6,15),
           (6,1,8,7,1,0),(7,6,4,8,4,15)]
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
        self.pidfile = str(Path(args.image).resolve().with_suffix(f'.{qp}.pid'))
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

    def call(self, name=None, api=None, ax=0, bx=None, si=None, bp=None, cx=None, dx=None, near=False, count=None):
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
        for index,value in [(5,bp),(1,cx),(2,dx)]:
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
    g = Guest(args)
    try:
        mode = g.read(0x449, 1)[0]
        if args.mono_smoke:
            assert mode == 6
            g.call('fmf_files_open')
            before = g.frame(out/'before.ppm')
            g.call(api=0x046D,ax=2)
            g.call(api=0x0475,ax=2)
            after = g.frame(out/'after.ppm')
            assert before.tobytes() == after.tobytes()
            assert g.read(os88sym.linear('thm_kind'),1) == b'\0'
            assert set(after.get_flattened_data()) <= {(0,0,0),(255,255,255)}
            print('PASS kern_small CGA mode 06: File Manager boots; Color API no-ops; monochrome pixels unchanged')
            return
        assert mode == (0x10 if args.ega else 0x12), f'BIOS video mode {mode:02x}'
        assert g.read(os88sym.linear('vid_kind'),1)[0] == (3 if args.ega else 0)
        g.call(api=0x046D, ax=2)
        g.call(api=0x047D, ax=0)  # isolate solid-preset checks from saved preferences
        g.call('fmf_files_open')
        g.call('cpf_cp_open')
        # Inspect the real window table, not theme table values alone.
        eq = os88sym.equates()
        windows = []
        base = os88sym.linear('wm_wins')
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
        for pid, desk, title, inactive, selection, iink in PRESETS:
            g.call(api=0x0475, ax=pid)
            g.call(api=0x046D, ax=2)
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
            assert matches(image.getpixel((630,240)),15), (pid,'Control Panel paper')
            # Render the real menu's enabled first cell, then restore it.
            if pid:
                for label,value in [('menu_x1',450),('menu_y1',30),
                        ('menu_x2',620),('menu_y2',48),
                        ('menu_iptr',symbols['menu_items_logo']),('menu_dseg',0)]:
                    g.write(os88sym.linear(label),struct.pack('<H',value))
                g.call('menu_item_xor',ax=0)
                selected = g.frame(out/f'selection-{pid}.ppm')
                assert matches(selected.getpixel((615,32)),selection), (pid,'selection paper')
                pixels = list(selected.crop((458,35,600,43)).get_flattened_data())
                assert any(matches(p,15) for p in pixels), (pid,'selection ink')
                g.call('menu_item_unselect',ax=0)
                restored = g.frame(out/f'restored-{pid}.ppm')
                assert matches(restored.getpixel((615,32)),7), (pid,'restored menu paper')
            assert g.read(os88sym.linear('thm_preset'),1)[0] == pid
            image.save(out/f'preset-{pid}.png')
            print(f'PASS mode {mode:02x}, preset {pid}: desktop, titles, white File Manager/Control Panel, menus/selection',flush=True)
        # Windows changes title/button drawing, not the selected preset's
        # semantic color roles. Check the actual title and minimize-button face.
        fx,fy,fw,fh = fm[1:]
        for pid, desk, title, *_ in PRESETS:
            g.call(api=0x0475,ax=pid)
            g.call(api=0x046D,ax=3)
            assert g.read(os88sym.linear('thm_preset'),1) == bytes([pid])
            assert g.read(os88sym.linear('thm_tbgc'),1) == bytes([title]), ('Windows active title role',pid)
            g.call('wm_paint_all')
            image = g.frame(out/f'windows-preset-{pid}.ppm')
            assert matches(image.getpixel((image.width-10,image.height-45)),desk), ('Windows desktop color',pid)
            assert matches(image.getpixel((fx+100,fy+5)),title), ('Windows title bar color',pid)
            assert matches(image.getpixel((fx+fw-46,fy+4)),title), ('Windows minimize button color',pid)
        g.call(api=0x046D,ax=2)
        print('PASS Windows title style: selected preset colors reach active bars and button faces',flush=True)
        # Open the actual APPS folder using its FAT12 directory cluster.
        disk = source.read_bytes()
        root = (struct.unpack_from('<H',disk,14)[0] + disk[16]*struct.unpack_from('<H',disk,22)[0])*512
        entries = [disk[i:i+32] for i in range(root,root+struct.unpack_from('<H',disk,17)[0]*32,32)]
        apps = next(e for e in entries if e[:11] == b'APPS       ')
        g.write(os88sym.linear('dsk_cwd'),apps[26:28])
        g.call('fmf_files_open')
        for pid,*_ in PRESETS:
            g.call(api=0x0475,ax=pid)
            g.call('wm_paint_all')
            raw = g.read(0x600+fm[0],eq['WIN_SIZE'])
            x,y,ww,hh = struct.unpack_from('<HHHH',raw,2)
            frame = g.frame(out/f'folder-{pid}.ppm')
            assert matches(frame.getpixel((x+ww-25,y+hh-45)),15), (pid,'folder paper')
        # Launch existing Paint, then compare every pixel of its content.
        g.write(os88sym.linear('ld_lname'),b'PAINT.O88\0')
        result = g.call('ld_run_name_x',near=True,si=symbols['ld_lname'])
        assert result & 255 == 0, ('Paint launch',result)
        slot = g.read(os88sym.linear('wm_zord')+g.read(os88sym.linear('wm_zn'),1)[0]-1,1)[0]
        raw = g.read(base+slot*eq['WIN_SIZE'],eq['WIN_SIZE'])
        x,y,ww,hh = struct.unpack_from('<HHHH',raw,2)
        # Exclude the WM-owned resize grip at bottom right, which is chrome.
        rect = (x+1,y+19,x+ww-17,y+hh-1)
        g.call('wm_paint_all')
        expected = g.frame(out/'paint-original.ppm').crop(rect).tobytes()
        for pid,*_ in PRESETS:
            g.call(api=0x0475,ax=pid)
            g.call('wm_paint_all')
            frame = g.frame(out/f'paint-{pid}.ppm')
            assert frame.crop(rect).tobytes() == expected, (pid,'Paint content/palette changed')
        print('PASS all presets: APPS folder white; existing Paint content/palette pixel-identical',flush=True)
        g.call(api=0x0475,ax=2)
        g.call(api=symbols['cw_thmv_open'])
        slot = g.read(os88sym.linear('wm_zord')+g.read(os88sym.linear('wm_zn'),1)[0]-1,1)[0]
        chooser = symbols['wm_wins']+slot*eq['WIN_SIZE']
        raw = g.read(0x600+chooser,eq['WIN_SIZE'])
        xx,yy,www,hhh = struct.unpack_from('<HHHH',raw,2)
        frame = g.frame(out/'customize.ppm')
        assert matches(frame.getpixel((xx+www-12,yy+25)),15), 'Customize white paper'
        overlay = Path(os88build.at('build/thmpal.bin')).read_bytes()
        assert b'Light\0' in overlay and b'Classic\0' in overlay and b'95\0' in overlay
        assert b'Color Theme\0' in overlay and b'Customize Color Theme\0' not in overlay
        assert b'Amber\0' in overlay and b'Amber Terminal\0' not in overlay
        assert b'Windows\0' in overlay and b'Ocean\0' not in overlay
        onclick = struct.unpack_from('<H',raw,eq['W_ONCLICK'])[0]
        onkey = struct.unpack_from('<H',raw,eq['W_ONKEY'])[0]
        # Click the reordered visible list; IDs now match the row order.
        for row,(pid,*_) in enumerate(PRESETS):
            g.call('wm_pkgcall',bp=onclick,si=chooser,cx=xx+25,dx=yy+19+3+row*11+5,count='font_run_x')
            assert g.last_count == 0, ('row callback duplicated text drawing',row,g.last_count)
            assert g.read(os88sym.linear('thm_preset'),1) == bytes([pid]), ('list order',row,pid)
            g.ui_pass()
            preview = g.frame(out/f'list-preview-{pid}.ppm')
            assert matches(preview.getpixel((preview.width-10,preview.height-45)),PRESETS[pid][1]), ('posted preview',pid)
        g.call(api=0x0475,ax=2)
        g.call(api=symbols['cw_thmv_open'])  # reseed the selected row and Cancel state
        # Actual Stripes radio callback, then Cancel/Escape restores the opening state.
        g.call('wm_pkgcall',bp=onclick,si=chooser,cx=xx+15,dx=yy+19+STYLE_Y+STYLE_PITCH+8)
        assert g.read(os88sym.linear('thm_cstrp'),1) == b'\1'
        assert g.read(os88sym.linear('thm_strp'),1) == b'\1', 'Stripes did not reach live title role'
        g.call('wm_paint_all')
        preview = g.frame(out/'color-theme-stripes.ppm')
        assert matches(preview.getpixel((xx+35,yy+3)),15), 'Color Theme stripe ink'
        assert matches(preview.getpixel((xx+35,yy+4)),3), 'Color Theme stripe ground'
        # All three style choices must drive the live appearance, then return
        # to the opening state when Cancel is pressed.
        g.call('wm_pkgcall',bp=onclick,si=chooser,cx=xx+15,dx=yy+19+STYLE_Y+8)
        assert g.read(os88sym.linear('thm_cstrp'),1) == b'\0', 'Solid did not clear stripes'
        assert g.read(os88sym.linear('thm_strp'),1) == b'\0', 'Solid did not reach live title role'
        g.call('wm_pkgcall',bp=onclick,si=chooser,cx=xx+15,dx=yy+19+STYLE_Y+2*STYLE_PITCH+8)
        assert g.read(os88sym.linear('thm_kind'),1) == b'\3', 'Windows style was not applied'
        g.call('wm_pkgcall',bp=onclick,si=chooser,cx=xx+15,dx=yy+19+STYLE_Y+8)
        assert g.read(os88sym.linear('thm_kind'),1) == b'\2', 'Solid did not leave Windows style'
        assert g.read(os88sym.linear('thm_strp'),1) == b'\0', 'Solid after Windows retained stripes'
        g.call('wm_pkgcall',bp=onclick,si=chooser,cx=xx+15,dx=yy+19+STYLE_Y+STYLE_PITCH+8)
        assert g.read(os88sym.linear('thm_cstrp'),1) == b'\1'
        g.call('wm_pkgcall',bp=onkey,si=chooser,ax=27)
        assert g.read(os88sym.linear('thm_cstrp'),1) == b'\0'
        g.call(api=symbols['cw_thmv_open'])
        slot = g.read(os88sym.linear('wm_zord')+g.read(os88sym.linear('wm_zn'),1)[0]-1,1)[0]
        chooser = symbols['wm_wins']+slot*eq['WIN_SIZE']
        raw = g.read(0x600+chooser,eq['WIN_SIZE'])
        xx,yy,www,hhh = struct.unpack_from('<HHHH',raw,2)
        onclick = struct.unpack_from('<H',raw,eq['W_ONCLICK'])[0]
        onkey = struct.unpack_from('<H',raw,eq['W_ONKEY'])[0]
        g.call('wm_pkgcall',bp=onclick,si=chooser,cx=xx+15,dx=yy+19+STYLE_Y+STYLE_PITCH+8)
        g.call('wm_pkgcall',bp=onkey,si=chooser,ax=13)  # Apply/Return keeps it
        assert g.read(os88sym.linear('thm_cstrp'),1) == b'\1'
        # Reopening must seed the Stripes radio from the saved preference too.
        g.call(api=symbols['cw_thmv_open'])
        slot = g.read(os88sym.linear('wm_zord')+g.read(os88sym.linear('wm_zn'),1)[0]-1,1)[0]
        chooser = symbols['wm_wins']+slot*eq['WIN_SIZE']
        raw = g.read(0x600+chooser,eq['WIN_SIZE'])
        xx,yy,www,hhh = struct.unpack_from('<HHHH',raw,2)
        onclick = struct.unpack_from('<H',raw,eq['W_ONCLICK'])[0]
        onkey = struct.unpack_from('<H',raw,eq['W_ONKEY'])[0]
        g.call('wm_pkgcall',bp=onclick,si=chooser,cx=xx+15,dx=yy+19+STYLE_Y+8)
        assert g.read(os88sym.linear('thm_cstrp'),1) == b'\0', 'Solid did not switch off the seeded Stripes style'
        g.call('wm_pkgcall',bp=onkey,si=chooser,ax=27)
        assert g.read(os88sym.linear('thm_cstrp'),1) == b'\1', 'Cancel did not restore stripes'
        g.call('wm_paint_all')
        stripes = g.frame(out/'stripes-on.ppm')
        assert matches(stripes.getpixel((x+40,y+3)),15), 'stripe ink'
        assert matches(stripes.getpixel((x+40,y+4)),3), 'stripe ground'
        g.call(api=0x0475,ax=99)  # unknown ID falls back without losing stripes
        assert g.read(os88sym.linear('thm_preset'),1) == b'\0'
        assert g.read(os88sym.linear('thm_strp'),1) == b'\1', 'preset lost stripes'
        g.call(api=0x047D,ax=0)
        g.call('wm_paint_all')
        solid = g.frame(out/'stripes-off.ppm')
        assert matches(solid.getpixel((x+40,y+3)),1), 'solid title'
        assert matches(solid.getpixel((x+40,y+4)),1), 'solid title'
        g.call(api=symbols['cw_thmv_reap'])
        # Windows can use either the new module buttons or the kernel's
        # original controls. The checkbox is only active on the Windows row;
        # both layouts keep the selected preset's title colors.
        g.call(api=0x0475,ax=2)
        g.call(api=0x046D,ax=2)
        g.call(api=symbols['cw_thmv_open'])
        slot = g.read(os88sym.linear('wm_zord')+g.read(os88sym.linear('wm_zn'),1)[0]-1,1)[0]
        chooser = symbols['wm_wins']+slot*eq['WIN_SIZE']
        raw = g.read(0x600+chooser,eq['WIN_SIZE'])
        xx,yy,www,hhh = struct.unpack_from('<HHHH',raw,2)
        onclick = struct.unpack_from('<H',raw,eq['W_ONCLICK'])[0]
        onkey = struct.unpack_from('<H',raw,eq['W_ONKEY'])[0]
        g.call('wm_pkgcall',bp=onclick,si=chooser,cx=xx+15,
               dx=yy+19+STYLE_Y+2*STYLE_PITCH+8,count='thm_set')  # Windows
        assert g.last_count == 1, ('Windows selection resolved the theme more than once',g.last_count)
        assert g.read(os88sym.linear('thm_kind'),1) == b'\3'
        light_title = PRESETS[2][2]
        assert g.read(os88sym.linear('thm_tbgc'),1) == bytes([light_title])
        g.call('wm_pkgcall',bp=onclick,si=chooser,cx=xx+1+CLASSIC_X+5,
               dx=yy+19+STYLE_Y+2*STYLE_PITCH+8)
        assert g.read(os88sym.linear('thm_cstrp'),1) == b'\6', '95 buttons bit was not stored with Windows style'
        assert g.read(os88sym.linear('thm_kind'),1) == b'\3', 'Classic checkbox deselected Windows'
        assert g.read(os88sym.linear('thm_tbgc'),1) == bytes([light_title]), 'Classic changed the preset title color'
        button_face = g.read(os88sym.linear('thm_bgc'),1)[0]
        g.call('wm_paint_all')
        classic = g.frame(out/'windows-classic-buttons.png')
        assert matches(classic.getpixel((xx+www-65,yy+3)),light_title), 'Classic title bar lost its theme color'
        assert matches(classic.getpixel((xx+www-46,yy+4)),button_face), 'Classic did not restore main Windows button colors'
        g.call('wm_pkgcall',bp=onclick,si=chooser,cx=xx+1+CLASSIC_X+5,
               dx=yy+19+STYLE_Y+2*STYLE_PITCH+8)
        assert g.read(os88sym.linear('thm_cstrp'),1) == b'\4', '95 checkbox did not turn off'
        g.call('wm_paint_all')
        modern = g.frame(out/'windows-theme-buttons.png')
        assert matches(modern.getpixel((xx+www-46,yy+4)),light_title), 'Theme-colored Windows button style did not return'
        # Choosing another preset while Windows is active must update the
        # palette without resetting the Windows radio or the 95 button mode.
        g.call('wm_pkgcall',bp=onclick,si=chooser,cx=xx+1+CLASSIC_X+5,
               dx=yy+19+STYLE_Y+2*STYLE_PITCH+8)
        g.call('wm_pkgcall',bp=onclick,si=chooser,cx=xx+15,
               dx=yy+19+3+3*11+5,count='thm_set')  # Forest
        assert g.last_count == 1, ('Windows preset selection resolved the theme more than once',g.last_count)
        assert g.read(os88sym.linear('thm_kind'),1) == b'\3', 'preset selection left Windows mode'
        assert g.read(os88sym.linear('thm_cstrp'),1) == b'\6', 'preset selection reset the Windows/95 style'
        forest_title = PRESETS[3][2]
        assert g.read(os88sym.linear('thm_tbgc'),1) == bytes([forest_title]), 'Windows did not adopt the new preset colors'
        g.call('wm_paint_all')
        forest = g.frame(out/'windows-forest-buttons.png')
        assert matches(forest.getpixel((xx+www-65,yy+3)),forest_title), 'Windows title bar did not follow the selected preset'
        assert matches(forest.getpixel((xx+www-46,yy+4)),g.read(os88sym.linear('thm_bgc'),1)[0]), 'Windows button palette did not follow the selected preset'
        g.call('wm_pkgcall',bp=onclick,si=chooser,cx=xx+1+CLASSIC_X+5,
               dx=yy+19+STYLE_Y+2*STYLE_PITCH+8)
        assert g.read(os88sym.linear('thm_cstrp'),1) == b'\4'
        g.call('wm_pkgcall',bp=onkey,si=chooser,ax=27)
        assert g.read(os88sym.linear('thm_kind'),1) == b'\2', 'Cancel did not restore Color mode'
        assert g.read(os88sym.linear('thm_cstrp'),1) == b'\0', 'Cancel did not restore style bits'
        g.call(api=symbols['cw_thmv_reap'])
        # Apply a different preset with Windows/95 selected, then reopen:
        # both independent choices must remain live and seed the controls.
        g.call(api=symbols['cw_thmv_open'])
        slot = g.read(os88sym.linear('wm_zord')+g.read(os88sym.linear('wm_zn'),1)[0]-1,1)[0]
        chooser = symbols['wm_wins']+slot*eq['WIN_SIZE']
        raw = g.read(0x600+chooser,eq['WIN_SIZE'])
        xx,yy = struct.unpack_from('<HH',raw,2)
        onclick = struct.unpack_from('<H',raw,eq['W_ONCLICK'])[0]
        onkey = struct.unpack_from('<H',raw,eq['W_ONKEY'])[0]
        g.call('wm_pkgcall',bp=onclick,si=chooser,cx=xx+15,
               dx=yy+19+STYLE_Y+2*STYLE_PITCH+8)
        g.call('wm_pkgcall',bp=onclick,si=chooser,cx=xx+1+CLASSIC_X+5,
               dx=yy+19+STYLE_Y+2*STYLE_PITCH+8)
        g.call('wm_pkgcall',bp=onclick,si=chooser,cx=xx+15,
               dx=yy+19+3+3*11+5)  # Forest
        g.call('wm_pkgcall',bp=onkey,si=chooser,ax=13)
        assert g.read(os88sym.linear('thm_kind'),1) == b'\3'
        assert g.read(os88sym.linear('thm_preset'),1) == b'\3'
        assert g.read(os88sym.linear('thm_cstrp'),1) == b'\6'
        g.call(api=symbols['cw_thmv_open'])
        assert g.read(os88sym.linear('thm_kind'),1) == b'\3'
        assert g.read(os88sym.linear('thm_preset'),1) == b'\3'
        assert g.read(os88sym.linear('thm_cstrp'),1) == b'\6'
        g.call(api=symbols['cw_thmv_reap'])
        print('PASS Apply/reopen: selected preset and Windows/95 style remain selected',flush=True)
        print('PASS Windows 95 option: main button colors and theme-colored buttons toggle; bar color and Cancel persist',flush=True)
        print('PASS title styles: Solid, Stripes and Windows preview; Cancel, Apply and preset changes',flush=True)
        print('PASS Customize opens with Windows style available',flush=True)
        for kind,paper in [(0,15),(1,0)]:
            g.write(os88sym.linear('cp_dirty'),b'\0\0')
            g.call(api=0x046D,ax=kind)
            assert g.read(os88sym.linear('cp_dirty'),2) == b'\1\1', 'mode API did not post repaint/save'
            assert g.read(os88sym.linear('thm_strp'),1) == b'\1', 'Bright/Dark stripe regression'
            g.ui_pass()
            frame = g.frame(out/f'mode-{kind}.ppm')
            assert matches(frame.getpixel((frame.width-40,3)),paper), ('Bright/Dark menu',kind)
            assert frame.crop(rect).tobytes() == expected, ('Bright/Dark Paint content',kind)
        print('PASS Bright/Dark: existing menu behavior and unchanged Paint content',flush=True)
        for kind in (0,1):
            g.call(api=0x046D,ax=kind)
            g.call(api=symbols['cw_thmv_open'])
            assert g.read(os88sym.linear('thm_kind'),1) == bytes([kind]), ('opening Customize changed the active theme',kind)
            slot = g.read(os88sym.linear('wm_zord')+g.read(os88sym.linear('wm_zn'),1)[0]-1,1)[0]
            chooser = symbols['wm_wins']+slot*eq['WIN_SIZE']
            raw = g.read(0x600+chooser,eq['WIN_SIZE'])
            onkey = struct.unpack_from('<H',raw,eq['W_ONKEY'])[0]
            g.call('wm_pkgcall',bp=onkey,si=chooser,ax=27,count='thm_set')
            assert g.last_count == 1, ('Cancel resolved hidden Color rows',kind,g.last_count)
            assert g.read(os88sym.linear('thm_kind'),1) == bytes([kind])
        print('PASS work counts: row callbacks draw no duplicate text; Bright/Dark Cancel resolves once',flush=True)
        g.call(api=0x046D,ax=2)
        g.write(os88sym.linear('ld_lname'),b'NOTEPAD.O88\0')
        assert g.call('ld_run_name_x',near=True,si=symbols['ld_lname']) & 255 == 0
        slot = g.read(os88sym.linear('wm_zord')+g.read(os88sym.linear('wm_zn'),1)[0]-1,1)[0]
        raw = g.read(base+slot*eq['WIN_SIZE'],eq['WIN_SIZE'])
        x,y,ww,hh = struct.unpack_from('<HHHH',raw,2)
        rect = (x+1,y+19,x+ww-17,y+hh-1)
        g.call('wm_paint_all')
        expected = g.frame(out/'document-original.ppm').crop(rect).tobytes()
        for pid,*_ in PRESETS:
            g.call(api=0x0475,ax=pid)
            g.call('wm_paint_all')
            frame = g.frame(out/f'document-{pid}.ppm')
            assert matches(frame.getpixel((x+ww-25,y+40)),15), (pid,'document paper')
            assert frame.crop(rect).tobytes() == expected, (pid,'document content changed')
        print('PASS all presets: existing Notepad document white and pixel-identical',flush=True)
        g.call('cpf_cp_open')
        g.call(api=symbols['cw_thmv_open'])
        row = os88sym.linear('thmv_row')
        segment = struct.unpack('<H',g.read(row+2,2))[0]
        assert segment
        count = g.read(os88sym.linear('wm_zn'),1)[0]
        g.call('cpf_cp_flush_close')
        assert g.read(row+2,2) == b'\0\0', 'parent close retained overlay'
        assert g.read(os88sym.linear('wm_zn'),1)[0] < count, 'parent close retained child'
        # A resident, valid module with a mismatched private ABI must be
        # refused and freed before its callback is executed.
        g.call('cpf_cp_open')
        g.call(api=symbols['cw_thmv_open'])
        segment = struct.unpack('<H',g.read(row+2,2))[0]
        g.write(segment*16+10,b'\xff\xff')
        # Destroy the child first, avoiding stale callbacks in this deliberate
        # test corruption; the next open exercises the compatibility gate.
        slot = g.read(os88sym.linear('wm_zord')+g.read(os88sym.linear('wm_zn'),1)[0]-1,1)[0]
        chooser = symbols['wm_wins']+slot*eq['WIN_SIZE']
        raw = g.read(0x600+chooser,eq['WIN_SIZE'])
        onkey = struct.unpack_from('<H',raw,eq['W_ONKEY'])[0]
        g.call('wm_pkgcall',bp=onkey,si=chooser,ax=13)
        g.call(api=symbols['cw_thmv_open'])
        assert g.read(row+2,2) == b'\0\0', 'incompatible overlay retained'
        assert g.read(row+14,1) == b'\3', 'incompatible overlay error missing'
        g.call(api=0x046D,ax=0)
        flags = [(base+slot*eq['WIN_SIZE'],g.read(base+slot*eq['WIN_SIZE'],2))
                 for slot in range(eq['MAX_WIN'])]
        # Fault injection: temporarily exhaust the fixed window table.
        # No normal UI pass runs while these synthetic used bits are set.
        try:
            for address,old in flags:
                g.write(address,struct.pack('<H',int.from_bytes(old,'little') | 1))
            g.call(api=symbols['cw_thmv_open'])
            assert g.read(row+2,2) == b'\0\0', 'failed creation retained overlay'
            assert g.read(os88sym.linear('thm_kind'),1) == b'\0', 'failed creation changed appearance'
        finally:
            for address,old in flags:
                g.write(address,old)
        print('PASS overlay: parent teardown, private ABI rejection and failed-create rollback',flush=True)
    finally:
        g.close()
    if args.visual_only:
        return
    # Real save through the existing Control Panel writer, then a fresh boot.
    for pid,*_ in PRESETS:
        shutil.copyfile(source,args.image)
        g = Guest(args)
        try:
            g.call('cpf_cp_open')
            g.call(api=0x0475,ax=pid)
            g.call(api=0x046D,ax=2)
            g.call(api=0x047D,ax=pid & 1)
            g.call('cpf_cp_flush_close')
            assert g.read(os88sym.linear('cp_wdirty'),1) == b'\0'
        finally:
            g.close()
        g = Guest(args)
        try:
            assert g.read(os88sym.linear('thm_kind'),1) == b'\2'
            assert g.read(os88sym.linear('thm_preset'),1) == bytes([pid])
            assert g.read(os88sym.linear('thm_cstrp'),1) == bytes([pid & 1])
            assert g.read(os88sym.linear('thm_strp'),1) == bytes([pid & 1])
            print(f'PASS preset {pid}: SYSTEM.CFG mode/preset/stripes save and reboot',flush=True)
        finally:
            g.close()
    # Windows is a title style of the selected Color preset. Persist the theme
    # kind as Color and the Windows style in the independent style byte.
    shutil.copyfile(source,args.image)
    g = Guest(args)
    try:
        g.call('cpf_cp_open')
        g.call(api=0x046D,ax=2)
        g.call(api=0x047D,ax=4)
        assert g.read(os88sym.linear('thm_kind'),1) == b'\3'
        g.call('cpf_cp_flush_close')
        assert g.read(os88sym.linear('drv_cfg')+9,1) == b'\2', 'Windows style overwrote the selected Color theme'
    finally:
        g.close()
    g = Guest(args)
    try:
        assert g.read(os88sym.linear('thm_kind'),1) == b'\3', 'Windows title style was not restored'
        assert g.read(os88sym.linear('thm_cstrp'),1) == b'\4', 'Windows title style preference was not saved'
        print('PASS Windows style: Color theme ID and independent style save and reboot',flush=True)
    finally:
        g.close()
    # The Windows and 95 bits share the pre-existing style byte and survive the same
    # configuration path without a new record or allocation.
    shutil.copyfile(source,args.image)
    g = Guest(args)
    try:
        g.call('cpf_cp_open')
        g.call(api=0x046D,ax=2)
        g.call(api=0x047D,ax=6)
        g.call('cpf_cp_flush_close')
        assert g.read(os88sym.linear('drv_cfg')+9,1) == b'\2', '95 style overwrote the selected Color theme'
    finally:
        g.close()
    g = Guest(args)
    try:
        assert g.read(os88sym.linear('thm_kind'),1) == b'\3'
        assert g.read(os88sym.linear('thm_cstrp'),1) == b'\6', 'Windows/95 style preferences were not saved'
        print('PASS Windows 95 option: existing SYSTEM.CFG style byte save and reboot',flush=True)
    finally:
        g.close()

if __name__ == '__main__':
    main()
