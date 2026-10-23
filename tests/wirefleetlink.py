#!/usr/bin/env python3
"""Two shipped WireFleet instances, UART bytes only, with faults and recovery.

Uses WireBall's existing owned-QEMU/socket bridge (docs/TESTING.md item 3).
The independent host parser uses binascii's CRC, never the game's decoder.
Registered as wirefleetlink in soak; make wirefleetdisk before running it.
"""
import binascii
from pathlib import Path
import struct
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools'), str(ROOT / 'tests')]
import wireball as W
import os88ui
import os88qemu as Q
import dispapps
import dispcp
import os88geom as G


def encode(body):
    body = bytearray(body)
    body[62:] = struct.pack('<H', binascii.crc_hqx(body[:62], 0xffff))
    out = bytearray(b'\x7e')
    for byte in body:
        out.extend((0x7d, byte ^ 0x20) if byte in (0x7d,0x7e) else (byte,))
    out.append(0x7e)
    return bytes(out)


def frames(wire):
    for raw in bytes(wire).split(b'\x7e'):
        out, escaped = bytearray(), False
        for byte in raw:
            if escaped:
                out.append(byte ^ 0x20)
                escaped = False
            elif byte == 0x7d:
                escaped = True
            else:
                out.append(byte)
        if not escaped and len(out) == 64 and out[:3] == b'WF\1' and binascii.crc_hqx(out[:62],0xffff) == int.from_bytes(out[62:],'little'):
            yield bytes(out)


class Mouse(W.Mouse):
    def where(self):
        x,y = struct.unpack('<HH', self.m.read(W.S('mouse_x'),4))
        return x,y,self.m.read(W.S('mouse_btn'),1)[0]

    def _edge(self, down):
        self.run('down' if down else 'up')

    def to(self, x, y, l=None, **kw):
        if l is not None and bool(self.where()[2]&1) != l:
            self._edge(l)
        super().to(x,y)

    def click(self,x,y,settle=None):
        super().click(x,y)

    def dblclick(self,x,y,settle=None):
        super().dblclick(x,y)


class UI(os88ui.UI):
    def _wait(self,cond,what,guest,snapshot=None):
        assert Q.acted(self.m,cond,secs=guest,what=what), (what, snapshot() if snapshot else '')

    def settle(self, **kw):
        return W.settle(self.m)

    def scroll_to(self,entry,limit=10,win=None):
        # os88ui's algorithm, using QEMU's guest tick clock for each key.
        def position():
            base=self._fsblk(win) if win is not None else None
            if base is None:
                base=(G.KERNEL_SEG<<4)+self._word('fm_vp')
            return self.scroll(win),int.from_bytes(self.m.read(base+G.FS_SEL,2),'little')
        def step(key):
            before=position()
            self.m.key(key)
            return Q.acted(self.m,lambda:position()!=before,secs=3,what='directory scroll')
        for _ in range(200):
            if self.scroll(win)==0 or not step('ArrowUp'):
                break
        for _ in range(200):
            if self.scroll(win)>=entry or not step('ArrowDown'):
                break
        self.settle()
        row=entry-self.scroll(win)
        assert row>=0
        return row


class Machine(W.Machine):
    def __init__(self,directory,index,ps2=False):
        super().__init__(directory,index,app='wirefleet',filename='WIREFLET.O88',
                         disk=W.artifact('wireflet.img'),prefix='wf_',ps2=ps2)
        self.mo = Mouse(self)
        self.ui = UI(self,mouse=self.mo,sym=W.S,verbose=False)

    def v(self,name,n=1):
        return self.value('wf_'+name,n)

    def data(self,name,n):
        return self.readseg(self.seg,self.sy['wf_'+name],n)

    def wait_for(self,cond,what,secs=10):
        assert Q.acted(self,cond,secs=secs,what=what), (what,self.state())

    def state(self):
        return {name:self.v(name,n) for name,n in
                [('linkopen',1),('connected',1),('side',1),('ready',1),('peerready',1),
                 ('turn',2),('peerturn',2),('pending',1),('cachevalid',1),('hash',2),
                 ('badframes',2),('chatpending',1),('chatcount',2),('conflict',1),('finished',1),
                 ('linkmode',1),('cands',1),('curport',1),('heard',1)]}

    def key(self,key):
        super().key('Return' if key=='Enter' else key)
        Q.pace(self,.14)
        W.settle(self)
        live=dispapps.pkg_seg(self,0)
        if live:
            self.seg=live[1]

    def connect(self):
        self.ui.menu_pick('Link','Connect COM'+str(self.linkport+1))
        self.wait_for(lambda:self.v('linkopen')==1,'UART lease')

    def fire(self,cell):
        self.put([('target',cell,1)])
        self.key('ret')

    def save(self):
        self.key('s')
        self.wait_for(lambda:int.from_bytes(self.read(W.S('fdlg_win'),2),'little') not in (0,65535),'Save dialog')
        self.key('ret')
        self.wait_for(lambda:int.from_bytes(self.read(W.S('fdlg_win'),2),'little')==0,'Save complete')
        assert self.v('message',2)==self.sy['wf_savedmessage'], self.state()
        record = self.data('savebuf',512)
        assert record[:8]==b'WFLT\1\0\0\2'
        assert binascii.crc_hqx(record[:510],0xffff)==int.from_bytes(record[510:],'little')
        return record

    def reopen_load(self,record):
        self.ui.close(self.ui.window('WireFleet'))
        assert self.read(W.S('ser_row'),2)==b'\xff\xff','forced lifetime retained a row'
        self.ui.open('WIREFLET.O88')
        W.settle(self)
        self.seg=dispapps.pkg_seg(self,0)[1]
        self.key('o')
        self.wait_for(lambda:int.from_bytes(self.read(W.S('fdlg_win'),2),'little') not in (0,65535),'Load dialog')
        self.ui.chooser_select('FLEET.WFL')
        self.key('ret')
        self.wait_for(lambda:int.from_bytes(self.read(W.S('fdlg_win'),2),'little')==0,'Load complete')
        assert self.v('message',2)==self.sy['wf_loadedmessage'],self.state()
        assert self.data('identity',12)==record[16:28]
        assert self.data('board',300)==record[54:354]
        assert self.v('turn',2)==int.from_bytes(record[8:10],'little')
        assert self.v('pending')==record[28] and not self.v('connected')


class DropResult:
    """Frame-aware fault fixture; forwards complete UART frames unchanged."""
    def __init__(self,cable):
        self.cable=cable
        self.buffers=[bytearray(),bytearray()]
        self.dropped=False

    def __call__(self,direction,data):
        out=bytearray()
        buf=self.buffers[direction]
        for byte in data:
            if byte==0x7e:
                if buf:
                    wire=b'\x7e'+bytes(buf)+b'\x7e'
                    packet=list(frames(wire))
                    if packet and packet[0][3]==3 and not self.dropped:
                        self.dropped=True
                        self.cable.enabled=False
                    elif self.cable.enabled:
                        out.extend(wire)
                    buf.clear()
            else:
                buf.append(byte)
        return bytes(out)


def free_ports():
    """SPEC.md 114.9: no serial mouse, so COM1 and COM2 are both free. Each
    PC must move its lease between them and stop on the cable's port."""
    with tempfile.TemporaryDirectory(prefix='wf-free-') as td:
        machines=[]
        cable=None
        try:
            machines=[Machine(Path(td),0,ps2=True),Machine(Path(td),1,ps2=True)]
            cable=W.Cable(machines)
            for m in machines:
                assert m.read(W.S('mou_port'),1)==b'\4','PS/2 pointer did not win'
                m.wait_for(lambda m=m:m.v('connected')==1,'automatic link over two free ports',secs=40)
            for m in machines:
                assert m.v('curport')==m.linkport+1 and m.v('cands')==m.linkport+1,m.state()
                assert m.v('linkmode')==1
            print('wirefleetlink: two free ports, each PC found the cable port by itself')
        finally:
            if cable:
                cable.close()
            for m in machines:
                m.cleanup()


def main():
    free_ports()
    with tempfile.TemporaryDirectory(prefix='wf-link-') as td:
        directory=Path(td)
        machines=[]
        cable=None
        try:
            machines=[Machine(directory,0),Machine(directory,1)]
            a,b=machines
            cable=W.Cable(machines)
            # SPEC.md 114.9: no menu use. Each PC is already looking.
            for m in machines:
                m.key('a')
                assert sorted(m.data('board',100)).count(0)==83
            assert a.data('board',100)!=b.data('board',100),'fresh PCs inherited an identical auto fleet'
            for m in machines:
                m.wait_for(lambda m=m:m.v('connected')==1,'automatic addressed handshake',secs=20)
                assert m.v('curport')==m.linkport+1 and m.v('linkmode')==1,m.state()
            # New fleet after linking used to strand the other PC for good:
            # its identity changes, and before any shot the peer rebinds.
            old=a.data('identity',4)
            a.key('n')
            assert a.data('identity',4)!=old
            for m in machines:
                m.wait_for(lambda m=m:m.v('connected')==1,'rebind at turn 0 after New fleet',secs=20)
            assert b.data('peer',4)==a.data('identity',4)
            a.key('a')
            for m in machines:
                m.key('y')
            for m in machines:
                m.wait_for(lambda m=m:m.v('peerready')==1,'both ready')
            host,guest=sorted(machines,key=lambda m:m.v('side'))
            assert host.v('side')==0 and guest.v('side')==1
            assert host.data('match',4)==guest.data('match',4)==host.data('identity',4)
            # A wrong-turn key cannot send a command or change either board.
            guest.fire(0)
            assert guest.v('turn',2)==0 and guest.v('pending')==0
            # Miss first, while deliberately losing the defender's result.
            water=guest.data('board',100).index(0)
            drop=DropResult(cable)
            cable.filter=drop
            host.fire(water)
            guest.wait_for(lambda:drop.dropped and guest.v('turn',2)==1,'defender commits before lost result')
            assert host.v('turn',2)==0 and host.v('pending')==1
            before=[m.data('board',300) for m in machines]
            for m in machines:
                m.wait_for(lambda m=m:m.v('connected')==0,'silent cable pause',secs=7)
            assert [m.data('board',300) for m in machines]==before
            saved=[m.save() for m in machines]
            for m,record in zip(machines,saved):
                m.reopen_load(record)
                m.connect()
            cable.filter=None
            cable.enabled=True
            for m in machines:
                m.wait_for(lambda m=m:m.v('connected')==1 and m.v('turn',2)==1,'saved pending shot resumes',secs=15)
            assert host.v('pending')==0
            assert guest.data('inshots',100)[water]==host.data('outshots',100)[water]==1
            assert host.v('hash',2)==guest.v('hash',2)
            # Duplicate a valid shot/result after commit: state must stay put.
            for direction,destination in ((1-host.linkport,1-guest.linkport),(1-guest.linkport,1-host.linkport)):
                candidates=[p for p in frames(cable.observed[direction]) if p[3] in (2,3)]
                for packet in candidates[-2:]:
                    cable.inject(destination,encode(packet))
            Q.pace(host,.6)
            assert all(m.v('turn',2)==1 for m in machines)
            # CRC-valid structural errors and bad CRCs must be refused.
            bad=bytearray(next(p for p in frames(cable.observed[1-host.linkport]) if p[3]==2))
            target=1-guest.linkport
            bad[19]=100
            before_bad=guest.v('badframes',2)
            cable.inject(target,encode(bad))
            bad[2]=99
            cable.inject(target,encode(bad))
            cable.inject(target,b'\x7eWF\x01\x7d\x7e'+encode(bad)[:-2]+b'\0\x7e')
            guest.wait_for(lambda:guest.v('badframes',2)>before_bad,'invalid frames discarded')
            assert all(m.v('turn',2)==1 for m in machines)
            # Chat letters are edit input, not fleet/fullscreen/mute controls.
            guest.key('t')
            for key in ('r','a','m','f'):
                guest.key(key)
            assert guest.v('muted')==0 and guest.v('chatmode')==1
            guest.key('ret')
            host.wait_for(lambda:b'ramf' in host.data('chatline',40),'chat received')
            guest.wait_for(lambda:guest.v('chatpending')==0,'chat acknowledged')
            chat_count=host.v('chatcount',2)
            chat=next(p for p in frames(cable.observed[1-guest.linkport]) if p[3]==4)
            cable.inject(1-host.linkport,encode(chat))
            Q.pace(host,.6)
            assert host.v('chatcount',2)==chat_count,'duplicate chat appended twice'
            # Complete a real match: captain 0 targets the peer's fleet; the
            # other captain deliberately targets water. Only target selection
            # is a fixture; Enter, serial frames and judgment are shipped code.
            hit_cells=[i for i,c in enumerate(guest.data('board',100)) if c]
            misses=[i for i,c in enumerate(host.data('board',100)) if not c]
            expected_turn=1
            for cell in hit_cells:
                guest.wait_for(lambda:guest.v('peerturn',2)==expected_turn,'turn acknowledgment')
                guest.fire(misses.pop())
                expected_turn+=1
                for m in machines:
                    m.wait_for(lambda m=m:m.v('turn',2)==expected_turn,'peer miss committed')
                host.wait_for(lambda:host.v('peerturn',2)==expected_turn,'shot turn acknowledged')
                host.fire(cell)
                expected_turn+=1
                for m in machines:
                    m.wait_for(lambda m=m:m.v('turn',2)==expected_turn,'hit/sunk committed')
                assert host.v('hash',2)==guest.v('hash',2),'history hashes diverged'
            assert host.v('finished')==1 and guest.v('finished')==2
            assert sum(c>=2 for c in host.data('outshots',100))==17
            assert all(c in (0,1,3) for c in host.data('outshots',100)),'sunk extents incomplete'
            assert host.v('enemysunk')==31
            ended=[m.data('board',300) for m in machines]
            host.fire(99)
            assert [m.data('board',300) for m in machines]==ended,'shot after victory'
            # Validate privacy in actual outgoing bytes, rather than an
            # independently reimplemented game's state snapshots.
            for observed in cable.observed:
                packets=list(frames(observed))
                assert packets and all(p[3]<=4 for p in packets)
                for p in packets:
                    if p[3]!=3 or p[20]!=3:
                        assert p[21:24]==b'\0\0\0','unhit fleet coordinates crossed the wire'
            for i,m in enumerate(machines):
                m.hmp('screendump "'+W.artifact('wirefleet-battle-%d.ppm'%i)+'"')
                m.ui.close(m.ui.window('WireFleet'))
                assert m.read(W.S('ser_row'),2)==b'\xff\xff'
                m.mo.to(100,100)
                assert m.mo.where()[:2]==(100,100),'serial mouse lost after match close'
            print('wirefleetlink: two ports, turn rules, lost result, save/close/load/reconnect, duplicate shots/chat, invalid frames, all ships sunk, victory and cleanup')
        except BaseException:
            for i,m in enumerate(machines):
                print('wirefleet failure PC',i,m.state(),
                      'ring',m.data('serialbuf',5).hex(),
                      'tx',m.v('txpos',2),m.v('txlen',2),
                      'identity/peer/match',m.data('identity',12).hex(),flush=True)
                print('UART',m.hmp('i /b '+hex((0x3f8,0x2f8)[m.linkport]+5)),
                      m.hmp('info pic').splitlines()[-2:],flush=True)
                m.hmp('screendump "'+W.artifact('wirefleet-failure-%d.ppm'%i)+'"')
            if cable:
                for i,wire in enumerate(cable.observed):
                    print('wirefleet wire',i,len(wire),'bytes',
                          [(p[3],p[4:16].hex()) for p in list(frames(wire))[-6:]],flush=True)
            raise
        finally:
            if cable:
                cable.close()
            for m in machines:
                m.cleanup()


if __name__=='__main__':
    main()
