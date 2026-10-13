# Asset provenance

One entry per asset that ships in `game/`. No entry means no shipping.

All four slice assets were made on 2026-10-13 in look **A, toy diorama** (#43).
The pipeline was the same for each, in two Meshy steps:

1. **Reference image:** text-to-image with `nano-banana-pro` at 1:1 (9
   credits). The image is kept in `assets/source/` (re-encoded as JPEG to keep the repo small).
2. **Model:** image-to-3d from that image's task (`input_task_id`), `meshy-6`,
   textured, remeshed to a target of 8,000 triangles, origin at the bottom,
   GLB only (30 credits).

Every prompt began with the same style lead, so the four share a look:

> Stylized low-poly toy diorama game asset, chunky simple shapes, flat
> shading, saturated colours, like a painted board-game miniature.

Each prompt ended with "Single object centered, … plain white background, no
base, no ground, no text."

| Asset | Shipping file | Prompt after the style lead | Text-to-image task | Image-to-3d task | Triangles |
|---|---|---|---|---|---|
| Engineer | `game/assets/models/engineer.glb` | A sturdy coastal engineer figure standing upright: bright orange raincoat, yellow hard hat, dark rubber boots, holding a long wooden push-pole upright beside the body. Compact, slightly oversized head and hands. | `01a13479-28a9-7046-a251-651881d67ee0` | `01a1347a-1e0f-70ea-895e-e6c544d3781e` | 7,175 |
| Crawler | `game/assets/models/crawler.glb` | A squat sea crawler creature: crab-like body with a rounded teal-green barnacled shell, six short stubby legs, two big orange-tipped claws raised, small glowing yellow eyes, wide and low. Cute but menacing. | `01a13479-2916-75ff-9c41-6e55b60273f1` | `01a1347a-2073-7532-86b7-4bdcd440b6b2` | 7,752 |
| Cottage (building) | `game/assets/models/cottage.glb` | A small fisherman's cottage on a square footprint: white plastered walls, steep terracotta tiled roof, stone chimney, blue wooden door, two small windows, a coil of rope by the door. | `01a13479-297c-713f-825b-f49517004344` | `01a1347a-22c5-7316-982f-890a39dafc47` | 7,405 |
| Sea wall (prop) | `game/assets/models/seawall.glb` | A short straight segment of a harbour sea wall: chunky grey stone blocks, a weathered wooden post at each end, a few patches of green seaweed at the bottom, rectangular footprint about twice as long as deep. | `01a13479-29e0-753a-aef5-9a46913a8438` | `01a1347a-250b-766b-a61f-b494c234c141` | 7,748 |

Credits spent: 4 × 9 + 4 × 30 = **156**, against the quote of about 180-210
in #43.

## Edits

- **Geometry:** none. The GLBs ship as Meshy produced them, with their
  base-colour textures embedded. The separate texture PNGs the download also
  wrote were duplicates and were discarded. Godot's importer extracts the
  embedded textures as `*_0.jpg`.
- **Normalised at load, not in the files:** `game/scripts/model_fit.gd` scales
  each model to its footprint and height and puts its origin at the base
  centre.

  | Asset | Footprint × height (tiles) |
  |---|---|
  | Engineer | 0.7 × 1.0 |
  | Crawler | 0.95 × 0.5 |
  | Cottage | 0.92 × 1.0 |
  | Sea wall | 1.8 × 1.0 |
- **Added in the scene, not to the models:**
  - A coloured ring under each engineer, which tells the three apart.
  - A dark red disc under each crawler, for enemy contrast.
  - A dark slab with a ✕ for a razed cottage.

## Licence and approval

- **Licence:** generated with Meshy on the owner's paid plan, whose terms
  allow commercial use of generated assets. The owner confirmed the plan on
  2026-10-13 and cleared the repository to go public with these assets. They
  are distributed under the repository's BSD 3-Clause licence (`LICENSE`).
- **Owner approval:** the look (A) was chosen by Claude on the owner's
  instruction (#43). The owner reviewed the integrated captures on 2026-10-13
  ("the assets look okay") and asked for the health display to be cleaned up,
  which #54 did.

## Terrain textures (2026-10-13, #67, #68)

Four seamless textures applied by `game/shaders/terrain.gdshader`: the band's
ground texture on tile tops (sand at elevation 0-2, grass 3-6, stony ground
7-8) and the cliff texture on the sides, all in world space.

Two generators were compared on the same prompts, as the owner suggested:
Meshy `nano-banana-pro` and `venice image` with `flux-2-pro`. Meshy's set
shipped: it is chunkier and painterly, matching look A, while Venice's is finer
and closer to photographic, and came back at 4:3. The comparison and Venice's
four are kept in `assets/source/terrain/`.

Every prompt began: "Seamless tileable texture, top-down flat orthographic view
(for the cliff: flat front view of a vertical cliff face), stylized toy diorama
like a painted board-game miniature:" and ended: "Even flat lighting, no
shadows, no border, fills the whole frame."

| Texture | Shipping file | Prompt middle | Meshy text-to-image task |
|---|---|---|---|
| Sand | `game/assets/textures/terrain_sand.jpg` | pale warm beach sand with gentle wind ripples and a few tiny shell flecks | `01a1363a-e5ab-7552-85bd-2398f16fe51d` |
| Grass | `game/assets/textures/terrain_grass.jpg` | short bright green grass with chunky painted strokes and a few tiny clover leaves | `01a1363a-e61f-74b5-94f0-29e17753ad0c` |
| Stony ground | `game/assets/textures/terrain_rock.jpg` | grey-brown stony upland ground, small flat stones packed together with thin moss in the cracks | `01a1363a-e684-71db-829f-8533030f411b` |
| Cliff face | `game/assets/textures/terrain_cliff.jpg` | horizontal layered rock strata in warm greys and sandy browns | `01a1363a-e6e3-7694-943f-6e816fc6399a` |

**Edits:** re-encoded from PNG to JPEG at quality 90, 1024². In the game the
grass band is tinted darker and less saturated (0.72, 0.8, 0.66), so units and
highlights stay the brightest things on the board, and every tile's brightness
rises with elevation.

**Cost:** 4 × 9 = 36 Meshy credits, plus 4 Venice images at $0.03.

**Licence:** generated on the owner's paid plans; shipped under the
repository's BSD 3-Clause licence.

## Terrain toppers (2026-10-13, #69)

Three small decorative props, made like the slice assets: a `nano-banana-pro`
reference image with the look-A style lead, then image-to-3d with the cheaper
smart-topology model (`meshy-t2`, textured, GLB), at 9 + 15 credits each.

| Prop | Band | Shipping file | Prompt after the style lead | Text-to-image task | Image-to-3d task |
|---|---|---|---|---|---|
| Grass tuft | grass | `game/assets/models/tuft.glb` | A small clump of tall green grass blades with two tiny yellow wildflowers, short and round, wider than tall. | `01a13651-0a7c-7181-b6fc-ba70815d4c15` | `01a13651-daed-72aa-a7d1-5849aa80ffe8` |
| Stones | stony ground | `game/assets/models/stones.glb` | A low cluster of three small rounded grey-brown stones with a patch of green moss, wider than tall. | `01a13651-0ae3-718f-b4f2-5d1ef2c553a7` | `01a13651-de71-7523-b089-afc75b51cd25` |
| Driftwood and shells | sand | `game/assets/models/driftwood.glb` | A short piece of pale bleached driftwood lying flat with two pink-and-cream seashells beside it, low and flat. | `01a13651-0b3f-77a1-8308-c2f19532f741` | `01a13651-e114-72a7-947b-a5af145f00f2` |

**Edits:** none to the files. In the game each prop is fitted to 0.42 × 0.26
tiles. It is placed on about 40% of tiles by a presentation-only hash of the
tile and the seed, never on a cottage's tile. It hides while anything stands on
its tile, and once the tile floods.

**Cost:** 3 × 24 = 72 credits. **Licence:** as above.
