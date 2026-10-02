#!/usr/bin/env python3
"""P0 compiler rejection cases, guest/host movement traces and actor/retry matrix.
Invoked by stickio.py so the established host and adapter registry rows cover it.
"""
from copy import deepcopy
from pathlib import Path
import json
import re
import struct
import subprocess
import tempfile

from stickio_courses import authored_course, validate_course, decode_map, encode_map
from stickio_sim import Player, Simulation, FIELDS, SIGNED, BYTE_FIELDS

ROOT = Path(__file__).resolve().parents[1]


def rejected(call, message):
    try:
        call()
    except ValueError as error:
        assert message in str(error), (message, str(error))
    else:
        raise AssertionError('compiler accepted '+message)


def host(raw, sym):
    from stickio import at
    from stickio_assets import courses, generate
    fixture_path = ROOT/'apps/stickio/levels/p0-first-room.json'
    fixture = authored_course(fixture_path)
    assert fixture['enemies'] == [[176, 56, 1, 1], [528, 88, 2, 1]]
    levels = courses()
    for course in levels:
        validate_course(course)
        assert all(y==28 for _,y,kind,_ in course['enemies'] if kind==3), 'legacy flyer patrol migration'
    cases = (
        ('illegal tile', lambda l: l['map'][10].__setitem__(3, 16)),
        ('unsupported spawn', lambda l: l.__setitem__('spawn', [32, 56])),
        ('unsupported spawn', lambda l: l.__setitem__('spawn', [32, 104])),
        ('unprotected checkpoint', lambda l: l['map'][23].__setitem__(4, 5)),
        ('unsafe checkpoint arrival support', lambda l: l['map'][23].__setitem__(6, 0)),
        ('duplicate/empty object ID', lambda l: l['objects'][1].__setitem__('id', l['objects'][0]['id'])),
        ('actor pool exceeded', lambda l: l.__setitem__('enemies', l['enemies']*11)),
        ('activation/render budget', lambda l: (
            l.__setitem__('enemies', [[i*20+500,72,3,1] for i in range(7)]),
            l.__setitem__('objects', [dict(id='actor-'+str(i),x=i*20+500,y=72,
                                          kind='flyer',direction=1) for i in range(7)]))),
        ('missing exit', lambda l: l['map'][44].__setitem__(5, 0)),
    )
    for message, mutate in cases:
        level = deepcopy(fixture)
        mutate(level)
        rejected(lambda: validate_course(level), message)
    for encoded, message in ((b'\0\0', 'invalid RLE'), (b'\1\16', 'invalid RLE'),
                             (b'\1', 'truncated RLE'), (b'\1\0', 'incomplete RLE'),
                             (b'\xff\0'*2, 'RLE exceeds')):
        rejected(lambda: decode_map(encoded, 20), message)
    doc = json.loads(fixture_path.read_text())
    with tempfile.TemporaryDirectory() as td:
        root = Path(td)
        invalid = root/'invalid.json'
        for message, change in (
            ('room links', lambda d: d['rooms'][0]['links'].append(dict(destination='missing'))),
            ('unsupported course version', lambda d: d.__setitem__('version', 2)),
            ('unsupported course field', lambda d: d.__setitem__('timer', 300)),
            ('invalid course ID', lambda d: d.__setitem__('id', '../escaped')),
        ):
            data = deepcopy(doc)
            change(data)
            invalid.write_text(json.dumps(data))
            rejected(lambda: authored_course(invalid), message)
        generate(root/'first', course=fixture_path)
        generate(root/'second', course=fixture_path)
        assert (root/'first/assets.inc').read_bytes() == (root/'second/assets.inc').read_bytes()
        subprocess.run(['nasm','-f','bin','-w+error','-I',str(ROOT/'apps')+'/',
            '-I',str(ROOT/'apps/stickio')+'/', '-I',str(root/'first')+'/',
            '-o',str(root/'fixture.bin'),str(ROOT/'apps/stickio/stickio.asm')], check=True)
        assert json.loads((root/'first/levels.json').read_text())[0] == fixture
    manifest = json.loads(at('build/stickio-art/manifest.json').read_text())
    assert len(manifest['courses']) == 30 and len(manifest['materials']) == 10
    for i, course in enumerate(levels):
        header = struct.unpack_from('<4H2B4H', raw, sym['st_level%d'%i])
        assert header[6:] == (*course['spawn'], *course['checkpoint'])
        assert manifest['courses'][i]['rewards'] == course['rewards']
    expected = dict(ST_WALK=448, ST_RUN=768, ST_ACCEL=48, ST_FRICTION=64,
        ST_JUMP=-1664, ST_RELEASE=-640, ST_GRAVITY=96, ST_FALL=1536,
        ST_STOMP=-1152, ST_SPRING=-2176, ST_ASSIST=5, ST_PROTECTION=90)
    constants = dict((name,int(value)) for name,value in re.findall(
        r'^%define (ST_\w+) (-?\d+)$', (ROOT/'apps/stickio/const.inc').read_text(), re.M))
    assert all(constants[k] == v for k,v in expected.items())
    image, bss = struct.unpack_from('<HH', raw, 8)
    metrics = dict(image_bytes=image,bss_bytes=bss,total_bytes=image+bss,
        packed_bytes=at('build/stickio.o88').stat().st_size,headroom_bytes=61440-image-bss,
        reference=dict(image_bytes=38696,bss_bytes=14911,packed_bytes=17470),
        authored_fixture='p0-first-room',host_trace_scope='player integer step oracle; no course completion search')
    proof = at('build/stickio-proof');proof.mkdir(exist_ok=True)
    (proof/'p0-size-ledger.json').write_text(json.dumps(metrics,indent=2)+'\n')
    print('P0 host: authored fixture builds deterministically; schema/RLE/pool/arrival rejection checks; sizes',metrics)


def snapshot(p):
    start = min(p.sym['st_'+field] for field in FIELDS)
    end = max(p.sym['st_'+field]+(1 if field in BYTE_FIELDS else 2) for field in FIELDS)
    data = p.m.read(p.base+start, end-start)
    state = {}
    for field in FIELDS:
        off = p.sym['st_'+field]-start
        value = data[off] if field in BYTE_FIELDS else struct.unpack_from('<H', data, off)[0]
        if field in SIGNED and value & 32768:
            value -= 65536
        state[field] = value
    return state


def guest(p):
    traces = 0
    def reset(mp, **values):
        p.put('level',0,2);p.call('newcourse');p.call('load')
        p.put('ne',0,2);p.put('jumpheld',0);p.put('keys',0)
        p.m.write(p.a('map'), bytes(v for col in mp for v in col))
        for field, value in values.items():
            p.put(field,value,1 if field in BYTE_FIELDS else 2)
        return Simulation(mp, Player(**snapshot(p)))
    flat = [[0]*6+[1,1] for _ in range(64)]
    scenarios = []
    scenarios.append(('accel-reverse-friction', flat, {}, [2]*20+[1]*28+[0]*12))
    scenarios.append(('held-run-jump', flat, {}, [10]*16+[14]*35+[10]*10))
    scenarios.append(('tap-jump', flat, {}, [4]+[0]*28))
    scenarios.append(('buffered-tap', flat, dict(jumpbuf=5), [0]*30))
    ceiling = deepcopy(flat);ceiling[2][3]=2;ceiling[3][3]=2
    scenarios.append(('ceiling-corners', ceiling, {}, [4]*20+[0]*12))
    wall = deepcopy(flat);wall[5][4]=wall[5][5]=2
    scenarios.append(('wall-and-reversal', wall, {}, [10]*24+[1]*22))
    edge = deepcopy(flat)
    for x in (3,4,5):edge[x][6]=edge[x][7]=0
    scenarios.append(('coyote-departure', edge, dict(x=43,vx=448), [2]*4+[6]*8+[2]*24))
    ledge = deepcopy(flat);ledge[2][4]=ledge[3][4]=9
    scenarios.append(('oneway-crossings', ledge, {}, [4]*28+[0]*15))
    spring = deepcopy(flat);spring[2][5]=6
    scenarios.append(('spring', spring, dict(y=55*256,vy=256,ground=0,coyote=0), [0]*28))
    for name, mp, values, keys in scenarios:
        model = reset(mp, **values)
        for index, key in enumerate(keys):
            p.put('keys',key);p.call('step');model.step(key)
            actual = snapshot(p)
            expected = {field:getattr(model.player,field) for field in FIELDS}
            assert actual == expected, (name,index,key,{f:(actual[f],expected[f]) for f in FIELDS if actual[f]!=expected[f]})
            traces += 1
    # Ground actors retain the authored support height and follow lowered terrain.
    def actor(mp, x, y, kind=1, direction=1, age=0, vy=0):
        reset(mp);p.put('ne',1,2);p.put('x',0,2)
        p.m.write(p.a('enemies'),struct.pack('<HHBbHHHHH',x,y,kind,direction,age,vy&65535,0,y,y))
    def record():
        return struct.unpack('<HhBbHhHHH',p.data('enemies',16))
    for support in (3,4,5,6,7):
        mp = [[0]*8 for _ in range(64)]
        for col in mp:col[support]=1
        y = support*16-24
        actor(mp,100,y)
        for _ in range(8):p.call('enemy_step')
        assert record()[1] == y and record()[5] == 0, ('authored support',support,record())
    drop = deepcopy(flat)
    for x in range(8,12):drop[x][6]=0
    actor(drop,119,72)
    for _ in range(25):p.call('enemy_step')
    assert record()[1]==88 and record()[5]==0, ('walker lowered landing',record())
    actor(drop,119,72,kind=2)
    p.call('enemy_step')
    assert record()[3] == -1 and record()[1]==72, ('hopper ledge policy',record())
    high = deepcopy(flat)
    for x in range(4,12):high[x][4]=1;high[x][5]=1
    actor(high,100,40,kind=2)
    for _ in range(6):p.call('enemy_step')
    assert record()[1]==40 and record()[5] < 0, ('hopper launches from support',record())
    for _ in range(5):p.call('enemy_step')
    assert record()[1]<40, ('hopper ascent',record())
    actor(flat,100,12,kind=3)
    p.call('enemy_step')
    assert record()[1]==13, ('flyer authored center',record())
    wall = deepcopy(flat);wall[8][4]=wall[8][5]=2
    actor(wall,114,72)
    p.call('enemy_step')
    assert record()[0]==114 and record()[3]==-1, ('actor wall turn',record())
    oneway = deepcopy(flat);oneway[6][4]=9
    actor(oneway,100,36,vy=1024)
    p.call('enemy_step')
    assert record()[1]==40 and record()[5]==0, ('actor crossed one-way',record())
    actor(oneway,100,48,vy=-1000)
    p.call('enemy_step')
    assert record()[1]<48 and record()[5]<0, ('actor one-way underside',record())
    ceiling = deepcopy(flat);ceiling[6][2]=2
    actor(ceiling,100,40,vy=-1000)
    p.call('enemy_step')
    assert record()[1]==38 and record()[5]==0, ('actor ceiling',record())
    pit = deepcopy(flat)
    for x in range(5,18):pit[x][6]=pit[x][7]=0
    actor(pit,100,72)
    for _ in range(24):p.call('enemy_step')
    assert record()[2]==0, ('pit retirement',record())
    actor(flat,306,72,direction=-1)
    p.put('x',304,2);p.put('invuln',0);p.put('lives',3)
    p.call('enemy_step')
    assert p.b('lives')==3, 'invisible boundary actor damaged player'
    # Runtime convergence can exceed authored density. Advance every nearby
    # actor, report overflow, and refuse contact from origins omitted by drawing.
    reset(flat);p.put('ne',7,2);p.put('x',220,2);p.put('invuln',0);p.put('lives',3)
    p.m.write(p.a('enemies'),b''.join(struct.pack('<HHBbHHHHH',x,72,1,1,0,0,0,72,72)
        for x in (40,60,80,100,120,140,220)))
    p.call('enemy_step')
    assert p.b('lives')==3 and p.w('actor_overflow')>0
    records=p.data('enemies',7*16)
    assert all(struct.unpack_from('<H',records,i*16+6)[0]==1 for i in range(7)), 'visible actor froze'
    # A rebuilt enemy can be stomped again but cannot mint another score reward.
    p.call('newcourse');p.call('load');p.put('ne',1,2)
    def stomp():
        p.m.write(p.a('enemies'),struct.pack('<HHBbHHHHH',64,72,1,1,0,0,0,72,72))
        p.put('x',64,2);p.put('y',55*256,2);p.put('vy',1024,2)
        p.put('ground',0);p.put('coyote',0);p.put('keys',0);p.call('step')
    score=p.w('score');stomp()
    assert p.w('score')==score+25
    p.call('load');p.put('ne',1,2);stomp()
    assert p.w('score')==score+25, 'retry duplicated enemy score'
    # Rewards collected through gameplay remain consumed after RLE reconstruction.
    p.call('newcourse');p.call('load');p.put('ne',0,2)
    mp = p.data('map',64*8)
    for tile in (3,4):
        index = mp.index(tile)
        x, row = divmod(index,8)
        p.put('x',x*16,2);p.put('ground',0);p.put('coyote',0);p.put('jumpheld',1)
        p.put('keys',4 if tile==3 else 0)
        p.put('y',(row*16+(17 if tile==3 else -16))*256,2)
        p.put('vy',-1000 if tile==3 else 0,2)
        coins, score = p.w('coins'), p.w('score')
        p.call('step')
        assert p.w('coins') == coins+1 and p.w('score') == score+10, ('reward',tile)
        p.put('lives',3);p.call('respawn')
        assert p.data('map',512)[index] == (2 if tile==3 else 0), 'reward restored on retry'
        assert p.w('coins')==coins+1 and p.w('score')==score+10
    # A raised checkpoint stores authored x/y, not the incidental overlap position.
    header = p.a('level0')+14
    old_header = p.m.read(header,4)
    try:
        p.m.write(header,struct.pack('<HH',384,56))
        p.call('load');p.put('ne',0,2)
        p.m.write(p.a('map')+24*8+4,b'\x08')
        p.m.write(p.a('map')+24*8+5,b'\x02')
        p.put('x',389,2);p.put('y',56*256,2);p.put('vy',0,2);p.put('keys',0)
        p.call('step')
        assert (p.w('checkpoint'),p.w('checkpoint_y')) == (384,56)
        p.put('lives',3);p.call('respawn')
        assert p.w('x')==384 and p.w('y')==56*256 and p.b('invuln')==90
    finally:
        p.m.write(header,old_header)
    p.call('newcourse');p.call('load')
    assert not any(p.data('rewards',160)), 'new course did not reset ledger'
    print('P0 guest:',traces,'per-step host matches; support/ledge/hop/flyer/pit/contact and reward/checkpoint matrix',flush=True)
