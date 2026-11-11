# zoal-grass

Native X-Plane 12 plugin (C++, XPLM; macOS, Windows, Linux) that grows 3D grass on unpaved ground around the
airport you are at.

- **Grass**: each 4.5 m clump is an instanced OBJ with three levels of detail, all made of real 3D blades (tapered,
  leaning, drooping; in tufts of 5-9) coloured from the DINKIssTyle blade strip, plus DINKIssTyle's 3D flower plants:
  near (`lod_near_m`, 40 m) every tuft; mid (`lod_mid_m`, 140 m) half of them with wider single-triangle blades;
  far a sparse set, wider still. No `.for` forest is placed in the sim.
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

## Install

Unzip a release and drop the `zoal-grass` folder into `<X-Plane 12>/Resources/plugins/`:

```
zoal-grass/
  mac_x64/zoal-grass.xpl   win_x64/zoal-grass.xpl   lin_x64/zoal-grass.xpl
  resources/               grass_art.txt, textures/zg_atlas_<style>.png
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

`resources/` holds the baked art only: one atlas per season and `grass_art.txt` (sprite/strip rectangles, blade
colour strip, flower plants), derived from DINKIssTyle 3D Grass (`licenses/DINKIssTyle-NOTICE.txt`). To re-bake from
the original download:

```sh
python3 tools/bake_art.py <path>/DINKIssTyle_LIB_Grass resources
```

To look at the generated clumps without X-Plane (Blender 4.3+ / 5):

```sh
cmake -S . -B build -DZOAL_GRASS_BUILD_TOOLS=ON && cmake --build build
build/offline_check "<X-Plane 12>" KBTV /tmp/zg resources     # index, DSF polygons, mask, objects per style
blender -b --factory-startup -P tools/preview_blender.py -- /tmp/zg/objs_summer /tmp/zg/summer 4.5 41
```
