#!/usr/bin/env python3
"""Independent integer player oracle for Stickio movement, not a course playthrough.
Units: x pixels plus frac/256; y, vx and vy signed 8.8; timers in steps.
This models the guest's order, including release before buffered launch,
negative horizontal flooring, leading-edge probes, and one-way crossing.
No ROM, emulator, assembly translation or generated gameplay code is used.
"""
from dataclasses import dataclass

FIELDS = ('x', 'frac', 'y', 'vx', 'vy', 'oldy', 'ground', 'jumpheld', 'jumpbuf', 'jump_tier',
          'coyote', 'land', 'invuln', 'facing', 'gait', 'cam', 'state', 'score',
          'coins', 'lives', 'checkpoint', 'checkpoint_y')
SIGNED = {'y', 'vx', 'vy', 'oldy'}
BYTE_FIELDS = {'ground', 'jumpheld', 'jumpbuf', 'jump_tier', 'coyote', 'land', 'invuln', 'facing', 'state', 'lives'}


@dataclass
class Player:
    x: int = 32
    frac: int = 0
    y: int = 72*256
    vx: int = 0
    vy: int = 0
    oldy: int = 0
    ground: int = 1
    jumpheld: int = 0
    jumpbuf: int = 0
    jump_tier: int = 0
    coyote: int = 5
    land: int = 0
    invuln: int = 90
    facing: int = 0
    gait: int = 0
    cam: int = 0
    state: int = 0
    score: int = 0
    coins: int = 0
    lives: int = 5
    checkpoint: int = 32
    checkpoint_y: int = 72


class Simulation:
    def __init__(self, mp, player=None, checkpoint=None, final=False):
        self.map = [list(col) for col in mp]
        self.initial = [list(col) for col in mp]
        self.player = player or Player()
        self.checkpoint = checkpoint or [len(mp)//2*16, 72]
        self.final = final
        self.consumed = set()

    def tile(self, x, y):
        if x < 0 or y < 0 or y >= 128 or x//16 >= len(self.map):
            return 0, None
        return self.map[x//16][y//16], (x//16, y//16)

    def consume(self, address, value):
        self.consumed.add(address)
        x, y = address
        self.map[x][y] = value

    def coin(self):
        p = self.player
        p.coins = (p.coins+1) & 65535
        p.score = (p.score+10) & 65535
        if p.coins % 50 == 0 and p.lives < 9:
            p.lives += 1

    def respawn(self):
        p = self.player
        if p.lives <= 1:
            p.lives, p.state = 0, 2
            return
        p.lives -= 1
        p.x, p.y = p.checkpoint, p.checkpoint_y*256
        p.vx = p.vy = p.frac = p.gait = 0
        p.jump_tier = 0
        p.ground, p.coyote, p.jumpbuf, p.land, p.invuln, p.facing, p.state = 1, 5, 0, 0, 90, 0, 0
        self.map = [list(col) for col in self.initial]
        for x, y in self.consumed:
            self.map[x][y] = 2 if self.initial[x][y] == 3 else 0
        self.camera(reset=True)

    def camera(self, reset=False):
        p = self.player
        # Project the previous viewport onto the aligned interval that keeps
        # the player origin in [104,136], then constrain it to the room.
        lower = ((p.x-136+3)//4)*4
        upper = ((p.x-104)//4)*4
        cam = p.x-120 if reset else min(max(p.cam, lower), upper)
        p.cam = min(max(cam, 0), len(self.map)*16-320) & ~3

    def step(self, keys):
        p = self.player
        for name in ('invuln', 'land', 'jumpbuf'):
            setattr(p, name, max(0, getattr(p, name)-1))
        if keys & 4:
            if not p.jumpheld:
                p.jumpbuf, p.jumpheld = 5, 1
        else:
            p.jumpheld = 0
            p.vy = max(p.vy, -640)
        p.coyote = 5 if p.ground else max(0, p.coyote-1)
        if p.jumpbuf and p.coyote:
            speed = abs(p.vx)
            p.jump_tier = 2 if speed >= 640 else 1 if speed >= 256 else 0
            p.vy = (-1664, -1728, -1792)[p.jump_tier]
            p.ground = p.coyote = p.jumpbuf = 0
        cap = 768 if keys & 8 else 448
        lr = keys & 3
        opposing = (lr == 1 and p.vx > 0) or (lr == 2 and p.vx < 0)
        if p.ground and opposing:
            p.facing = 1 if lr == 1 else 0
            p.vx = max(p.vx-96, 0) if lr == 1 else min(p.vx+96, 0)
        elif lr == 1:
            p.facing, p.vx = 1, max(p.vx-48, -cap)
        elif lr == 2:
            p.facing, p.vx = 0, min(p.vx+48, cap)
        elif p.vx > 0:
            p.vx = max(p.vx-64, 0)
        elif p.vx < 0:
            p.vx = min(p.vx+64, 0)
        velocity = p.vx+p.frac
        p.frac = velocity & 255
        newx = min(max(p.x+(velocity//256), 0), len(self.map)*16-16)
        edge = newx+(0 if p.vx < 0 else 13)
        top = p.y//256+3
        if any(self.tile(edge, yy)[0] in (1, 2, 3) for yy in (top, top+19)):
            p.vx, p.frac = 0, 0
        else:
            p.gait = (p.gait+abs(newx-p.x)) & 65535
            p.x = newx
        p.oldy = p.y
        gravity = (96, 96, 96)[p.jump_tier] if p.vy < 0 else (96, 112, 128)[p.jump_tier]
        p.vy = min(p.vy+gravity, 1536)
        newy = p.y+p.vy
        if not -32768 <= newy <= 32767:
            self.respawn()
            return
        p.y, p.ground = newy, 0
        yy = p.y//256
        if p.vy >= 0:
            feet = yy+24
            for x in (p.x+3, p.x+12):
                tile, _ = self.tile(x, feet)
                if tile in (1, 2, 3) or (tile in (6, 9) and p.oldy//256+24 <= (feet & ~15)):
                    p.y, p.vy, p.ground = ((feet & ~15)-24)*256, 0, 1
                    p.jump_tier = 0
                    if tile == 6:
                        p.vy, p.ground, p.coyote = -2176, 0, 0
                    elif p.oldy-p.y < -256:
                        p.land = 5
                    break
        else:
            for x in (p.x+3, p.x+12):
                tile, address = self.tile(x, yy)
                if tile in (1, 2, 3):
                    if tile == 3:
                        self.consume(address, 2)
                        self.coin()
                    p.y, p.vy = ((yy & ~15)+16)*256, 0
                    break
        tile, address = self.tile(p.x+8, p.y//256+16)
        if tile == 4:
            self.consume(address, 0)
            self.coin()
        elif tile == 5:
            self.respawn()
            return
        elif tile == 6 and p.vy >= 0:
            p.vy, p.ground, p.coyote = -2176, 0, 0
            p.jump_tier = 0
        elif tile == 8:
            p.checkpoint, p.checkpoint_y = self.checkpoint
            self.consume(address, 0)
        elif tile == 7:
            p.state = 3 if self.final else 1
            p.score = (p.score+100) & 65535
        if p.y > 127*256:
            self.respawn()
            return
        self.camera()
