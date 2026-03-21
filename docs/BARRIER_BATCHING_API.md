# Barrier Batching API (Vulkan Sync2)

This note proposes a batched image barrier API for your renderer so multiple transitions are queued and flushed with a single `vkCmdPipelineBarrier2` call.

It is designed to replace patterns like:

```c
rt_transition_all(cmd, &renderer.depth[renderer.swapchain.current_image], VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
rt_transition_all(cmd, &renderer.hdr_color[renderer.swapchain.current_image], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

image_transition_swapchain(cmd, &renderer.swapchain, VK_IMAGE_LAYOUT_GENERAL,
                           VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
```

---

## Why batching

- Fewer API calls and less driver overhead.
- Clearer per-pass sync setup.
- Easier to audit transitions in one place.

---

## Step 1: Batch container

```c
#define MAX_BATCHED_IMAGE_BARRIERS 128

typedef struct BarrierBatch {
    VkImageMemoryBarrier2 image_barriers[MAX_BATCHED_IMAGE_BARRIERS];
    uint32_t image_count;
} BarrierBatch;

static inline void barrier_batch_begin(BarrierBatch* batch)
{
    batch->image_count = 0;
}
```

---

## Step 2: Add to batch instead of executing

Your proposed API is good. Keep it as-is, but add a capacity assert.

```c
void barrier_add_image(
    BarrierBatch* batch,
    VkImage image,
    VkImageLayout old_layout,
    VkImageLayout new_layout,
    VkPipelineStageFlags2 src_stage,
    VkPipelineStageFlags2 dst_stage,
    VkAccessFlags2 src_access,
    VkAccessFlags2 dst_access,
    VkImageAspectFlags aspect)
{
    assert(batch->image_count < MAX_BATCHED_IMAGE_BARRIERS);

    VkImageMemoryBarrier2* b = &batch->image_barriers[batch->image_count++];

    *b = (VkImageMemoryBarrier2){
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = src_stage,
        .srcAccessMask = src_access,
        .dstStageMask = dst_stage,
        .dstAccessMask = dst_access,
        .oldLayout = old_layout,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = aspect,
            .baseMipLevel = 0,
            .levelCount = VK_REMAINING_MIP_LEVELS,
            .baseArrayLayer = 0,
            .layerCount = VK_REMAINING_ARRAY_LAYERS
        }
    };
}
```

---

## Step 3: Flush once

Your flush function is the correct shape.

```c
void barrier_flush(VkCommandBuffer cmd, BarrierBatch* batch)
{
    if(batch->image_count == 0) return;

    VkDependencyInfo dep = {
        .sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .imageMemoryBarrierCount = batch->image_count,
        .pImageMemoryBarriers = batch->image_barriers
    };

    vkCmdPipelineBarrier2(cmd, &dep);

    batch->image_count = 0;
}
```

---

## Step 4: Integrate with your tracked state (`ImageState`)

Because your renderer already tracks per-image/mip state, add wrappers that pull `old_*` from tracked state and then update it.

```c
void barrier_add_tracked_mip(
    BarrierBatch* batch,
    VkImage image,
    ImageState* state,
    VkImageAspectFlags aspect,
    uint32_t mip,
    VkImageLayout new_layout,
    VkPipelineStageFlags2 new_stage,
    VkAccessFlags2 new_access)
{
    if(state->validity == IMAGE_STATE_VALID &&
       state->layout == new_layout &&
       state->stage == new_stage &&
       state->access == new_access)
    {
        return;
    }

    VkImageMemoryBarrier2* b = &batch->image_barriers[batch->image_count++];

    *b = (VkImageMemoryBarrier2){
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2,
        .srcStageMask = (state->validity == IMAGE_STATE_VALID) ? state->stage : VK_PIPELINE_STAGE_2_NONE,
        .srcAccessMask = (state->validity == IMAGE_STATE_VALID) ? state->access : VK_ACCESS_2_NONE,
        .dstStageMask = new_stage,
        .dstAccessMask = new_access,
        .oldLayout = (state->validity == IMAGE_STATE_VALID) ? state->layout : VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = new_layout,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = image,
        .subresourceRange = {
            .aspectMask = aspect,
            .baseMipLevel = mip,
            .levelCount = 1,
            .baseArrayLayer = 0,
            .layerCount = 1,
        },
    };

    state->layout = new_layout;
    state->stage = new_stage;
    state->access = new_access;
    state->validity = IMAGE_STATE_VALID;
}
```

Then add helpers equivalent to today’s `rt_transition_all` and `image_transition_swapchain`, but queue-only:

- `barrier_add_rt_all(BarrierBatch* batch, RenderTarget* rt, ...)`
- `barrier_add_swapchain_current(BarrierBatch* batch, FlowSwapchain* sc, ...)`

---

## Step 5: Replace current call sites

```c
BarrierBatch batch;
barrier_batch_begin(&batch);

barrier_add_rt_all(&batch, &renderer.depth[renderer.swapchain.current_image],
                   VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                   VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                   VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);

barrier_add_rt_all(&batch, &renderer.hdr_color[renderer.swapchain.current_image],
                   VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                   VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                   VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

barrier_add_swapchain_current(&batch, &renderer.swapchain,
                              VK_IMAGE_LAYOUT_GENERAL,
                              VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
                              VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);

barrier_flush(cmd, &batch);
```

---

## Practical notes

- Keep `MAX_BATCHED_IMAGE_BARRIERS` conservative and assert on overflow.
- Use tracked-state wrappers for correctness and dedup.
- Prefer one `barrier_flush` per pass boundary, not per image.
- Keep buffer and image barriers separate arrays if you later batch both kinds.

---

## Minimal rollout plan

1. Add `BarrierBatch` + `barrier_add_image` + `barrier_flush`.
2. Add queue-only wrappers mirroring existing transition helpers.
3. Replace one render pass setup block first (the snippet above).
4. Expand to other passes once validated.

---


