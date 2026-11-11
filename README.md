# zoal-grass

Native X-Plane 12 plugin (C++, XPLM; macOS, Windows, Linux) that grows 3D grass on unpaved ground around the
airport you are at.

- **Grass**: each 4.5 m clump is an instanced OBJ of Blender plant clumps (crossed alpha cards). The same plants at the
  same density grow from the camera to the edge of the grass (one level of detail, about 860 triangles per clump), so
  nothing changes type or thins out as you get closer or zoom (X-Plane scales level-of-detail distances with the
  zoom). `real_blades = true` (Settings > Rendering) adds 3D blades coloured from the DINKIssTyle blade strip
  within `lod_near_m` (40 m; about 20x the triangles there) and a sparser level beyond `lod_mid_m`. No flowers, no
  `.for` forest.
- **Seen from above**: each lawn clump has a flat ground-cover card under it, the same clump rendered from above, so
  the field stays full of grass from high and overhead views where upright cards are edge-on (2 triangles each).
- **Plants** (modelled in Blender, see [Art](#art)): lawn clumps in four colours (side and top views), meadow grass with feathery panicles,
  timothy and wild awned seed-head grass, green and dry bunch-grass tussocks, moss cushions, sedge, clover and
  dandelion / plantain rosettes. They grow in drifts, and every clump variant leans to one lawn colour, so fields look patchy, not tiled.
- **Biomes from the GPS position**: a Koppen-Geiger climate map (Beck et al. 2018, `licenses/Beck-KG-NOTICE.txt`)
  gives the climate at the aircraft; it picks the plant mix and how the seasons look (table below).
- **Placement**: ported from XPGrass 1.0.4 (Tor Jakobsen, MIT, `licenses/XPGrass-LICENSE.txt`): apt.dat
  runways/pavements/boundaries plus scenery pavement, roads and occupied footprints are rasterised into a mask.
  The reader walks enabled scenery packs in priority order, applies rectangular/polygonal exclusions and airport-ID
  filters, and stops at the first base mesh for each tile. It reads uncompressed and 7z DSFs, including global scenery.
  Native road networks use `.net` widths (including asymmetric and virtual road types); curved roads are flattened.
  `.agb`/`.ags` autogen areas and `.fac` facades are excluded; `.obj` vertices and `.agp` tile/annotation bounds provide
  conservative object keep-outs. Clumps are placed on a grid, probed onto the terrain and created as `XPLMInstance`s
  (auto-shift on X-Plane 12.1+), within the existing per-frame placement budget.
- **Full-footprint clearance**: every generated grass vertex, across all variants and LODs, contributes to a bounding
  radius. Placement, edge snapping and retries check every mask cell in a square enclosing that radius. Narrow
  pavement edges are rasterised even when they fall between cell centres. `pavement_margin_m` is now extra clearance
  beyond the grass footprint, so existing settings leave a wider verge. Autogen areas are excluded conservatively,
  including gardens; object bounds are independent of heading and may leave larger gaps than the visible footprint.
  This controls placement, not rendering layers: grass remains ordinary depth-tested geometry on the terrain.
- **Streaming ortho (XEarthLayer, AutoOrtho, ...)**: all file work runs on a background thread; scenery packs on a
  FUSE / FSKit / WinFsp / Dokan / network mount are never read; terrain probe misses while tiles stream in are
  retried, and `SCENERY_LOADED` only re-tries empty cells instead of removing the grass. Mask coverage cannot include
  scenery available only on skipped mounts. An unreadable/corrupt local DSF withholds grass on that tile and logs the
  reason. Unresolved object art uses a logged 10 m radius; unknown roads use a logged 20 m corridor. These fallbacks
  cannot guarantee clearance for larger custom assets. Road/object bounds include all matching library variants
  conservatively; pavement classification still uses the first resolved variant. Animated object transforms are not
  evaluated. Reload the plugin after changing scenery packs.
- **OpenStreetMap roads** (`use_osm_roads = true`, Settings > Clearance): roads, car parks and
  rail lines from OpenStreetMap around each airport are added to the pavement mask, so grass stays off roads the
  scenery does not describe (ortho, base mesh). Roads take their `width` / `lanes` tags or a width for their class;
  footways count unless tagged unpaved, tracks and paths only when tagged paved; tunnels are ignored. The data is
  downloaded once per airport from an Overpass API server (`osm_overpass_url`, default overpass-api.de; only the
  airport's bounding box is sent) on its own thread and kept in `zoal-grass/osm_cache/` for `osm_cache_days` (30).
  The first visit places the grass at once without them and rebuilds that airport's grass when they arrive (public
  Overpass servers can take a minute); without a network the grass is simply placed without them. Untick it in
  Settings > Clearance or set `use_osm_roads = false` to switch it off. Map data (c) OpenStreetMap contributors, ODbL.

## Seasons and styles

Settings > Look > Season (or `style =` in `zoal-grass.ini`):

| style | look | dry blades | height |
|---|---|---|---|
| `spring` | fresh green | 3 % | 0.9x |
| `summer` | DINKIssTyle colours, calmed | 12 % | 1.0x |
| `late_summer` | warm, drying | 45 % | 1.05x |
| `autumn` | olive / brown | 55 % | 0.95x |
| `winter` | dormant tan | 80 % | 0.7x |
| `custom` | summer colours | `dry_mix` / Settings > Look > Dry grass | 1.0x |
| `auto` (default) | season from the **sim's** date (`sim/time/local_date_days`) and the aircraft latitude (southern hemisphere shifted, tropics summer, winters below 40° autumn); `sim/weather/region/snow_cover` ≥ 15 % forces winter, ≥ 60 % hides the grass. Re-checked every 5 s. |

## Plants / biome

Settings > Look > Plants (or `biome =` in `zoal-grass.ini`). `auto` (default) looks up the Koppen
climate at the aircraft position (a 0.25° grid; airports in a coastal water cell take the nearest land). A new climate
is taken up while no airport has grass (in flight), so an airport on a climate border keeps one look:

| biome | Koppen | plants | seasons (`style = auto`) |
|---|---|---|---|
| `temperate` | Cf, Cw | lawn, meadow panicles, clover, rosettes | by date and latitude, as above |
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
  fonts/                   Inter (the settings window's typeface)
  licenses/
```

## Settings window

**Plugins > zoal-grass > Settings…** (or the `zoal-grass/settings` command) opens a window with every setting,
in five tabs:

| tab | what |
|---|---|
| Look | season and plants (auto or picked), dry grass (custom season), height, density, width |
| Placement | radius, clump spacing, grass outside the boundary, steepest slope; grass on dirt / gravel / turf, pavement clearance |
| Clearance | scenery roads and buildings, the always / never pavement word lists; OpenStreetMap roads, the Overpass server, how long downloads are kept |
| Rendering | real 3D blades and their distances, GPU mode, shadows, normal map, how far beyond the radius grass is removed |
| Advanced | theme (Graphite, Daylight, Night), the airport area margins, mask resolution, status logging, reset to defaults |

The header shows what is growing (season, plants, Koppen climate, clumps) and the on / off switch. Changes apply as
you make them (a slider when you let go); **Save** writes `zoal-grass/zoal-grass.ini`, **Reload** reads it back.
It is drawn with Dear ImGui by X-Plane's panel graphics, in zoal-atc's look (Inter, the same palettes and controls),
and needs **X-Plane 12.4.4 or newer**; on an older X-Plane the plugin still loads and keeps the full menu.

Settings also live in `zoal-grass/zoal-grass.ini` (written on first start). Commands: `zoal-grass/settings`,
`zoal-grass/toggle`, `zoal-grass/reload`. Log lines start with `zoal-grass:` in `Log.txt`. Generated objects go to
`zoal-grass/cache/` and are rebuilt when look settings change.

## Build

```sh
scripts/ensure-deps.sh                     # X-Plane SDK 410, Dear ImGui, FreeType, Inter into .cache (pinned, checked)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release       # -> build/<platform>/zoal-grass.xpl
ctest --test-dir build -C Release          # X-Plane-free core tests
cmake --build build --target install_xplane -- # macOS dev loop: -DXPLANE_ROOT=... (default /Users/dzou/X-Plane 12)
```

The settings window can be checked without X-Plane: configure with `-DZOAL_GRASS_BUILD_PREVIEW=ON`, then
`build/settings_preview .cache/inter-v4.1 out.ppm <tab 0-4> <graphite|daylight|night> 780 680 1.5` renders it
(software rasteriser from zoal-atc's `tools/panel_preview.cpp`). The window code (`src/gui/`) comes from zoal-atc:
`imgui_window` (the X-Plane host; its SDK 4.4 calls are looked up at run time), `kit` and the palettes and controls
in `settings_view`.

The public-domain 7-Zip C decoder is vendored at a pinned revision in `third_party/7zip/` (see
`licenses/7zip-NOTICE.txt`); no external 7z executable is required. Core tests include synthetic scenery stacks,
compressed DSFs, road curves, exclusions, sub-cell pavement and generated mesh footprint checks.

CI (`.github/workflows/ci.yml`) builds and tests on macOS (universal), Windows (MSVC, static runtime) and Linux.
Pushing a `v*` tag runs `release.yml`, which packages all three into `zoal-grass-<version>.zip` and publishes it.

## Art

`resources/` holds the baked art only: one 2048 x 1024 atlas per season, `grass_art.txt` (blade colour strip, plant
cards) and `climate_kg.bin`. The blade colour strip is derived from DINKIssTyle 3D Grass (`licenses/DINKIssTyle-NOTICE.txt`); the plant cards are modelled in Blender by `tools/blender_species.py`
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
