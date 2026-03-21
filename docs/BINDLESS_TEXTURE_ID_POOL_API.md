# Bindless Texture ID Pool API Design

## Goal

Use `mu_id_pool` as a fast CPU-side allocator for bindless texture indices (descriptor array slots), so texture creation/destruction is cheap and fragmentation stays manageable.

This fits the renderer model that uses a single descriptor set layout and a single pipeline layout.

---

## Proposed API

```c
typedef struct
{
    uint32_t first;
    uint32_t last;
} mu_id_pool_range;

typedef struct
{
    mu_id_pool_range* ranges;
    uint32_t            count;
    uint32_t            capacity;
    uint32_t            max_id;
    uint32_t            used_ids;
} mu_id_pool;

void mu_id_pool_init(mu_id_pool* pool, uint32_t pool_size);
void mu_id_pool_deinit(mu_id_pool* pool);
void mu_id_pool_destroy_all(mu_id_pool* pool);

/* allocation */
bool mu_id_pool_create_id(mu_id_pool* pool, uint32_t* out_id);
bool mu_id_pool_create_range_id(mu_id_pool* pool, uint32_t* out_id, uint32_t count);

/* deallocation */
bool mu_id_pool_destroy_id(mu_id_pool* pool, uint32_t id);
bool mu_id_pool_destroy_range_id(mu_id_pool* pool, uint32_t id, uint32_t count);

/* queries */
bool mu_id_pool_is_range_available(const mu_id_pool* pool, uint32_t search_count);
void mu_id_pool_print_ranges(const mu_id_pool* pool);
void mu_id_pool_check_ranges(const mu_id_pool* pool);

uint32_t mu_id_pool_get_available_ids(const mu_id_pool* pool);
bool     mu_id_pool_is_id(const mu_id_pool* pool, uint32_t id);
uint32_t mu_id_pool_get_largest_continuous_range(const mu_id_pool* pool);
```

---

## Core Semantics

`mu_id_pool` tracks **free** ranges of descriptor IDs.

- `ranges[i] = [first, last]` (inclusive)
- ranges are sorted by `first`
- ranges are non-overlapping and non-adjacent (adjacent ranges should be merged)
- valid IDs are in `[0, max_id - 1]`
- `used_ids + available_ids == max_id`

At init with `pool_size = N`:
- one free range exists: `[0, N - 1]`
- `used_ids = 0`

---

## High-Performance Allocation Strategy

### 1) Fast path for single ID

`mu_id_pool_create_id` should allocate from the first suitable range (or a remembered cursor index if added later).

- take `id = ranges[k].first`
- increment `ranges[k].first`
- if empty (`first > last`) remove range

This is typically $O(1)$ for stable workloads.

### 2) Contiguous range allocation

`mu_id_pool_create_range_id(pool, &id, count)`:

- find first range with size $\ge count$
- return start ID in `out_id`
- consume `[start, start + count - 1]`

Complexity: $O(R)$ where $R$ is number of free ranges (usually small if merges are correct).

### 3) Deallocation with aggressive coalescing

`mu_id_pool_destroy_id` and `mu_id_pool_destroy_range_id` must merge with neighbors:

- merge left if `left.last + 1 == new.first`
- merge right if `new.last + 1 == right.first`
- merge both when bridging two ranges

Coalescing keeps `R` low and preserves allocator speed.

---

## Suggested Error/Return Behavior

Return `false` for invalid requests:

- null pointers
- `count == 0`
- out-of-bounds IDs/ranges
- double free / overlap with already-free area
- not enough capacity on allocation

Return `true` on successful state mutation.

---

## Vulkan Bindless Texture Mapping

Each allocated ID corresponds to one slot in a large sampled-image descriptor array.

- **Allocate texture slot**: `mu_id_pool_create_id` (or range for texture arrays/pages)
- **Write descriptor** into global bindless descriptor set at that ID
- **Store ID** in material/instance data
- **Shader samples** using that integer ID
- **Free slot** on texture destruction: `mu_id_pool_destroy_id`

This avoids per-draw descriptor set churn and matches bindless rendering.

---

## Integration Pattern

### Startup

1. Choose max bindless texture count (example: 65,536).
2. Create descriptor set with that array size.
3. `mu_id_pool_init(&texture_ids, max_textures)`.

### Texture creation

1. Create image + view.
2. Allocate ID from pool.
3. Update descriptor array element at allocated ID.
4. Publish ID to runtime objects.

### Texture destruction

1. Defer destruction until GPU-safe point (frame fence/timeline).
2. Optionally write a default/fallback descriptor to that slot.
3. Return ID to pool.

---

## Threading Model

Recommended: one lock around pool mutation, or route all create/destroy through a render-resource thread.

- Reads (`is_id`, `get_available_ids`) may use shared lock if needed.
- Keep lock scope small; do Vulkan descriptor writes outside long critical sections when possible.

---

## Performance Notes

- Pre-reserve `ranges` capacity to avoid reallocations in hot paths.
- Keep free list coalesced to minimize scan length.
- Prefer allocating IDs in monotonic patterns when possible to improve cache behavior.
- For very high churn, consider adding a small per-frame free queue and batch-return to pool.

---

## Invariants for `mu_id_pool_check_ranges`

Validate in debug builds:

1. `count <= capacity`
2. each range has `first <= last`
3. sorted: `ranges[i].last < ranges[i+1].first`
4. non-adjacent: `ranges[i].last + 1 < ranges[i+1].first`
5. computed free IDs + `used_ids == max_id`

---

## Minimal Usage Example

```c
mu_id_pool texture_pool;
mu_id_pool_init(&texture_pool, 65536);

uint32_t tex_id;
if (mu_id_pool_create_id(&texture_pool, &tex_id)) {
    // write descriptor for tex_id
    // store tex_id in material
}

// later, after GPU-safe retirement:
mu_id_pool_destroy_id(&texture_pool, tex_id);

mu_id_pool_deinit(&texture_pool);
```

---

## Recommended Extensions (Optional)

- `mu_id_pool_create_id_at_least(pool, min_id, out_id)` for reserved ID regions.
- `mu_id_pool_compact_hint(...)` metrics only (no remap) for fragmentation reporting.
- frame-deferred free API for direct integration with GPU retirement queues.

---

## Summary

`mu_id_pool` is a strong fit for high-performance bindless texture index allocation when:

- free ranges stay coalesced,
- destruction is deferred until GPU-safe,
- descriptor updates are centralized.

That gives stable performance, low descriptor management overhead, and predictable shader-visible texture IDs.
