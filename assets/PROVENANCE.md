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
