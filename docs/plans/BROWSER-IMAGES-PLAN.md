# Browser images — integration plan

This is the 1.7 starting contract for inline images in the windowed Browser. It uses PiXEL and Video Player as implementation references, while keeping the Browser's network, memory and redraw behavior page-specific. No operating system or driver changes are part of this plan. The 1.6 storage work remains a prerequisite: the current Browser uses 16-bit byte offsets and cannot retain a 96 KiB logical source or formatted document until that representation is implemented and tested.

## Audit result: existing behavior versus Browser gaps

| Need | Already provided by os8088 | Still missing in Browser |
|---|---|---|
| VGA/EGA colour drawing | `OSAPI_GFX_BLIT4` and the kernel's four-plane renderer; EGA is the same planar path with its own geometry/mode | Decode and quantize image rows to the os8088 16-colour palette, then call the API |
| Hercules/CGA monochrome fallback | The same `OSAPI_GFX_BLIT4` dispatches through the 1bpp renderer, whose §39.4 palette reduction applies black, 50% dither or white | Nothing adapter-specific; do not add a Browser grayscale converter or probe |
| Window clipping | `BLIT4` honors the window clip and display dispatch | Limit each submitted image band to the image box and use the package's ordinary window clip |
| Static BMP/PCX/PIX decode | Caller-owned `apps/os88img.inc` with signature/header checks and 16-colour palette mapping | It currently requires a whole decoded destination and has no web-resource fetch or Browser image records |
| GIF/JPEG/PNG decode | PiXEL's lazy decode parts; GIF supports static image data, and JPEG/PNG have their own row decoders | No stable Browser-facing decoder ABI; PiXEL parts depend on PiXEL context and service callbacks |
| Fetching inline resources | Browser's HTTP worker fetches one page response, with generation checks and bounded redirects | Separate image/resource transfer ownership, source URL resolution, cancellation, bounded compressed storage and response dispatch |
| Repaint after scroll/occlusion | Normal Browser invalidation and clipping | Durable compressed source plus a deterministic way to restart/redecode rows; no full decoded bitmap per image |

This audit is why image output will be 4bpp regardless of the display: the OS
already provides the adapter conversion. The Browser's job is HTTP resource
ownership, format validation, conversion from source colors into the native
16-colour palette, and bounded redraw. Avoiding duplicate display code reduces
both package size and adapter-specific correctness risk.

## What the OS already adapts

The Browser does not need an adapter probe or a private VGA/EGA/Hercules
renderer. `OSAPI_GFX_BLIT4` is the existing windowed image path. It takes
packed 4bpp pixels, clips them to the window, and dispatches by the display:
VGA and EGA use the four-plane colour renderer; Hercules and CGA reduce each
os8088 palette colour through the kernel's existing ink table, including its
black / 50%-dither / white mapping. This adapter choice is automatic in the
kernel. Use `OSAPI_WM_DISPLAY` when a package genuinely needs display geometry;
do not guess from the primary adapter with `OSAPI_VIDEO`, and do not access
VRAM or use the full-screen page API from this ordinary window.

That means the Browser's image pipeline should produce the same packed
4bpp palette indices on every display and send them through `BLIT4`. EGA gets
the colour image through the same planar path as VGA; Hercules gets the
kernel's monochrome reduction without a second image converter. A Browser-side
1bpp/grayscale pass would duplicate and diverge from §39.4, so it is not part
of the design. JPEG and true-colour PNG still need a Browser/PiXEL-compatible
conversion into the 16 os8088 colours before `BLIT4`; the kernel only adapts
those 16 colours to the active display.

## Reuse from PiXEL and Video Player

PiXEL recognizes GIF, PNG, JPEG, BMP, PCX, TGA, PNM and PIX (`apps/pixel/pixel.asm`, `apps/pixel/pxsimple.inc`, and its lazy decoder parts). Reusable lessons include bounded input heads, decoder work claims acquired per operation, row-oriented output, and optional XMS with a conventional-memory fallback. The decoders are coupled to PiXEL package state and its part protocol. Share source only after defining a shared interface; do not fork or copy decoder files into Browser. See SPEC.md section 106.

The existing `apps/os88img.inc` (SPEC.md §94, indexed in `docs/INDEX.md`) is
already a clean, caller-owned include for `.PIX`, uncompressed 1/4bpp BMP and
1/4-plane PCX. It maps a file palette to the os8088 16-colour palette and
outputs packed 4bpp. Its current contract decodes the *whole* image into a
destination segment and refuses output above one 64 KiB segment; it is not a
row-streaming web decoder and it does not cover GIF, JPEG or PNG. Use it as a
reference and do not silently turn its full-bitmap allocation into the
Browser's image store. The Browser's required web formats need a shared
row-oriented decoder contract with caller-owned state and bounded compressed
input.

Video Player's EMS/XMS rings and VRAM renderers solve a different job. They show bounded staging and adapter-specific output, but a normal Browser window draws through clipped graphics APIs. It cannot keep a raw framebuffer pointer or use fullscreen page flipping. Displayed pixels are transient: the Browser must redraw images after exposure, scrolling, resizing and repaint. See SPEC.md section 98 and docs/plans/VIDEO-XMS-PLAN.md.

## Staged format support

1. Start with static GIF and baseline JPEG, decoded one row at a time. Freeze GIF on its first frame initially. These exercise indexed and continuous-tone sources without committing the first browser image release to every PiXEL format.
2. Add PNG after row-memory and transparency behavior are measured. Keep alpha handling bounded and define the background-composition rule.
3. Evaluate BMP and PCX as compatibility formats using existing PiXEL decoders; add them only if network and adapter tests show meaningful early-Web coverage. Defer TGA, PNM and PIX unless a real page corpus motivates them.
4. Keep animated GIF, interlacing edge cases, progressive JPEG presentation, and image saving out of the first static-image milestone. The download manager owns Save Image As and binary persistence.

An extension is not authoritative. The response media type may guide dispatch, but a decoder validates the signature and refuses malformed or unsupported data with a small placeholder/status, never an unbounded read. Resolve relative image URLs against the page URL and use the Browser's redirect, cancellation and navigation-generation rules. One failed image must not fail the surrounding document.

## Memory and rendering

The page response and image resources must have different owners. The page
source stays available for View Source, Save Page As and form controls; an
image response must never borrow or replace `br_srcseg`. Add one sequential
resource job with its own generation, URL, bounded compressed store and
decoder state. Serial fetches keep the existing single-socket discipline and
avoid one socket, input ring and decoder claim per `<img>`. A navigation or
close invalidates the resource generation before releasing any image claim.

Do not allocate a decoded full-image bitmap. Keep compressed bytes in a
bounded image arena subject to the same fresh free-heap reserve check as page
growth, then decode rows into one reusable packed-4bpp row. Store each image's
URL/body range, checked dimensions, native-palette mapping and document
placement in bounded records. Repaint starts the decoder from the retained
compressed range and emits only rows intersecting the viewport; slower replay
is acceptable here because it avoids a full decoded copy and gives deterministic
repaint after occlusion. If an image or decoder claim cannot fit above the
reserve, keep the page and its `alt` text, skip that image, and show a bounded
status. Never make a page fail because one image is large or malformed.

The UI task alone claims, grows and frees page/image storage. Network code
receives into a fixed staging window, parks at the existing wake boundary, and
lets the UI append into the image arena only after checking the navigation and
resource generations. Decoder code sees only validated segment ranges and
caller-owned scratch. Optional EMS/XMS may be considered only through
documented ownership and transfer APIs; conventional memory remains the
complete fallback within the page's free-heap reserve.

Use the windowed graphics API. Convert decoded rows into packed 4bpp indices in
the os8088 palette, then call `OSAPI_GFX_BLIT4` under the normal graphics lock
and Browser clip. The kernel already selects the VGA/EGA colour path or the
Hercules/CGA 1bpp path, including §39.4's deterministic grayscale reduction
and 50% dither. Do not add an adapter-specific Browser branch or duplicate
that dither. For continuous-tone sources, use a deterministic, bounded
conversion into the 16-colour palette; prefer PiXEL's tested palette/dither
method where its cost and code size fit, otherwise document and test the
smaller Browser-specific mapping. Preserve clipping and never draw outside
Browser content. Measure conversion on an 8088-speed emulator; QEMU validates
bytes and control flow, not speed.

Reserve the declared image box before painting when dimensions are known.
Unknown dimensions require a bounded format-header read before layout is
published; malformed dimensions use the alt text and a small fixed placeholder.
An image inside an anchor remains clickable across the complete image box.
Re-decode on repaint rather than retaining a full bitmap until measurements
justify a row cache. Any cache must be bounded and reconstructible, and must
not violate the document/free-heap reserve.

## Fixtures and acceptance

`tests/brcapacity.py --serve` exposes `/images.htm` and committed PiXEL samples under `/assets/`: indexed and animated GIF, PNG, JPEG, BMP and PCX. From QEMU/slirp, the index is `http://10.0.2.2:8097/`. Keep the corpus deterministic and add fixtures only for decoder features or failure boundaries.

For each supported format, test known packed-4bpp rows on VGA, EGA, CGA and
Hercules. Assert color output on VGA/EGA and the kernel's existing monochrome
reduction on CGA/Hercules; use the same known pixels across adapters so the
Browser conversion is proven adapter-independent. Also test clipping at all
content edges, scroll away/back, repaint after window occlusion, resize,
malformed/truncated data, duplicate URLs, cancellation during fetch and
low-memory refusal while another app remains launchable. Record compressed
bytes, peak conventional claim, decoder-work claim, row-buffer size and
retained cache separately. Add 8088 timing only after byte/pixel correctness
passes.

Before 1.7 implementation, close the 1.6 capacity gap in the Browser source/document/line representation and add exact decoder/graphics contracts to SPEC.md. This avoids building image offsets and fetches on a page store that cannot yet represent the requested page sizes.

## Audit log — 2026-10-17

1. Read `docs/INDEX.md`, SPEC.md §§5.4, 39.4, 39.24, 71 and 94, the
   Browser's receive/parser path, `apps/os88img.inc`, and PiXEL's decoder
   contracts in `apps/pixel/pxpart.inc`, `pxrec.inc` and `pxgif.asm`.
2. Confirmed colour and monochrome adapter selection is already automatic in
   `OSAPI_GFX_BLIT4`. Therefore the Browser output contract is native packed
   4bpp; adding an app-side Hercules/EGA probe or grayscale converter would
   duplicate the kernel and create mismatched clipping/palette behavior.
3. Confirmed `os88img.inc` is reusable code for BMP/PCX/PIX but its all-image
   destination is too expensive for the Browser's repaint path and does not
   implement the target web formats. PiXEL's mature GIF/JPEG/PNG parts cannot
   be called directly because their state, package links and seven row
   services belong to PiXEL. The next code step is a caller-owned row-decoder
   interface, reusing shared format primitives rather than copying the parts.
4. Confirmed the Browser's existing HTTP state machine writes into the page
   source claim. Inline image fetches therefore need a separate bounded arena
   and resource generation; reusing the page claim would break View Source,
   Save Page As and form controls. The design uses one sequential resource
   request, UI-owned memory growth, retained compressed ranges and row replay
   for repaint. Reserve refusal degrades only that image to `alt` text.
5. No kernel, driver, or Browser rendering behavior changed in this audit
   pass. Baseline checks passed: `brcapacity.py --check`, `brtest.py`,
   `br14.py`, `brtable.py`, and `brclick.py` in WSL/QEMU. These verify the
   current Browser remains healthy; they do not claim image rendering works.
