# The play's profile on the Victory 286: three of the owner's files

**A measurement, not a description.** Taken 2026-10-15 by the owner on the
**Packard Bell Victory 286** (docs/FIELD-MACHINES.md), PicoMEM 1.x as disk
and Sound Blaster, the bus at 8 wait states, with `9be3a228`'s VIDEOD.O88
(VPDIAG=1, SPEC.md 98.3.22): three of the owner's own files, each played to
its end. All three **looked right to the owner**, Bad Carrot's two slow
stretches included ("better than chopping the scene to pieces"), and none
broke its sound. True of that machine, those files and those plays; not
maintained. `tools/os88vfit.py` read the profiles; its output is at the end.

| file | made with | measured / model (records > 2 ms) | whole frame, mean of the period | 95% | most |
|---|---|---:|---:|---:|---:|
| 01 Bad Carrot, 320x180 VGA8 Mode X, flipped | 286-picomem1, `--avg 4.8 --peak 4.8 --flip-lag 5` | 2.41 (two decodes: see below) | 35.1% (model 29.1%) | 86.8% | 126.4% |
| 03 Gas Gas Gas, 640x300 VGA4, page-ahead | 286-fast, `--avg 4 --peak 4` | **1.009** | 42.8% (model 41.6%) | 90.1% | 124.7% |
| 04 Cowboy Bebop, 640x400 VGA4, page-ahead | 286-fast, `--avg 4 --peak 4` | **0.990** | 40.3% (model 39.4%) | 84.5% | 108.9% |

All three in place from an XMS bank (bank kind 8, 98.3.18.13).

## What it says

- **The decode model is NOT pessimistic.** On VGA4 a record costs what
  the model says to within 1% at the median (0.948-1.264 between the 10th
  and 90th percentiles on 03); the whole frame's share is the model's +1
  point. The fits: 03 `draw = 0.867 x model + 2,443 us`, 04 `0.941 x model
  + 1,166 us` - a FIXED 1-2.5 ms a frame the model does not have (in place,
  the borrows; and the frame's own work outside the lists).
- **Mode X flipped is under-priced**: with a term for the back page (the
  previous record decoded again) and one for the record's size,
  `draw = 1.102 x model + 0.952 x the last record's + 146.5 us a KB +
  1,095 us` (rms 503 us over 5,187 frames). The 146.5 us a KB is in-place
  reading paid TWICE - the record and the back page's copy of it - which
  is ~73 us a KB a pass: one borrow (~150 us) per 2,048 bytes, as
  98.2.1.3.4's `xbor_us`/`xyield` estimate has it.
- **So the owner's higher `--peak` was not buying back an over-priced
  model.** It was buying the PLAYER'S TOLERANCE, which the encoder does
  not model: replayed through the schedule with its measured frame times,
  Bad Carrot falls **up to 4.46 frames (186 ms) behind its sound** in five
  stretches of up to 2.0 s (frames 4476-4520 and 2130-2178 the longest -
  the flames and the suns), and catches up every time. Its `--flip-lag 5`
  is what allowed it, and the file used 4.46 of the 5. The player counted
  621 late frames; the owner saw "two slowdown sections" and no break. 03
  is never 0.39 of a frame behind, 04 never 0.09.


## 01

```
01-64GRY.V88: 'Bad Carrot - 320*240*64', 5188 frames, VGA8 on 5, profile 286-picomem1
   played: 5187 frames timed of 5188; sound 1, flip 1, in place bank, bank kind 8, late 621
   the draw = 1.218 x model + 0.952 x the last record's + 1175 us (rms 506 us over 5187 frames)
   measured/model, records past 2 ms of model: median 2.411, 10% 2.122, 90% 2.759
   the whole frame, of a 41.6 ms period: mean 35.1%, 95% 86.8%, most 126.4% (frame 4523)
   ...as the model priced the draw: mean 29.1%, 95% 76.0%, most 117.7%
```

## 03

```
03-640FS.V88: 'Gas Gas Gas - 640*400*16', 1698 frames, VGA4 on 3, profile 286-fast
   played: 1697 frames timed of 1698; sound 1, flip 2, in place bank, bank kind 8, late 0
   the draw = 0.867 x model + 2443 us (rms 4121 us over 1694 frames)
   measured/model, records past 2 ms of model: median 1.009, 10% 0.948, 90% 1.264
   the whole frame, of a 41.6 ms period: mean 42.8%, 95% 90.1%, most 124.7% (frame 628)
   ...as the model priced the draw: mean 41.6%, 95% 88.2%, most 155.9%
   3 frames ran 54 ms or more (channel 2 wrapped): left out
```

## 04

```
04-640FS.V88: 'Cowboy Bebop - 640*400*16', 1803 frames, VGA4 on 3, profile 286-fast
   played: 1802 frames timed of 1803; sound 1, flip 2, in place bank, bank kind 8, late 0
   the draw = 0.941 x model + 1166 us (rms 1623 us over 1802 frames)
   measured/model, records past 2 ms of model: median 0.990, 10% 0.961, 90% 1.113
   the whole frame, of a 50.0 ms period: mean 40.3%, 95% 84.5%, most 108.9% (frame 962)
   ...as the model priced the draw: mean 39.4%, 95% 86.4%, most 111.7%
```
