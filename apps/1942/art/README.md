# Original 1942 artwork

`aircraft.png` and `terrain.png` are original raster masters created with the
built-in imagegen tool for this project. They contain no NES cartridge data.
The game uses the committed `sprites.json` and `sea.idx`, `reef.idx`,
`port.idx` production assets. Each `.idx` file is 256×240 row-major palette
indices. `../palette.json` defines the common 64-color RGB palette, the
CGA conversion map, and three hardware register profiles.

Normal builds use `tools/1942assets.py` (Python standard library only).
To revise the masters, run `python3 tools/1942art.py` with Pillow installed.
That offline import resizes the masters, builds the shared palette and writes
the production assets; review those changes together. It also rebuilds the
original 5×7 stencil alphabet and the small projectile/wake sprites. Pillow
and imagegen are authoring tools, not build dependencies.

The aircraft atlas has a four-by-four layout: player, left bank, right bank,
roll; green fighter, orange fighter, blue fighter, bomber; four explosions;
carrier, battleship, medal, island. Runtime aircraft are 24×24, the bomber
40×40, explosions 24×24, and the pickup 12×12. The ship and island records are
available in the bank for later formations. The terrain master is a triptych:
ocean, reef and harbor. Terrain remains cached during combat; small wakes
move independently and stage families change every four stages.

Generation prompts (built-in imagegen, no CLI/API fallback):

- Aircraft: production pixel art sprite atlas for an original top-down WWII
  Pacific vertical shooter at 256×240 logical resolution, 1990 arcade quality.
  Four-by-four equal cells, isolated sprites, no grid, text, labels or border.
  Orthographic top-down pixel art with metal panel shading, navy outlines and
  ivory highlights. First row: silver twin-boom P-38 facing up, banking left,
  banking right and narrow barrel roll. Second row: olive, orange/cream and
  navy fighters facing down, then a large twin-engine olive bomber. Third row:
  four successive fiery explosion stages. Fourth row: top-down carrier,
  battleship, gold medal powerup, tropical rocky island. Readable silhouettes
  when reduced to 24×24 or 32×32. Original game art, not a screenshot or
  copied sprites. The tool supplied transparency for the isolation background.
- Terrain: original landscape atlas with three equal vertical panels for a
  256×240 top-down Pacific aerial shooter. Ocean with sandy jungle islands;
  coral atoll with turquoise shallows and limestone beaches; naval harbor
  with concrete piers, warehouses, cranes, fuel tanks and tiny moored ships.
  Land clustered at the left/right edges, broad clear deep-water corridor
  through the center. Directly overhead, no perspective, horizon, text,
  aircraft or UI. Rich arcade navy, teal, jade, sand and ochre palette, crisp
  shore foam and readable detail at target resolution. Ocean at the top and
  bottom edges. Original scenery, not an existing game screenshot.
