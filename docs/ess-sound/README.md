# ESS sound change: screenshots

Captured from actual MartyPC guests on 2026-11-13, cropped to the window and
scaled 2× with nearest-neighbor sampling. None is a host-rendered mockup.

| adapter | machine | captures |
|---|---|---|
| VGA | `os8088_xt_vga` | `vga-before.png`, `vga-after.png`: Control Panel → Drivers |
| CGA | `os8088_5150_sb_gla` | `cga-before.png`, `cga-after.png`: Control Panel → Drivers |
| Hercules | `os8088_5150_herc_sb_gla` | `herc-before.png`, `herc-after.png`: Control Panel → Drivers |

The before captures use the original main checkout's build and the after
captures this branch's build. The Sound playing-memory estimate changes
26.5KB → 27KB to include the larger transient driver image. These pictures
show the accounting change; they do not establish ESS playback.

`vga-dma3.png`, `cga-dma3.png` and `herc-dma3.png` show Sound Debug's
selected-channel report. The `snddiag*` tests first finish their real
ten-second **DMA-1** tone, then inject **3 into the guest driver's reported
DMA byte**, ask the real program to refresh, and take the screenshot.
The card remains the emulator's Sound Blaster, **not an ESS**. Those pictures
prove the text can show DMA 3 / `D3`; they do not prove audio on DMA 3.

Their machines are `os8088_xt_vga_sb`, `os8088_5150_sb_gla`, and
`os8088_5150_herc_sb_720_gla`, respectively. The tests then inject a refused
route, check `DMA none` and no BLASTER line, and restore the original byte.
See [the research](../ESS-SOUND.md) and SPEC.md §34.5.8 / §127.3.
