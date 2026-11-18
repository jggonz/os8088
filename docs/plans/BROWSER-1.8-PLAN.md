# Browser 1.8 - early-web compatibility

Branch `browser-1.8`, cut from `browser-1.7` while PR #358 is in review. The
roadmap line (BROWSER-ROADMAP.md): compatibility and integration against
early-web pages; refine progressive rendering and interactions.

## The survey that ordered it

Eleven front pages of 1996-1999, mirrored from the Wayback Machine (raw
`id_` captures, every picture fetched from the same capture), served to the
Browser on QEMU and looked at:

Pictures laid out and fetched, per page (the page's `<img>` count first;
the image table holds 32 a page):

| page | `<img>` | 1.7 | 1.8 step 1 |
|---|---:|---:|---:|
| Netscape 1997 | 18 | 3 | 18 |
| Apple 1997 | 55 | 0 | 32 |
| Microsoft 1996 | 22 | 0 | 22 (13 of them archive error pages, not pictures) |
| CNN 2000 | 63 | 1 | 31 |
| eBay 1999 | 16 | 0 | 16 |
| GeoCities 1999 | 17 | 3 | 17 |
| AltaVista 1998 | 11 | 5 | 11 |
| Yahoo 1996, Google 1998, NASA 1997, Space Jam 1996 | 0-2 | all | all |

JavaScript: six of the eleven carry none, but these are corporate front
pages, the cleanest kind. Entertainment and fan sites (Nintendo, Bored.com,
Mad) used it on every page. The owner's decision: JavaScript is 1.8b, after
this plan, starting with a wider survey (those sites, several years each,
inner pages too) that records what each page uses and whether it works,
degrades or breaks without it.

## Steps, in the owner's order

1. **Pictures in table cells**, and column widths that hold them -
   SPEC.md 71.33. Done: about 170 pictures where 1.7 fetched 12.
2. **Colours and backgrounds**: `bgcolor`, `<font color>`, `text`/`link`
   colours, cell backgrounds, on VGA and EGA; Hercules and CGA stay black
   on white. Options > Colors. Done: SPEC.md 71.34.
3. **Fonts and sizes**: `<h1>`-`<h6>`, `<font size>`, `<big>`/`<small>`
   and `<font face>`, body text staying on the 8x8 cell font. Options >
   Fonts, off by default on an 8088. The faces are the SYSTEM's, not
   PageDesk's strikes: `SYSTEM/FONTS` already ships Helvetica (Arimo), Times
   (Tinos) and Courier (Cousine), the Liberation family's metric twins, and
   `apps/os88type.inc` sets them - nothing ships a second copy.
   **3a done** (SPEC.md 71.35): `<h1>`-`<h3>` two rows tall, h1 the cell
   doubled and h2/h3 in Times, from part 8 fetched by the first such heading
   and never at launch. **3b done**: `<big>`, `<small>` and `<font size>`,
   big letters in Times among 8x8 ones. `<font face>` is left: it could only
   change big letters and headings, and costs part 8 two more faces.
4. **Text beside a picture**: `align=left/right` floats, `hspace`, `vspace`,
   `<br clear>`.
5. **Progressive pictures and pictures past 60,000 bytes**, and the image
   table's 32 entries a page.
6. **A bare `&` in a `<select>`'s options** eats text up to the next option.
7. **The Options menu**, saved in `SYSTEM/APPDATA/BROWSER.CFG`: Images,
   Images on click, Animations, Colors, Fonts, Tables, Underline links,
   Cookies, Cache, and Set Home Page. No password store.

Open: the image table's 32 entries a page (step 1's note).
