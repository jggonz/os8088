#!/usr/bin/env python3
# =============================================================================
# os8088 - tests/arcsound.py - ARCADE's sound routes on a 4.77 MHz 8088
# (docs/ARCADE-SPEC.md section 8)
#
#     python3 tests/arcsound.py [sb|fm ...]
#
# Space Invaders, played with fire held, on two MartyPC machines:
#
#   sb  the 5150 with a Sound Blaster: the route is the STREAM (V_SROUTE 2);
#       every byte mixed is read back out of the ring (V_SBTOT, V_SBSEG) and
#       it is SOUND - in play more than 5% of it away from silence, none of it
#       outside the +-126 three voices at +-42 can reach - fed at the CARD'S
#       rate (an 8088 mixes at 4,000 Hz, ARCADE-SPEC 8.3) within 5%; the board
#       keeps its 59.54 frames a second within 3% with at most one half frame
#       dropped; and Esc closes the ring (V_SBSEG 0).
#   fm  a Hercules XT with an AdLib and no Sound Blaster: the route is FM
#       (V_SROUTE 1, V_SWHY 2: no stream there) and the chip SOUNDS - the
#       OPL's own capture (MARTYPC_WAV) loud in play - and Esc gives the chip
#       back (SOUND.DRV's opl_xown none).
#
# The machine RUNS while the sound is taken: MartyPC drains a capture only
# between free-running batches, so a capture taken over `advance` is empty
# whatever the guest played. Waits are on the guest: its own frame counter
# (V_NFRAME) or guest_sleep, never the host's clock.
# =============================================================================
import os
import struct
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(ROOT, 'tools'))
sys.path.insert(0, HERE)
os.chdir(ROOT)
import os88marty                    # noqa: E402
import os88ui                       # noqa: E402

MHZ = 4.77273
BOARD_HZ = 59.54
V_NFRAME, V_NDROP = 0x6044, 0x6046
V_SROUTE, V_SBTOT, V_SBSEG = 0x604E, 0x6050, 0x6052
RL = 4096


def find_view(m):
    mem = bytes(m.read(0, 0xA0000))
    i = 0
    while True:
        i = mem.find(b'ARCV', i + 1)
        if i < 0:
            raise SystemExit('arcsound: FAIL - no ARCADE view in guest memory')
        if (i - 0x6040) % 16 == 0:
            return i - 0x6040


def word(m, a):
    return struct.unpack('<H', bytes(m.read(a, 2)))[0]


def frames_until(m, v, n):
    """Run until the board has delivered N more frames (its own counter)."""
    f0 = word(m, v + V_NFRAME)
    while (word(m, v + V_NFRAME) - f0) & 0xFFFF < n:
        os88marty.guest_sleep(m, 0.05)


def start(ui):
    """ARCADE open, full screen, a coin and a start; fire held and the base
    moving. Out: the view's linear address."""
    m = ui.m
    ui.path('B:/ARCADE.O88')
    m.run()
    m.key('Enter')
    os88marty.guest_sleep(m, 0.5)
    v = find_view(m)
    frames_until(m, v, 300)             # the board's power-on
    m.key('Digit5')
    frames_until(m, v, 60)
    m.key('Digit1')
    frames_until(m, v, 240)             # the base on the screen
    m.key('Space', down=True, up=False)
    m.key('ArrowLeft', down=True, up=False)
    return v


def arm_sb():
    bad = []
    with os88ui.boot('build/os8088-360.img', apps='build/arcade360.img',
                     machine='os8088_5150_sb', quick=True) as ui:
        m = ui.m
        v = start(ui)
        route = bytes(m.read(v + V_SROUTE, 2))
        if route[0] != 2:
            bad.append('the route is %d (declined at step %d), not the stream' % tuple(route))
            return bad
        seg = word(m, v + V_SBSEG)
        last = word(m, v + V_SBTOT)
        c0 = m.status()['cycles']
        f0, d0 = word(m, v + V_NFRAME), word(m, v + V_NDROP)
        stream = bytearray()
        flip = 0
        while (m.status()['cycles'] - c0) / (MHZ * 1e6) < 10.0:
            os88marty.guest_sleep(m, 0.15)  # 600 bytes at 4 kHz: the ring
            m.pause()                       #   holds 4,096
            tot = word(m, v + V_SBTOT)
            n = (tot - last) & 0xFFFF
            if n > RL - 1024:
                bad.append('the reader fell %d bytes behind the ring' % n)
                break
            ring = bytes(m.read(seg * 16, RL))
            stream += bytes(ring[(last + i) & (RL - 1)] for i in range(n))
            last = tot
            flip += 1
            if flip % 6 == 0:               # the base turns about now and then
                key = 'ArrowRight' if (flip // 6) % 2 else 'ArrowLeft'
                other = 'ArrowLeft' if key == 'ArrowRight' else 'ArrowRight'
                m.key(other, down=False, up=True)
                m.key(key, down=True, up=False)
            m.run()
        m.pause()
        dt = (m.status()['cycles'] - c0) / (MHZ * 1e6)
        frames = (word(m, v + V_NFRAME) - f0) & 0xFFFF
        drops = (word(m, v + V_NDROP) - d0) & 0xFFFF
        loud = sum(1 for b in stream if abs(b - 128) > 2)
        lo, hi = (min(stream), max(stream)) if stream else (128, 128)
        rate = len(stream) / dt
        print('arcsound sb: %.1f guest s, %d bytes mixed (%.0f a second), %.1f%% not '
              'silent, %d..%d; %.2f frames/s, %d dropped'
              % (dt, len(stream), rate, 100.0 * loud / max(1, len(stream)), lo, hi,
                 frames / dt, drops))
        if loud < len(stream) * 0.05:
            bad.append('the stream is near silence in play (%d of %d bytes)' % (loud, len(stream)))
        if lo < 2 or hi > 254:
            bad.append('a mixed byte at %d..%d: three voices cannot reach that' % (lo, hi))
        if abs(rate - 4000) > 200:
            bad.append('mixed %.0f bytes a second, not the card\'s 4,000' % rate)
        if abs(frames / dt - BOARD_HZ) > BOARD_HZ * 0.03:
            bad.append('%.2f frames a second, not within 3%% of %.2f' % (frames / dt, BOARD_HZ))
        if drops > 1:
            bad.append('%d half frames dropped' % drops)
        m.key('Space', down=False, up=True)
        m.key('Escape')
        m.run()
        os88marty.guest_sleep(m, 0.5)
        if word(m, v + V_SBSEG) != 0:
            bad.append('Esc left the ring open')
    return bad


def arm_fm():
    import os88drvsym
    import sndcheck
    bad = []
    tmp = tempfile.mkdtemp(prefix='arcsound-')
    cap = os.path.join(tmp, 'audio')
    os.environ['MARTYPC_WAV'] = cap
    try:
        with os88ui.boot('build/os8088-720.img', apps='build/arcade720.img',
                         machine='os8088_5150_herc_adlib_720_gla', quick=True) as ui:
            os.environ.pop('MARTYPC_WAV', None)
            m = ui.m
            v = start(ui)
            route = bytes(m.read(v + V_SROUTE, 2))
            if route[0] != 1:
                bad.append('the route is %d (declined at step %d), not FM' % tuple(route))
            os88marty.guest_sleep(m, 8.0)
            m.key('Space', down=False, up=True)
            m.key('Escape')
            os88marty.guest_sleep(m, 0.5)
            m.pause()
            sy = os88drvsym.symbols('sound.bin')
            fseg = word(m, m.sym('drv_fseg'))
            xown = m.read(fseg * 16 + sy['opl_xown'], 1)[0]
            if xown != 0xFF:
                bad.append('Esc left the chip claimed (opl_xown %d)' % xown)
    finally:
        os.environ.pop('MARTYPC_WAV', None)
    wav = cap + '.adlib_music_synthesizer.wav'
    rate, mono, _ = sndcheck.load(wav)
    play = mono[-int(9 * rate):-int(1 * rate)]      # the play, before the Esc
    rms = (sum(x * x for x in play[::4]) / max(1, len(play[::4]))) ** 0.5
    peak = max((abs(x) for x in play), default=0.0)
    print('arcsound fm: route %d, the OPL in play: RMS %.4f, peak %.3f' % (route[0], rms, peak))
    if peak < 0.05:
        bad.append('the OPL is silent in play (peak %.3f)' % peak)
    return bad


def main():
    arms = sys.argv[1:] or ['sb', 'fm']
    bad = []
    for a in arms:
        bad += ['%s: %s' % (a, b) for b in {'sb': arm_sb, 'fm': arm_fm}[a]()]
    for b in bad:
        print('arcsound: FAIL - ' + b)
    if bad:
        return 1
    print('arcsound: ok')
    return 0


if __name__ == '__main__':
    sys.exit(main())
