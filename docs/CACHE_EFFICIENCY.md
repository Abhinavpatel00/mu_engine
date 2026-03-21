# Cache Efficiency Guide for the Renderer

This document collects practical ways to make the renderer more cache efficient.
It focuses on both CPU cache behavior and GPU-friendly memory access patterns.
The goal is to reduce stalls, improve locality, and make frame-time more predictable.

## What cache efficiency means

Cache efficiency is about keeping frequently used data close together and accessed in predictable order.
Good cache behavior usually means:

- fewer random memory accesses
- fewer pointer chases
- better sequential reads and writes
- less copying of large structures
- fewer temporary allocations
- fewer expensive synchronization points

In this renderer, cache efficiency matters a lot because:

- resources are already suballocated from large GPU buffers
- push constants carry offsets instead of binding many small buffers
- clustered shading and voxel rendering can touch a lot of per-frame data
- barrier and upload paths can become expensive if they are too fragmented

## Renderer-specific principles

### 1. Prefer contiguous storage

Store related objects in arrays or packed tables instead of linked lists or many small heap allocations.
This helps both CPU prefetchers and GPU access patterns.

Good examples:

- one array of visible render items
- one array of light records
- one array of cluster records
- one packed array of material data
- one packed array of instance transforms

Avoid:

- per-object malloc/free
- pointer-heavy trees for hot frame data
- small scattered structs that are read every frame

### 2. Use structure of arrays for hot loops

If a system repeatedly reads only a few fields, split them into separate arrays.

For example, instead of storing:

- transform
- material id
- bounds
- visibility flags
- mesh offset

in one large object, keep the frequently accessed fields tightly packed.

This reduces the amount of data loaded per iteration and keeps working sets small.

### 3. Keep hot data smaller than the cache working set

Hot frame data should fit comfortably in L1 or L2 cache where possible.
If a system touches too much data per frame, it will start thrashing the cache.

Typical hot data includes:

- visible object lists
- per-frame constants
- cluster light lists
- command or draw metadata
- transient upload records

If a structure is only used during setup or debug, move it out of the hot path.

### 4. Avoid false sharing

If multiple threads write to nearby memory, they can bounce the same cache line between cores.
To reduce this:

- give each worker thread its own scratch space
- separate per-thread counters
- align write-heavy structures to cache line boundaries when needed
- merge thread results after the parallel phase

### 5. Write sequentially, read sequentially

Sequential access is much easier for caches and hardware prefetchers.
Favor passes that:

- build arrays in order
- compact visible items in one sweep
- process instances in sorted batches
- upload data in large contiguous blocks

Avoid jumping around in memory during the same pass.

## Data layout recommendations

### Packed frame buffers

Use large buffers for dynamic renderer data and suballocate slices from them.
That already matches the engine design well.

Recommended patterns:

- one buffer for CPU-visible transient data
- one buffer for GPU-only persistent data
- one staging buffer for uploads
- offsets passed through push constants

This keeps allocations cheap and improves locality compared with many tiny buffers.

### Stable IDs and dense tables

When tracking resources, prefer dense arrays with stable IDs or generation counters.
Dense arrays let you:

- iterate quickly
- compact dead entries cheaply
- avoid pointer chasing
- keep resources tightly packed

### Small metadata, large payloads

Keep metadata compact and store large payloads separately.
For example:

- compact draw records
- compact light records
- compact cluster descriptors
- large texture and mesh payloads in pools

This makes the hot metadata cache-friendly while the bulk data stays in big sequential buffers.

## Upload and staging path

Uploads can become cache-unfriendly if the code repeatedly touches tiny chunks of memory.
To improve that:

- batch small uploads together
- copy from contiguous CPU memory when possible
- reuse staging regions in a ring or linear pool
- avoid reallocating upload scratch buffers every frame

For transient uploads:

- write once
- submit once
- discard after the frame fence completes

That reduces repeated reads and writes to the same memory.

## Sorting and batching

Sorting work can improve both CPU and GPU cache efficiency.

### Sort by material or pipeline

Grouping similar draws reduces state changes and keeps command generation simple.
It also tends to improve data locality because adjacent objects often use similar resources.

### Sort by spatial locality

If visible objects are spatially sorted, neighboring objects are more likely to touch nearby:

- meshes
- materials
- textures
- cluster data

This can help cache usage during culling, light assignment, and draw preparation.

### Build batches in one pass

Try to generate draw metadata once and consume it immediately.
Avoid multiple passes over the same large visible-object list unless necessary.

## Clustered shading considerations

Clustered shading can be cache-heavy because it touches light lists, cluster metadata, and scene bounds.
To keep it efficient:

- store cluster data in dense arrays
- keep cluster-light indices compact
- use small index types if the range allows it
- avoid per-cluster heap allocations
- compact empty or unused clusters when possible

If the light list per cluster is large, use a layout that favors linear scans over random lookups.

## Buffer access patterns

This renderer does not rely on per-object vertex and index buffers in the usual way.
Instead, it uses buffer offsets passed through push constants.
That can be cache-friendly if the underlying buffer slices are packed well.

Guidelines:

- group related mesh data contiguously
- avoid scattering small slices across the same large buffer
- keep lifetime-based allocations in separate pools when possible
- prefer fixed-size records for hot render paths

If a shader repeatedly reads from a buffer slice, make sure the slice is aligned and packed so neighboring reads stay close together.

## CPU-side micro-optimizations

These are usually small wins, but they add up in hot loops:

- cache array lengths locally before loops
- avoid repeated function calls inside tight loops
- use simple branch patterns where possible
- minimize temporary copies of large structs
- use `memcpy` for packed block copies instead of manual field-by-field moves when appropriate
- remove debug-only work from per-frame hot paths

## GPU-side cache considerations

Even though this document focuses on cache efficiency broadly, GPU access patterns matter too.

Helpful rules:

- keep shader resource access coherent across neighboring threads
- prefer sequential reads in compute and fragment shaders
- avoid divergent control mu in hot paths when possible
- pack shader data into cache-friendly layouts
- keep uniform and push constant data compact

For texture-heavy rendering:

- prefer atlas or bindless layouts that reduce redundant indirection
- avoid sampling many unrelated textures in the same wave if you can batch by material

## Things to avoid

- linked lists in frame-hot systems
- many tiny allocations per frame
- pointer-rich object graphs in hot loops
- random access over giant sparse arrays
- storing large data inside frequently copied structs
- writing the same transient data multiple times per frame
- rebuilding large tables when only a small subset changed

## Profiling checklist

Before changing code, confirm where time is going.

Look for:

- high CPU active time
- cache misses in hot passes
- many small allocations
- repeated buffer growth or copies
- draw preparation taking longer than rendering
- cluster generation or light assignment becoming the bottleneck

If you have profiling tools available, inspect:

- visible-object compaction
- material sorting
- staging uploads
- barrier batching
- culling and cluster build passes

## Practical implementation order

A sensible order for improvements is:

1. Remove unnecessary allocations from hot paths.
2. Pack hot renderer data into dense arrays.
3. Split hot and cold fields.
4. Batch uploads and barriers.
5. Sort by material and spatial locality.
6. Profile again and tune the worst remaining pass.

## Example rules of thumb

- Keep frequently accessed data compact and aligned.
- Prefer one large linear pass over many small random ones.
- Make temporary data frame-scoped whenever possible.
- Use dense buffers and offsets instead of scattered allocations.
- Move cold metadata out of hot loops.
- Batch work so the same cache line is reused before eviction.

## Related docs

- [Render API Reference](RENDERER_API_REFERENCE.md)
- [Barrier Batching API](BARRIER_BATCHING_API.md)
- [Offset Allocator Design](offset_allocator_design.md)
- [Staging Buffer Review](STAGING_BUFFER_REVIEW.md)

## Summary

The fastest path to a cache-friendlier renderer is usually not a single trick.
It is a combination of:

- dense data structures
- fewer allocations
- linear access patterns
- compact hot metadata
- batched work
- careful profiling

If the renderer stays close to the current bindless, suballocated design, cache efficiency can improve without making the code much more complex.
