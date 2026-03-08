# Terrain Generation & WFC — Design Notes

## What changed

### Smoother terrain (noise)

The old `terrain()` was a single call to `stb_perlin_fbm_noise3` at frequency
0.01, lacunarity 2.0, gain 0.5, 6 octaves.  That produces hills that are
roughly 100-voxel wavelength — still fairly bumpy at the CHUNK_SIZE=128 scale.

**New approach — two-layer blend:**

| Layer       | Freq   | Octaves | Weight | Purpose                        |
|-------------|--------|---------|--------|--------------------------------|
| Continental | 0.002  | 4       | 70 %   | Broad, rolling terrain shape   |
| Detail      | 0.008  | 5       | 30 %   | Local bumps & texture          |

The continental layer gives sweeping hills/valleys visible across many chunks.
The detail layer prevents the terrain from looking too flat up close.  The
combined signal is still in roughly [-1, 1] so the existing `n*0.5+0.5` remap
works.

Gain was lowered slightly on the continental pass (0.45) to suppress high-freq
energy and keep hills smooth.

### Improved WFC biome assignment

Previous implementation ran WFC **per-voxel-column** — that's 128×128 = 16 K
collapse steps per chunk, and biome boundaries were one voxel wide, causing
harsh height jumps between biomes.

**New approach — coarse-grid WFC + bilinear interpolation:**

1. A coarse grid of `COARSE_DIM × COARSE_DIM` tiles (every 8 voxels) is
   collapsed with adjacency constraints.  This is only ~17×17 = 289 cells →
   very fast.
2. Each coarse cell is collapsed using **three** neighbours (left, up,
   diagonal) instead of two, which reduces visual stripe artefacts.
3. Each voxel column reads the four surrounding coarse cells and **bilinearly
   interpolates** the height bias using a Hermite smoothstep.  This yields
   gentle, blended slopes at biome boundaries instead of cliff walls.
4. The nearest coarse cell determines the surface block type, so block
   textures still change at biome borders — only the height is smoothed.

### More biome types

| Biome     | Surface       | Height bias | Notes                 |
|-----------|---------------|-------------|-----------------------|
| Grassland | Grass / Dirt  | +4          | Default, most common  |
| Forest    | Grass         | +8          | Slightly elevated     |
| Rocky     | Stone / Gravel| +16         | Foothills             |
| Mountain  | Stone         | +30         | Peaks                 |
| Sandy     | Sand          | -2          | Low coastal-like      |
| Desert    | Sand          | -4          | Flat dunes            |
| Clayey    | Clay / Dirt   | +2          | Subtle variation      |
| Snow      | Gravel        | +22         | High-altitude         |

Adjacency rules form a natural gradient:
`Desert ↔ Sandy ↔ Grassland ↔ Forest ↔ Snow ↔ Mountain ↔ Rocky ↔ Grassland`.
This means you'll never get desert directly touching snow, for example.

## Thoughts & future ideas

- **Chunk-edge continuity**: the coarse WFC is seeded from world-space coords
  (`hash2i(gx, gz, …)`), so the same cell always collapses to the same biome
  regardless of which chunk generates it.  However, the *constraint propagation*
  only sees cells inside the current chunk.  A proper fix would be to generate a
  1-cell border from the adjacent chunk first (cheap, since coarse grid is
  small).  For now it works well enough because the coarse grid is only 17×17
  and the hash provides spatial coherence.

- **True WFC with backtracking**: the current approach is "greedy WFC" — it
  never backtracks. True WFC picks the cell with lowest entropy first and
  backtracks on contradiction.  For terrain this is overkill; greedy + good
  adjacency tables already produce natural-looking regions.

- **Biome blending quality**: I'm doing bilinear interpolation of the *height
  bias*, not the block type.  If you wanted to blend surface types too (e.g.
  mix sand and grass blocks at borders), you'd need a weighted random pick
  based on `tx`/`tz`.

- **Performance**: generating 25 chunks (5×5) on every chunk-boundary crossing
  is CPU-heavy.  Next step would be caching chunks in a ring buffer keyed by
  `(chunk_x, chunk_z)` and only regenerating the newly-visible edge.

- **Vertical biomes**: currently biome assignment is 2D (XZ plane only); adding
  a Y-dependent rule (e.g. caves, underground biomes) would make the world
  much more interesting.
