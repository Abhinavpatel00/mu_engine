# Offset Allocator Integration Design

This document explains how the offset allocator works, how alignment-aware allocation is handled, and how to build a buffer allocator on top of it.

## Overview

The offset allocator manages *free regions* within a fixed-size address space. It returns an `OA_Allocation` containing an offset and internal metadata. The caller uses the offset to suballocate inside a larger GPU buffer.

Key properties:

- O(1) bin selection using a small-float binning scheme.
- Neighbor links to coalesce free blocks on `oa_free()`.
- Optional alignment handling via `oa_allocate_aligned()`.
- Debug validation to assert internal consistency in debug builds.

## Conceptual Model

```
+--------------------------------------------------------------+
|                     Large GPU Buffer                         |
+--------------------+-----------------+-----------------------+
|   Free region      | Allocated (A)   |      Free region      |
+--------------------+-----------------+-----------------------+
0                  offset(A)       offset(A)+size(A)        size
```

The allocator only tracks offsets and sizes. The buffer itself is managed by your renderer (Vulkan/VMA/etc.).

## Binning and Node Lists

Free regions are stored in bins by size. Each bin holds a linked list of free nodes. A two-level bitmap identifies which bins are non-empty.

```
Top bins:  [T0][T1][T2]...[T31]    (each bit means “some leaf bin in this top bin is non-empty”)
Leaf bins: [B0..B7] per top bin   (each bit means “bin list is non-empty”)

Node list in bin:
  bin_indices[bin] -> [node] -> [node] -> ...
```

## Allocation Flow (Unaligned)

```
request size
   |
   v
pick first bin >= size  -----> choose free node
   |
   v
remove node from bin
   |
   v
split remainder (if any)
   |
   v
return offset + metadata
```

## Allocation Flow (Aligned)

`oa_allocate_aligned()` scans candidate free nodes and tries to fit an aligned block inside each node.

```
free node: [offset.....................offset+size]
                          ^ aligned offset

leading gap  |<--->|  aligned block  |<--->| trailing gap
```

If alignment introduces a leading or trailing gap, those gaps are reinserted as free nodes (if large enough). The allocation uses the aligned offset.

## Free and Coalesce

Each node keeps neighbor pointers in address order. Freeing a block merges it with adjacent free neighbors to reduce fragmentation.

```
Before:
  [free] [used] [free]

Free middle:
  [free] [free] [free]  -> coalesce -> [free]
```

## Debug Validation

In `DEBUG` builds, `oa_debug_validate()` verifies:

- Each free node is in the correct size bin.
- Bin linked lists are consistent (prev/next).
- `free_storage` equals the sum of free node sizes.
- Bin bitmasks match actual bin occupancy.

This is invoked after `oa_reset()`, `oa_allocate()`, `oa_allocate_aligned()`, and `oa_free()`.

## Building a Buffer Allocator on Top

### Core idea

Create a large GPU buffer per usage class (e.g., `GPU_LOCAL`, `UPLOAD`, `STAGING`). Each buffer owns one `OA_Allocator`.

```
BufferPool
  - VkBuffer + VmaAllocation
  - OA_Allocator
  - usage flags
  - size
```

### Typical path

1. Create buffer of size $S$.
2. Initialize allocator with `oa_init(&alloc, S, max_allocs)`.
3. When allocating:
   - Call `oa_allocate_aligned()` (or `oa_allocate()` if alignment = 1).
   - Store `OA_Allocation` in your resource handle.
   - Bind or access data at `buffer + allocation.offset`.
4. On destroy:
   - Call `oa_free()` with stored `OA_Allocation`.
5. If out of space:
   - Create a new buffer pool or grow to a larger buffer.

### Example layout

```
BufferPool[GPU_LOCAL]
  buffer: 256MB
  allocator: manages [0..256MB)

Resource Handle:
  - buffer reference
  - OA_Allocation (offset + metadata)
  - size
```

### Passing offsets

Offsets are passed to shaders via push constants or descriptor offsets. This follows the project’s bindless model and keeps per-resource state minimal.

## Recommended Extensions

- Add `oa_can_allocate(size, alignment)` for quick checks.
- Add optional thread-safe locking around allocator calls.
- Track allocation sizes in higher-level resource handles.

## Integration Checklist

- [ ] One `OA_Allocator` per GPU buffer.
- [ ] Use `oa_allocate_aligned()` for uniform/SSBO alignment.
- [ ] Store `OA_Allocation` in resource handles.
- [ ] Free on resource destruction.
- [ ] Validate in debug builds.
