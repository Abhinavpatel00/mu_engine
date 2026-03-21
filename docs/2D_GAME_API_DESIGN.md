# Building a Good 2D Game API on Top of the Current Renderer

## Overview

This document outlines the architecture and design principles for building a high-performance 2D game API that leverages your existing Vulkan-based clustered shading renderer. The key advantage is reusing your bindless model, offset allocator, and GPU infrastructure while optimizing specifically for 2D workloads.

## Core Principles

### 1. Leverage Existing Infrastructure
- **Reuse bindless descriptor sets** for texture management
- **Use offset allocator** for sprite geometry and instance data
- **Share command buffer recording** through a unified submission system
- **Inherit post-processing pipeline** for effects (color grading, etc.)

### 2. Optimize for 2D Rendering
- **Minimal push constant data** (transform, texture region, layer depth)
- **Instanced rendering** for repeated sprites
- **Batch by texture** to minimize descriptor set changes
- **Z-ordering through depth values** (easier than 3D geometry management)

### 3. Memory Efficiency
- **Dynamic geometry buffers** for quads using offset allocator
- **Shared sampler pool** (reuse linear/nearest, clamp/wrap variants)
- **Atlas-based textures** to reduce state changes
- **Instance data streaming** per frame

## Architecture Layers

```
┌─────────────────────────────────────────┐
│      2D Game API Layer (User Code)      │
├─────────────────────────────────────────┤
│   Scene Graph / Sprite Manager          │
├─────────────────────────────────────────┤
│   2D Rendering Backend (This Layer)     │
│   ├─ Quad Batch Manager                 │
│   ├─ Transform Hierarchy                │
│   └─ 2D Camera System                   │
├─────────────────────────────────────────┤
│   Vulkan Core (Existing)                │
│   ├─ Offset Allocator                   │
│   ├─ Bindless Textures                  │
│   └─ Command Buffer Submission          │
└─────────────────────────────────────────┘
```

## API Design

### 1. 2D Sprite Type

```c
typedef struct Sprite2D
{
    // Rendering
    TextureID texture_id;
    vec2      position;
    vec2      scale;
    float     rotation;
    
    // Appearance
    vec4      tint_color;  // For color modulation
    float     depth;       // Z-order (0.0 to 1.0)
    
    // Texture mapping
    vec4      uv_rect;     // x,y = min UV, z,w = max UV (for atlas)
    
    // Transform
    uint32_t  transform_offset;  // Push constant offset
    bool      dirty;
} Sprite2D;
```

### 2. Batch Data Structure

Store on GPU using offset allocator:

```c
typedef struct GPU_Quad2D
{
    // Instance data (vec4 aligned)
    vec2  position;
    vec2  scale;
    
    float rotation;
    float depth;
    float opacity;
    uint32_t texture_id;
    
    // UV coordinates
    vec4  uv_rect;  // min_u, min_v, max_u, max_v
    
    // Color tint
    vec4  tint;
} GPU_Quad2D;
```

### 3. 2D Camera System

```c
typedef struct Camera2D
{
    vec2  position;       // World position
    float zoom;           // Scale factor
    float rotation;       // Rotation in radians
    
    uint32_t viewport_width;
    uint32_t viewport_height;
    
    // Cached matrix (update when dirty)
    mat4 view_matrix;
    mat4 projection_matrix;
    mat4 view_projection;
    bool dirty;
} Camera2D;

void camera2d_update_matrices(Camera2D* cam)
{
    // Build projection: orthographic
    glm_ortho(0.0f, (float)cam->viewport_width,
              (float)cam->viewport_height, 0.0f,
              -1.0f, 1.0f, cam->projection_matrix);
    
    // Build view: position + zoom + rotation
    glm_mat4_identity(cam->view_matrix);
    glm_translate(cam->view_matrix, (vec3){-cam->position[0], -cam->position[1], 0.0f});
    glm_scale(cam->view_matrix, (vec3){cam->zoom, cam->zoom, 1.0f});
    // Rotation around camera center...
    
    glm_mat4_mul(cam->projection_matrix, cam->view_matrix, cam->view_projection);
}
```

### 4. Sprite Batch Manager

```c
typedef struct SpriteBatch2D
{
    // GPU resources
    Buffer quad_geometry;          // Pre-allocated large buffer
    OffsetAllocator geometry_alloc;
    
    // CPU-side tracking
    Sprite2D* sprites;
    uint32_t  sprite_count;
    uint32_t  max_sprites;
    
    // Batching
    typedef struct {
        TextureID texture;
        uint32_t quad_offset;
        uint32_t quad_count;
    } Batch;
    
    Batch* sorted_batches;
    uint32_t batch_count;
    
    // Camera
    Camera2D camera;
    
    // Sync
    bool dirty;
} SpriteBatch2D;
```

### 5. Rendering Pipeline

Create a specialized 2D shader pipeline:

```glsl
// vertex.vert (2D)
#version 460 core
#extension GL_EXT_buffer_reference : require

layout(push_constant) uniform PushConstants {
    mat4 view_projection;
    uint quad_offset;
    uint base_texture_id;
} pc;

struct Quad2D {
    vec2 position;
    vec2 scale;
    float rotation;
    float depth;
    float opacity;
    uint texture_id;
    vec4 uv_rect;
    vec4 tint;
};

layout(buffer_reference, std430) readonly buffer QuadBuffer {
    Quad2D quads[];
};

layout(location = 0) out vec2 out_uv;
layout(location = 1) out vec4 out_tint;
layout(location = 2) out flat uint out_texture_id;

void main()
{
    uint quad_idx = gl_VertexIndex / 6;
    uint vertex_idx = gl_VertexIndex % 6;
    
    // Load quad from buffer
    QuadBuffer* buf = QuadBuffer(quad_offset);
    Quad2D quad = buf->quads[quad_idx];
    
    // Triangle strip to quad: vertex positions
    vec2 positions[6] = vec2[](
        vec2(0, 0), vec2(1, 0), vec2(1, 1),
        vec2(0, 0), vec2(1, 1), vec2(0, 1)
    );
    vec2 uvs[6] = vec2[](
        quad.uv_rect.xy, vec2(quad.uv_rect.z, quad.uv_rect.y), quad.uv_rect.zw,
        quad.uv_rect.xy, quad.uv_rect.zw, vec2(quad.uv_rect.x, quad.uv_rect.w)
    );
    
    vec2 pos = positions[vertex_idx];
    
    // Apply scale and rotation
    pos *= quad.scale;
    float cos_r = cos(quad.rotation);
    float sin_r = sin(quad.rotation);
    pos = vec2(pos.x * cos_r - pos.y * sin_r,
               pos.x * sin_r + pos.y * cos_r);
    pos += quad.position;
    
    // Transform to screen space
    gl_Position = pc.view_projection * vec4(pos, quad.depth, 1.0);
    
    out_uv = uvs[vertex_idx];
    out_tint = quad.tint * quad.opacity;
    out_texture_id = quad.texture_id;
}
```

```glsl
// fragment.frag (2D)
#version 460 core
#extension GL_EXT_nonuniform_qualifier : require

layout(location = 0) in vec2 in_uv;
layout(location = 1) in vec4 in_tint;
layout(location = 2) in flat uint in_texture_id;

layout(location = 0) out vec4 out_color;

// Bindless textures (shared with main renderer)
layout(set = 0, binding = 0) uniform sampler2D textures[];
layout(set = 0, binding = 1) uniform sampler samplers[];

void main()
{
    vec4 tex_color = texture(sampler2D(textures[in_texture_id], samplers[0]), in_uv);
    out_color = tex_color * in_tint;
}
```

## Rendering Workmu

### Frame Recording

```c
void sprite_batch_record(SpriteBatch2D* batch, VkCommandBuffer cmd)
{
    if (!batch->dirty) return;
    
    // 1. Update GPU data (only if sprites changed)
    sprite_batch_sync_gpu(batch);
    
    // 2. Sort sprites (by texture, then depth)
    sprite_batch_sort(batch);
    
    // 3. Record draw commands
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, batch->pipeline);
    vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                           batch->pipeline_layout, 0, 1,
                           &batch->descriptor_set, 0, NULL);
    
    // Push camera matrices
    mat4 vp = batch->camera.view_projection;
    vkCmdPushConstants(cmd, batch->pipeline_layout,
                      VK_SHADER_STAGE_VERTEX_BIT,
                      0, sizeof(mat4), (void*)vp);
    
    // Draw batches
    for (uint32_t i = 0; i < batch->batch_count; i++)
    {
        Batch* b = &batch->sorted_batches[i];
        
        // Push quad offset
        uint32_t offset = b->quad_offset;
        vkCmdPushConstants(cmd, batch->pipeline_layout,
                          VK_SHADER_STAGE_VERTEX_BIT,
                          sizeof(mat4), sizeof(uint32_t), &offset);
        
        // Draw instanced
        vkCmdDraw(cmd, b->quad_count * 6, 1, 0, 0);
    }
    
    batch->dirty = false;
}
```

### Sync GPU Data

```c
void sprite_batch_sync_gpu(SpriteBatch2D* batch)
{
    // Allocate space for all quads
    size_t needed_size = batch->sprite_count * sizeof(GPU_Quad2D);
    uint32_t offset = offset_allocator_alloc(&batch->geometry_alloc, needed_size);
    
    GPU_Quad2D* gpu_quads = (GPU_Quad2D*)(batch->quad_geometry.mapping + offset);
    
    for (uint32_t i = 0; i < batch->sprite_count; i++)
    {
        Sprite2D* sprite = &batch->sprites[i];
        GPU_Quad2D* gpu_quad = &gpu_quads[i];
        
        gpu_quad->position = sprite->position;
        gpu_quad->scale = sprite->scale;
        gpu_quad->rotation = sprite->rotation;
        gpu_quad->depth = sprite->depth;
        gpu_quad->texture_id = sprite->texture_id;
        gpu_quad->uv_rect = sprite->uv_rect;
        gpu_quad->tint = sprite->tint_color;
    }
    
    // Track offset for rendering
    batch->current_quad_offset = offset;
}
```

## Optimization Strategies

### 1. Batching
- **Sort by texture first** - minimizes descriptor changes
- **Secondary sort by depth** - for proper Z-ordering
- **Use instancing** - single draw call per texture batch

### 2. Memory Management
- **Use triple buffering** for GPU buffers
- **Pre-allocate large geometry buffers** (e.g., 64MB for 100k quads)
- **Reset allocator per frame** (or use frame arena)
- **Reuse mesh data** for repeated sprites

### 3. Transform Optimization
- **Update only dirty matrices** (use dirty flag)
- **Cache matrix multiplications** in GPU
- **Use packed transforms** (position + rotation + scale in single cache line)

### 4. Texture Management
- **Sprite sheet atlasing** - pack related sprites into single texture
- **Lazy texture loading** - load on first use
- **Mip generation** - for smooth scaling
- **Reuse bindless pool** with layer isolation

## Integration with Existing Renderer

### Shared Resources
```c
typedef struct Renderer2D_IntegrationContext
{
    // Share from main renderer
    Renderer* parent_renderer;
    
    // Reuse these:
    VkDescriptorSet bindless_descriptor_set;
    DefaultSamplerTable samplers;
    
    // 2D-specific resources
    VkPipeline sprite_pipeline;
    VkPipeline text_pipeline;    // For UI text rendering
    VkPipeline particle_pipeline; // Future: particles use same infrastructure
    
    Buffer sprite_vertex_buffer;
    OffsetAllocator sprite_alloc;
    
    // Camera
    Camera2D camera;
    
} Renderer2D_IntegrationContext;
```

### Integration Points
1. **In frame rendering**: Record 2D draw before post-processing but after 3D
2. **Texture loading**: Use `load_texture(renderer, path)` to register in bindless pool
3. **Sampler selection**: Reuse linear/nearest samplers from main renderer
4. **Command buffer**: Use existing command pool/buffers

## User-Facing API Example

```c
// Initialization
SpriteBatch2D* batch = sprite_batch_create(renderer, 10000); // max 10k sprites

// Set camera
batch->camera.position = (vec2){400, 300};
batch->camera.zoom = 2.0f;

// Load textures
TextureID player_tex = load_texture(renderer, "assets/player.png");
TextureID enemy_tex = load_texture(renderer, "assets/enemy.png");

// Add sprites
Sprite2D player = {
    .texture_id = player_tex,
    .position = {100, 100},
    .scale = {32, 32},
    .rotation = 0,
    .tint_color = {1, 1, 1, 1},
    .depth = 0.5f
};
sprite_batch_add(batch, &player);

// Each frame - update
player.position[0] += velocity_x;
if (sprite_batch_update_sprite(batch, 0, &player)) {
    batch->dirty = true;
}

// Record commands
sprite_batch_record(batch, command_buffer);
```

## Performance Considerations

| Aspect | Strategy | Expected Performance |
|--------|----------|---------------------|
| Draw calls | Batch by texture | 1-3 draw calls for most scenes |
| GPU memory | Offset allocator reuse | <100MB for 100k sprites |
| CPU overhead | Dirty flag + early exit | <1ms scene update for 10k sprites |
| Texture changes | Atlas + UV scrolling | Amortized to 1-2 binding changes |
| Transform calc | GPU-side in shader | O(n) on GPU, cached matrices |

## Advanced Features (Future)

### 1. Particle System
- Use same quad pipeline with per-particle lifetime data
- Packed as CPU-stream compute shader input

### 2. Text Rendering
- Glyph atlas (similar to sprite atlas)
- Instanced quad rendering per character
- Reuse batch infrastructure

### 3. UI Layer
- Separate batch for UI (rendered after game)
- 9-slice patches using UV adjustments
- Scissor rects for clipping (RenderPass2)

### 4. Post-Process Specific to 2D
- Chromatic aberration
- Motion blur per sprite depth layer
- Screen-space distortion

## Conclusion

By building the 2D API as a thin layer on top of your existing Vulkan infrastructure, you get:

✓ **Code reuse** - leverage bindless model, offset allocator, command submission  
✓ **Minimal overhead** - specialized shaders only for 2D  
✓ **High performance** - GPU-driven instancing, efficient batching  
✓ **Easy integration** - share textures/samplers with 3D renderer  
✓ **Extensibility** - particle systems, text, UI all use same infrastructure  

The key is keeping the API simple for users while maintaining tight integration with the GPU infrastructure below.
