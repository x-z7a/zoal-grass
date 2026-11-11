# zoal-grass

Native X-Plane 12 plugin (C++, XPLM; macOS, Windows, Linux) that grows 3D grass on unpaved ground around the
airport you are at.

- **Grass**: each 4.5 m clump is an instanced OBJ with four levels of detail. Up to 16 m (40 % of `lod_near_m`):
  real 3D blades (tapered, leaning, drooping; in tufts of 5-9) coloured from the DINKIssTyle blade strip. To
  `lod_near_m` (40 m): half the tufts as straight single-triangle blades, joined by lawn clumps. To `lod_mid_m`
  (140 m): Blender lawn clumps and plant cards only. Beyond: fewer, larger cards. No `.for` forest is placed in the sim.
- **Plants** (modelled in Blender, see [Art](#art)): lawn clumps in four colours, meadow grass with feathery panicles,
  timothy and wild awned seed-head grass, green and dry bunch-grass tussocks, moss cushions, sedge, dandelions with
  seed clocks, clover (with and without flowers) and dandelion / plantain rosettes, plus DINKIssTyle's 3D flower
  plants. They grow in drifts, and every clump variant leans to one lawn colour, so fields look patchy, not tiled.
- **Biomes from the GPS position**: a Koppen-Geiger climate map (Beck et al. 2018, `licenses/Beck-KG-NOTICE.txt`)
  gives the climate at the aircraft; it picks the plant mix and how the seasons look (table below).
- **Placement**: ported from XPGrass 1.0.4 (Tor Jakobsen, MIT, `licenses/XPGrass-LICENSE.txt`): apt.dat
  runways/pavements/boundaries plus the airport pack's draped `.pol` polygons and `.lin` roads/paths are rasterised
  into a pavement mask; clumps are placed on a grid around the aircraft, probed onto the terrain and created as
  `XPLMInstance`s (auto-shift on X-Plane 12.1+), within a 1.5 ms per-frame budget.
- **Streaming ortho (XEarthLayer, AutoOrtho, ...)**: all file work runs on a background thread; scenery packs on a
  FUSE / FSKit / WinFsp / Dokan / network mount are never read; terrain probe misses while tiles stream in are
  retried, and `SCENERY_LOADED` only re-tries empty cells instead of removing the grass.

## Seasons and styles

Plugins > zoal-grass > Season / style (or `style =` in `zoal-grass.ini`):

| style | look | dry blades | height | flowers |
|---|---|---|---|---|
| `spring` | fresh green | 3 % | 0.9x | many, all four kinds, in patches |
| `summer` | DINKIssTyle colours, calmed | 12 % | 1.0x | some (daisies) |
| `late_summer` | warm, drying | 45 % | 1.05x | few yellow |
| `autumn` | olive / brown | 55 % | 0.95x | rare |
| `winter` | dormant tan | 80 % | 0.7x | none |
| `custom` | summer colours | `dry_mix` / Custom grass colour menu | 1.0x | some |
| `auto` (default) | season from the **sim's** date (`sim/time/local_date_days`) and the aircraft latitude (southern hemisphere shifted, tropics summer, winters below 40° autumn); `sim/weather/region/snow_cover` ≥ 15 % forces winter, ≥ 60 % hides the grass. Re-checked every 5 s. |

`flowers =` (or the Flowers menu) scales the flower density.

## Plants / biome

Plugins > zoal-grass > Plants / biome (or `biome =` in `zoal-grass.ini`). `auto` (default) looks up the Koppen
climate at the aircraft position (a 0.25° grid; airports in a coastal water cell take the nearest land). A new climate
is taken up while no airport has grass (in flight), so an airport on a climate border keeps one look:

| biome | Koppen | plants | seasons (`style = auto`) |
|---|---|---|---|
| `temperate` | Cf, Cw | lawn, meadow panicles, clover, dandelions, rosettes | by date and latitude, as above |
| `continental` | D | as temperate, more moss and sedge | by date and latitude |
| `mediterranean` | Cs | golden seed grass, tussocks, some clover | green Dec-Apr, golden Jun-Oct |
| `steppe` | BS | bunch-grass tussocks, dry seed grass, sparse lawn | short green spring, then dry |
| `desert` | BW | sparse dry tussocks on bare ground | always dry (cold deserts: winter) |
| `tropical` | Af, Am | tall lush lawn, sedge | always summer |
| `savanna` | Aw | tall seed grass, tussocks | green wet season, straw dry season |
| `tundra` | ET, EF | moss cushions, sedge, short grass | short summer, long winter |

Choosing a biome by hand keeps its plants anywhere; with `style = auto` its seasons then follow a typical climate of
that biome.

## Install

Unzip a release and drop the `zoal-grass` folder into `<X-Plane 12>/Resources/plugins/`:

```
zoal-grass/
  mac_x64/zoal-grass.xpl   win_x64/zoal-grass.xpl   lin_x64/zoal-grass.xpl
  resources/               grass_art.txt, climate_kg.bin, textures/zg_atlas_<style>.png
  licenses/
```

Settings: Plugins > zoal-grass, or `zoal-grass/zoal-grass.ini` (written on first start). Commands:
`zoal-grass/toggle`, `zoal-grass/reload`. Log lines start with `zoal-grass:` in `Log.txt`. Generated objects go to
`zoal-grass/cache/` and are rebuilt when look settings change.

## Build

```sh
scripts/ensure-deps.sh                     # X-Plane SDK 410 into .cache/sdk (ZOAL_GRASS_SDK_ROOT reuses a local one)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release       # -> build/<platform>/zoal-grass.xpl
ctest --test-dir build -C Release          # X-Plane-free core tests
cmake --build build --target install_xplane -- # macOS dev loop: -DXPLANE_ROOT=... (default /Users/dzou/X-Plane 12)
```

CI (`.github/workflows/ci.yml`) builds and tests on macOS (universal), Windows (MSVC, static runtime) and Linux.
Pushing a `v*` tag runs `release.yml`, which packages all three into `zoal-grass-<version>.zip` and publishes it.

## Art

`resources/` holds the baked art only: one 2048 x 1024 atlas per season, `grass_art.txt` (blade colour strip, flower
plants, plant cards) and `climate_kg.bin`. The blade colour strip and the flower plants are derived from DINKIssTyle
3D Grass (`licenses/DINKIssTyle-NOTICE.txt`); the plant cards are modelled in Blender by `tools/blender_species.py`
(every plant is real geometry: tapered curved blades, spikelet seed heads, panicles, clover leaflets, toothed rosette
leaves, moss domes, lit by a soft sky and a high sun so clumps get dark interiors and bright tips) and rendered into
alpha sprites. To rebuild everything:

```sh
python3 tools/bake_art.py <path>/DINKIssTyle_LIB_Grass resources          # original art: 1024 x 512 atlases
blender -b --factory-startup -P tools/blender_species.py -- /tmp/zg_species   # Blender 4.2+ / 5: plant sprites
python3 tools/compose_atlas.py /tmp/zg_species resources                  # pack, grade per season (Pillow, numpy)
python3 tools/bake_climate.py <path>/Beck_KG_V1_present_0p083.tif resources   # climate grid (Pillow)
```

To add a plant: write a builder in `blender_species.py`, add it to `SPECIES` (name, side/top, metres, pixels), re-run
the last three steps and give it a kind in `GrassAssets::load` and a density in `species_mix` (`src/biome.cpp`).
`compose_atlas.py` can be re-run on its own output.

To look at the generated clumps without X-Plane (Blender 4.3+ / 5):

```sh
cmake -S . -B build -DZOAL_GRASS_BUILD_TOOLS=ON && cmake --build build
build/offline_check "<X-Plane 12>" KBTV /tmp/zg resources     # index, DSF polygons, mask, objects per style
blender -b --factory-startup -P tools/preview_blender.py -- /tmp/zg/objs_summer /tmp/zg/summer 4.5 41
```
