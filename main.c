#include "external/cglm/include/cglm/types.h"
#include "external/cglm/include/cglm/vec2.h"
#include "renderer.h"
#include "tinytypes.h"
#include "vk.h"
#include <GLFW/glfw3.h>
#include <dirent.h>
#include <stdalign.h>
#include <stddef.h>
#include <stdint.h>
#include <math.h>
#include <time.h>
#include <stdbool.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include "stb/stb_perlin.h"
#include "text_system.h"
#define DMON_IMPL
#include "external/dmon/dmon.h"

static volatile bool shader_changed = false;
static char          changed_shader[256];
static void inline watch_cb(dmon_watch_id id, dmon_action action, const char* root, const char* filepath, const char* oldfilepath, void* user)
{
    if(action == DMON_ACTION_MODIFY || action == DMON_ACTION_CREATE)
    {
        if(strstr(filepath, ".slang"))
        {
            snprintf(changed_shader, sizeof(changed_shader), "%s", filepath);
            shader_changed = true;
        }
    }
}
#include "external/tracy/public/tracy/TracyC.h"

#define RUN_ONCE_N(name) for(static bool name = true; name; name = false)
static bool voxel_debug     = true;
static bool take_screenshot = true;
static bool wireframe_mode  = false;

#define PRINT_FIELD(type, field)                                                                                       \
    printf("%-20s offset = %3zu  align = %2zu  size = %2zu\n", #field, offsetof(type, field),                          \
           _Alignof(((type*)0)->field), sizeof(((type*)0)->field))

#define PRINT_STRUCT(type) printf("\nSTRUCT %-20s size = %zu  align = %zu\n\n", #type, sizeof(type), _Alignof(type));

static inline size_t mu_ravel_index(const size_t* coord, const size_t* strides, size_t ndim)
{
    size_t index = 0;

    for(size_t i = 0; i < ndim; i++)
        index += coord[i] * strides[i];

    return index;
}


static inline void mu_unravel_index(size_t index, const size_t* dims, size_t ndim, size_t* coord)
{
    for(int i = ndim - 1; i >= 0; i--)
    {
        coord[i] = index % dims[i];
        index /= dims[i];
    }
}

typedef struct Sprite2D
{
    // Rendering
    TextureID texture_id;
    vec2      position;
    vec2      scale;
    float     rotation;

    vec2 velocity;
    // Appearance
    vec4  tint_color;  // For color modulation
    float depth;       // Z-order (0.0 to 1.0)

    // Texture mapping
    vec4 uv_rect;  // x,y = min UV, z,w = max UV (for atlas)

    // Transform
    uint32_t transform_offset;  // Push constant offset
    bool     dirty;
} Sprite2D;


// TODO: memory bandwidth is crucial as fuck so we will optimize it for that
typedef struct GPU_Quad2D
{
    // Instance data (vec4 aligned)
    vec2 position;
    vec2 scale;

    float    rotation;
    float    depth;
    float    opacity;
    uint32_t texture_id;

    // UV coordinates
    vec4 uv_rect;  // min_u, min_v, max_u, max_v

    // Color tint
    vec4 tint;
} GPU_Quad2D;
PUSH_CONSTANT(SpritePushConstants,
              VkDeviceAddress instance_ptr;  // 8
              vec2            screen_size;   // 8 → 16 total

              uint32_t sampler_id;  // 4
              uint32_t ok;          // 4 → 24

              float _padC[2];  // 8 → NOW offset = 32 ✅

              float view_proj[4][4];  // aligned to 16
);

#define MAX_DRAWS 1024
#define MAX_SPRITES 10000
#define MAX_SNAKE 256
#define CELL 32.0f
#define MAX_EATEN_FOOD 2048
typedef struct SpriteRenderer
{
    GPU_Quad2D* instances;
    uint32_t    instance_count;
    uint32_t    capacity;

    VkDrawIndirectCommand* draws;
    uint32_t               draw_count;

    BufferSlice instance_buffer;
    BufferSlice indirect_buffer;
    BufferSlice count_buffer;

    bool dirty;
} SpriteRenderer;

typedef struct Snake
{
    vec2 body[MAX_SNAKE];
    vec2 prev_body[MAX_SNAKE];
    int  length;
    vec2 food_eaten[MAX_EATEN_FOOD];
    u32  eaten_count;
    vec2 dir;
    vec2 next_dir;

    float timer;
    float delay;

    bool just_reset;
} Snake;
// lifecycle
void sprite_renderer_init(SpriteRenderer* r, uint32_t max_sprites);
void sprite_renderer_destroy(SpriteRenderer* r);

// per-frame
void sprite_begin(SpriteRenderer* r);
void sprite_submit(SpriteRenderer* r, Sprite2D* s);
void sprite_end(SpriteRenderer* r, VkCommandBuffer cmd);

// rendering

void sprite_render(SpriteRenderer* r, VkCommandBuffer cmd, const Camera* cam);
void sprite_renderer_init(SpriteRenderer* r, uint32_t max_sprites)
{
    r->capacity = max_sprites;

    r->instances = malloc(sizeof(GPU_Quad2D) * max_sprites);
    r->draws     = malloc(sizeof(VkDrawIndirectCommand) * MAX_DRAWS);

    r->instance_buffer = buffer_pool_alloc(&renderer.gpu_pool, sizeof(GPU_Quad2D) * max_sprites, 16);

    r->indirect_buffer = buffer_pool_alloc(&renderer.gpu_pool, sizeof(VkDrawIndirectCommand) * MAX_DRAWS, 16);

    r->count_buffer = buffer_pool_alloc(&renderer.cpu_pool, sizeof(uint32_t), 4);
}
void sprite_begin(SpriteRenderer* r)
{
    r->instance_count = 0;
    r->draw_count     = 0;
    r->dirty          = false;
}


void sprite_submit(SpriteRenderer* r, Sprite2D* s)
{


    // may use dynamic array with stb
    if(r->instance_count >= r->capacity)
        return;  // or assert if you're serious about life

    GPU_Quad2D* q = &r->instances[r->instance_count++];

    // pack data
    glm_vec2_copy(s->position, q->position);
    glm_vec2_copy(s->scale, q->scale);
    glm_vec4_copy(s->uv_rect, q->uv_rect);
    glm_vec4_copy(s->tint_color, q->tint);

    q->rotation   = s->rotation;
    q->depth      = s->depth;
    q->texture_id = s->texture_id;
    q->opacity    = s->tint_color[3];

    r->dirty = true;
}
static int compare_texture(const void* a, const void* b)
{
    const GPU_Quad2D* A = a;
    const GPU_Quad2D* B = b;
    return (int)A->texture_id - (int)B->texture_id;
}

void sprite_end(SpriteRenderer* r, VkCommandBuffer cmd)
{
    if(r->instance_count == 0)
        return;

    /*
        STEP 1: SORT

        Before:
            [tex1][tex2][tex1][tex3]  -> garbage batching

        After:
            [tex1][tex1][tex2][tex3]  -> perfect batching
    */
    qsort(r->instances, r->instance_count, sizeof(GPU_Quad2D), compare_texture);

    /*
        STEP 2: BUILD INDIRECT COMMANDS

        Each batch = one draw call
    */
    uint32_t start = 0;

    while(start < r->instance_count)

    {
        uint32_t tex   = r->instances[start].texture_id;
        uint32_t count = 1;

        for(uint32_t i = start + 1; i < r->instance_count; i++)
        {
            if(r->instances[i].texture_id != tex)
                break;
            count++;
        }

        VkDrawIndirectCommand* cmdi = &r->draws[r->draw_count++];

        cmdi->vertexCount   = 6;
        cmdi->instanceCount = count;
        cmdi->firstVertex   = 0;
        cmdi->firstInstance = start;

        start += count;
    }

    /*
        STEP 3: UPLOAD

        CPU → GPU
    */
    VkDeviceSize instance_size = sizeof(GPU_Quad2D) * r->instance_count;

    renderer_upload_buffer_to_slice(&renderer, cmd, r->instance_buffer, r->instances, instance_size, 16);

    VkDeviceSize indirect_size = sizeof(VkDrawIndirectCommand) * r->draw_count;

    renderer_upload_buffer_to_slice(&renderer, cmd, r->indirect_buffer, r->draws, indirect_size, 16);
    /*
        STEP 4: BARRIER

        COPY → SHADER READ
    */


    VkBufferMemoryBarrier2 barriers[2] = {// instance buffer
                                          {
                                              .sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                                              .srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT,
                                              .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                              .dstStageMask  = VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT,
                                              .dstAccessMask = VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
                                              .buffer        = r->instance_buffer.buffer,
                                              .offset        = r->instance_buffer.offset,
                                              .size          = instance_size,
                                          },

                                          // INDIRECT BUFFER (this one you ignored)
                                          {
                                              .sType         = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2,
                                              .srcStageMask  = VK_PIPELINE_STAGE_2_COPY_BIT,
                                              .srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT,
                                              .dstStageMask  = VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT,
                                              .dstAccessMask = VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT,
                                              .buffer        = r->indirect_buffer.buffer,
                                              .offset        = r->indirect_buffer.offset,
                                              .size          = indirect_size,
                                          }};

    VkDependencyInfo dep = {
        .sType                    = VK_STRUCTURE_TYPE_DEPENDENCY_INFO,
        .bufferMemoryBarrierCount = 2,
        .pBufferMemoryBarriers    = barriers,
    };

    vkCmdPipelineBarrier2(cmd, &dep);

    /*
        STEP 5: WRITE DRAW COUNT
    */
    *(uint32_t*)r->count_buffer.mapped = r->draw_count;
}
void sprite_render(SpriteRenderer* r, VkCommandBuffer cmd, const Camera* cam)
{
    if(r->draw_count == 0)
        return;
    VkBufferDeviceAddressInfo addr_info = {
        .sType  = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO,
        .buffer = r->instance_buffer.buffer,
    };

    SpritePushConstants push = {
        .instance_ptr = vkGetBufferDeviceAddress(renderer.device, &addr_info) + r->instance_buffer.offset,

        .screen_size = {(float)renderer.swapchain.extent.width, (float)renderer.swapchain.extent.height},

        .sampler_id = renderer.default_samplers.samplers[SAMPLER_LINEAR_WRAP_ANISO],
    };

    glm_mat4_ucopy(cam->view_proj, push.view_proj);
    vkCmdPushConstants(cmd, renderer.bindless_system.pipeline_layout, VK_SHADER_STAGE_ALL, 0, sizeof(SpritePushConstants), &push);
    vk_cmd_set_viewport_scissor(cmd, renderer.swapchain.extent);
    vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g_render_pipelines.pipelines[pipelines.sprite]);

    vkCmdDrawIndirectCount(cmd, r->indirect_buffer.buffer, r->indirect_buffer.offset, r->count_buffer.buffer,
                           r->count_buffer.offset, r->draw_count, sizeof(VkDrawIndirectCommand));
}
static void snake_init(Snake* s)
{
    s->length = 3;

    s->body[0][0] = 10.0f;
    s->body[0][1] = 10.0f;
    s->body[1][0] = 9.0f;
    s->body[1][1] = 10.0f;
    s->body[2][0] = 8.0f;
    s->body[2][1] = 10.0f;

    for(int i = 0; i < MAX_SNAKE; i++)
    {
        s->prev_body[i][0] = s->body[0][0];
        s->prev_body[i][1] = s->body[0][1];
    }

    s->dir[0]      = 1.0f;
    s->dir[1]      = 0.0f;
    s->next_dir[0] = s->dir[0];
    s->next_dir[1] = s->dir[1];

    s->timer = 0.0f;
    s->delay = 0.12f;

    s->just_reset = true;
}

static bool is_eaten(Snake* s, int x, int y)
{
    for(int i = 0; i < s->eaten_count; i++)
    {
        if(s->food_eaten[i][0] == x && s->food_eaten[i][1] == y)
            return true;
    }
    return false;
}


static void snake_input(Snake* s)
{
    // UP (W or ↑)
    if(glfwGetKey(renderer.window, GLFW_KEY_W) == GLFW_PRESS ||
       glfwGetKey(renderer.window, GLFW_KEY_UP) == GLFW_PRESS)
    {
        if(s->dir[1] != 1.0f)
        {
            s->next_dir[0] = 0.0f;
            s->next_dir[1] = -1.0f;
        }
    }

    // DOWN (S or ↓)
    if(glfwGetKey(renderer.window, GLFW_KEY_S) == GLFW_PRESS ||
       glfwGetKey(renderer.window, GLFW_KEY_DOWN) == GLFW_PRESS)
    {
        if(s->dir[1] != -1.0f)
        {
            s->next_dir[0] = 0.0f;
            s->next_dir[1] = 1.0f;
        }
    }

    // LEFT (A or ←)
    if(glfwGetKey(renderer.window, GLFW_KEY_A) == GLFW_PRESS ||
       glfwGetKey(renderer.window, GLFW_KEY_LEFT) == GLFW_PRESS)
    {
        if(s->dir[0] != 1.0f)
        {
            s->next_dir[0] = -1.0f;
            s->next_dir[1] = 0.0f;
        }
    }

    // RIGHT (D or →)
    if(glfwGetKey(renderer.window, GLFW_KEY_D) == GLFW_PRESS ||
       glfwGetKey(renderer.window, GLFW_KEY_RIGHT) == GLFW_PRESS)
    {
        if(s->dir[0] != -1.0f)
        {
            s->next_dir[0] = 1.0f;
            s->next_dir[1] = 0.0f;
        }
    }
}

static uint32_t snake_hash2d(int x, int y)
{
    uint32_t h = (uint32_t)(x * 374761393u + y * 668265263u);
    h          = (h ^ (h >> 13)) * 1274126177u;
    return h;
}


bool snake_is_food_at(int x, int y)
{
    return (snake_hash2d(x, y) % 20u) == 0u;
}
static void snake_step(Snake* s)
{
    for(int i = 0; i < s->length; i++)
    {
        s->prev_body[i][0] = s->body[i][0];
        s->prev_body[i][1] = s->body[i][1];
    }

    s->dir[0] = s->next_dir[0];
    s->dir[1] = s->next_dir[1];

    for(int i = s->length - 1; i > 0; i--)
    {
        s->body[i][0] = s->body[i - 1][0];
        s->body[i][1] = s->body[i - 1][1];
    }

    s->body[0][0] += s->dir[0];
    s->body[0][1] += s->dir[1];

    for(int i = 1; i < s->length; i++)
    {
        if((int)s->body[0][0] == (int)s->body[i][0] && (int)s->body[0][1] == (int)s->body[i][1])
        {
            snake_init(s);
            return;
        }
    }

    int      head_x = (int)s->body[0][0];
    int      head_y = (int)s->body[0][1];
    uint32_t h      = (uint32_t)(head_x * 374761393u + head_y * 668265263u);
    h               = (h ^ (h >> 13)) * 1274126177u;

    if(snake_is_food_at(head_x, head_y) && !is_eaten(s, head_x, head_y))
    {
        if(s->length < MAX_SNAKE)
            s->length++;

        // store eaten food
        if(s->eaten_count < MAX_EATEN_FOOD)
        {
            s->food_eaten[s->eaten_count][0] = head_x;
            s->food_eaten[s->eaten_count][1] = head_y;
            s->eaten_count++;
        }
    }
}

static void snake_update(Snake* s, float dt)
{
    s->timer += dt;

    if(s->timer >= s->delay)
    {
        s->timer -= s->delay;
        snake_step(s);
    }
}

static float snake_interp_alpha(const Snake* s)
{
    if(s->just_reset)
        return 0.0f;

    float alpha = s->timer / s->delay;
    if(alpha < 0.0f)
        return 0.0f;
    if(alpha > 1.0f)
        return 1.0f;
    return alpha;
}

static void snake_end_frame(Snake* s)
{
    s->just_reset = false;
}

static void snake_head_render_position(const Snake* s, float* out_x, float* out_y)
{
    float alpha = snake_interp_alpha(s);

    float head_x = s->prev_body[0][0] + (s->body[0][0] - s->prev_body[0][0]) * alpha;
    float head_y = s->prev_body[0][1] + (s->body[0][1] - s->prev_body[0][1]) * alpha;

    *out_x = head_x * CELL;
    *out_y = head_y * CELL;
}

static void snake_render(SpriteRenderer* r, Snake* s)
{
    float alpha = snake_interp_alpha(s);

    for(int i = 0; i < s->length; i++)
    {
        Sprite2D spr = {0};

        spr.texture_id = renderer.dummy_texture;

        float grid_x = s->prev_body[i][0] + (s->body[i][0] - s->prev_body[i][0]) * alpha;
        float grid_y = s->prev_body[i][1] + (s->body[i][1] - s->prev_body[i][1]) * alpha;

        spr.position[0] = grid_x * CELL;
        spr.position[1] = grid_y * CELL;

        spr.scale[0] = CELL;
        spr.scale[1] = CELL;

        spr.uv_rect[0] = 0.0f;
        spr.uv_rect[1] = 0.0f;
        spr.uv_rect[2] = 1.0f;
        spr.uv_rect[3] = 1.0f;
        spr.depth      = 0.0f;
        spr.rotation   = 0.0f;

        if(i == 0)
        {
            spr.tint_color[0] = 0.3f;
            spr.tint_color[1] = 1.0f;
            spr.tint_color[2] = 0.3f;
            spr.tint_color[3] = 1.0f;
            spr.rotation      = atan2f(s->dir[1], s->dir[0]);
        }
        else
        {
            spr.tint_color[0] = 0.2f;
            spr.tint_color[1] = 0.8f;
            spr.tint_color[2] = 0.2f;
            spr.tint_color[3] = 1.0f;
        }

        sprite_submit(r, &spr);
    }
}

static void snake_render_visible_food(SpriteRenderer* r, float cam_x, float cam_y, Snake* snake)
{
    int half_cells_x = (int)ceilf(((float)renderer.swapchain.extent.width * 0.5f) / CELL);
    int half_cells_y = (int)ceilf(((float)renderer.swapchain.extent.height * 0.5f) / CELL);

    int cam_cell_x = (int)floorf(cam_x / CELL);
    int cam_cell_y = (int)floorf(cam_y / CELL);

    int min_x = cam_cell_x - half_cells_x - 2;
    int max_x = cam_cell_x + half_cells_x + 2;
    int min_y = cam_cell_y - half_cells_y - 2;
    int max_y = cam_cell_y + half_cells_y + 2;

    for(int y = min_y; y <= max_y; y++)
    {
        for(int x = min_x; x <= max_x; x++)
        {
            if(!snake_is_food_at(x, y))
                continue;
            if(is_eaten(snake, x, y))
                continue;
            Sprite2D food = {0};

            food.texture_id    = renderer.dummy_texture;
            food.position[0]   = (float)x * CELL;
            food.position[1]   = (float)y * CELL;
            food.scale[0]      = CELL;
            food.scale[1]      = CELL;
            food.rotation      = 0.0f;
            food.depth         = 0.0f;
            food.tint_color[0] = 1.0f;
            food.tint_color[1] = 0.2f;
            food.tint_color[2] = 0.2f;
            food.tint_color[3] = 1.0f;
            food.uv_rect[0]    = 0.0f;
            food.uv_rect[1]    = 0.0f;
            food.uv_rect[2]    = 1.0f;
            food.uv_rect[3]    = 1.0f;

            sprite_submit(r, &food);
        }
    }
}
int main(void)
{
    graphics_init();

    const CameraMode app_camera_mode = CAMERA_MODE_2D;

    Camera cam = {0};
    if(app_camera_mode == CAMERA_MODE_2D)
    {
        camera_defaults_2d(&cam, renderer.swapchain.extent.width, renderer.swapchain.extent.height);
        cam.ortho_height_world = (float)renderer.swapchain.extent.height;
        cam.zoom               = 1.0f;
        camera2d_set_position(&cam, (float)renderer.swapchain.extent.width * 0.5f, (float)renderer.swapchain.extent.height * 0.5f);
    }
    else
    {
        camera_defaults_3d(&cam);
        camera3d_set_position(&cam, 11.0f, 3.3f, 8.6f);
        camera3d_set_rotation_yaw_pitch(&cam, glm_rad(5.7f), glm_rad(0.0f));
    }

    SpriteRenderer sprites;

    sprite_renderer_init(&sprites, 10000);
    text_system_init("/home/lk/myprojects/voxelfun/clusteredshading/assets/font/ttf/FiraCode-Bold.ttf", 48.0f);

    glfwSetInputMode(renderer.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

    dmon_init();
    dmon_watch("shaders", watch_cb, DMON_WATCHFLAGS_RECURSIVE, NULL);

    Snake snake = {0};
    snake_init(&snake);

    while(!glfwWindowShouldClose(renderer.window))
    {
        //MU_SCOPE_TIMER("GAME")
        {
            text_system_begin_frame();
            sprite_begin(&sprites);
            snake_input(&snake);
            snake_update(&snake, renderer.dt);
            float camera_x = 0.0f;
            float camera_y = 0.0f;
            snake_head_render_position(&snake, &camera_x, &camera_y);
            camera2d_set_position(&cam, camera_x, camera_y);
            snake_render_visible_food(&sprites, camera_x, camera_y,&snake);
            snake_render(&sprites, &snake);
            snake_end_frame(&snake);
            vec4 text_color = {1.0f, 2.0f, 2.0f, 1.0f};
            draw_text_2d("Hello World", 40.0f, 40.0f, 0.5f, text_color, 0.0f);
        }


        //     MU_SCOPE_TIMER("GRAPHICS CPU")
        {
            TracyCFrameMark;
            TracyCZoneN(frame_loop_zone, "Frame Loop", 1);

            TracyCZoneN(hot_reload_zone, "Hot Reload + Pipeline Rebuild", 1);
            if(shader_changed)
            {
                shader_changed = false;
                printf("hello");
                system("./compileslang.sh");

                pipeline_mark_dirty(changed_shader);
            }
            pipeline_rebuild(&renderer);
            TracyCZoneEnd(hot_reload_zone);

            TracyCZoneN(frame_start_zone, "frame_start", 1);
            frame_start(&renderer, &cam);
            TracyCZoneEnd(frame_start_zone);

            TracyCZoneN(streaming_zone, "Frame Loop", 1);
            if(!voxel_debug)
            {
            }
            TracyCZoneEnd(streaming_zone);

            VkCommandBuffer cmd        = renderer.frames[renderer.current_frame].cmdbuf;
            GpuProfiler*    frame_prof = &renderer.gpuprofiler[renderer.current_frame];

            uint32_t current_image = renderer.swapchain.current_image;
            TracyCZoneN(record_cmd_zone, "Record Command Buffer", 1);
            vk_cmd_begin(cmd, false);
            {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer.bindless_system.pipeline_layout,
                                        0, 1, &renderer.bindless_system.set, 0, NULL);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, renderer.bindless_system.pipeline_layout,
                                        0, 1, &renderer.bindless_system.set, 0, NULL);
                rt_transition_all(cmd, &renderer.depth[renderer.swapchain.current_image], VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);
                rt_transition_all(cmd, &renderer.hdr_color[renderer.swapchain.current_image], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

                image_transition_swapchain(cmd, &renderer.swapchain, VK_IMAGE_LAYOUT_GENERAL,
                                           VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
                flush_barriers(cmd);

                //  MU_SCOPE_TIMER("SPRITE CPU")
                {
                    sprite_end(&sprites, cmd);
                }
                bool text_ready = text_system_prepare_gpu_data(cmd);

                if(!text_ready)
                {
                    text_system_handle_prepare_failure();
                }
            }


            TracyCZoneN(imgui_zone, "ImGui CPU", 1);
            {
                imgui_begin_frame();


                igBegin("Renderer Debug", NULL, 0);

                double cpu_frame_ms  = renderer.cpu_frame_ns / 1000000.0;
                double cpu_active_ms = renderer.cpu_active_ns / 1000000.0;
                double cpu_wait_ms   = renderer.cpu_wait_ns / 1000000.0;

                igText("CPU frame (wall): %.3f ms", cpu_frame_ms);
                igText("CPU active: %.3f ms", cpu_active_ms);
                igText("CPU wait: %.3f ms", cpu_wait_ms);
                igText("FPS: %.1f", cpu_frame_ms > 0.0 ? 1000.0 / cpu_frame_ms : 0.0);

                igSeparator();
                igSeparator();

                igText("Camera Position");
                igText("x: %.3f", cam.position[0]);
                igText("y: %.3f", cam.position[1]);
                igText("z: %.3f", cam.position[2]);

                igSeparator();

                igText("Yaw: %.3f", cam.yaw);
                igText("Pitch: %.3f", cam.pitch);

                igSeparator();
                igText("GPU Profiler");
                if(frame_prof->pass_count == 0)
                {
                    igText("No GPU samples collected yet.");
                }
                for(uint32_t i = 0; i < frame_prof->pass_count; i++)
                {
                    GpuPass* pass = &frame_prof->passes[i];
                    igText("%s: %.3f ms", pass->name, pass->time_ms);
                    if(frame_prof->enable_pipeline_stats)
                    {
                        igText("  VS: %llu | FS: %llu | Prim: %llu", (unsigned long long)pass->vs_invocations,
                               (unsigned long long)pass->fs_invocations, (unsigned long long)pass->primitives);
                    }
                }

                igEnd();
                igRender();
            }
            TracyCZoneEnd(imgui_zone);

            gpu_profiler_begin_frame(frame_prof, cmd);
            {

                VkRenderingAttachmentInfo color = {
                    .sType            = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .imageView        = renderer.hdr_color[renderer.swapchain.current_image].view,
                    .imageLayout      = renderer.hdr_color[renderer.swapchain.current_image].mip_states[0].layout,
                    .loadOp           = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp          = VK_ATTACHMENT_STORE_OP_STORE,
                    .clearValue.color = {{0.1f, 0.1f, 0.1f, 1.0f}}};
                VkRenderingAttachmentInfo depth = {
                    .sType                   = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                    .imageView               = renderer.depth[renderer.swapchain.current_image].view,
                    .imageLayout             = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                    .loadOp                  = VK_ATTACHMENT_LOAD_OP_CLEAR,
                    .storeOp                 = VK_ATTACHMENT_STORE_OP_STORE,
                    .clearValue.depthStencil = {0.0f, 0},
                };

                VkRenderingInfo rendering = {
                    .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
                    .renderArea.extent    = renderer.swapchain.extent,
                    .layerCount           = 1,
                    .colorAttachmentCount = 1,
                    .pColorAttachments    = &color,
                    .pDepthAttachment     = &depth,
                };


                vkCmdBeginRendering(cmd, &rendering);


                // --- RENDER ---

                //     MU_SCOPE_TIMER("SP and TE CPU")
                {
                    sprite_render(&sprites, cmd, &cam);
                    text_system_render(cmd);
                }
                vkCmdEndRendering(cmd);
            }
            //
#include "passes.h"
            post_pass();
            pass_smaa();
            pass_ldr_to_swapchain();
            pass_imgui();


            if(take_screenshot)
            {
                renderer_record_screenshot(&renderer, cmd);
            }
            image_transition_swapchain(renderer.frames[renderer.current_frame].cmdbuf, &renderer.swapchain,
                                       VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0);
            flush_barriers(cmd);

            vk_cmd_end(renderer.frames[renderer.current_frame].cmdbuf);
            TracyCZoneEnd(record_cmd_zone);


            TracyCZoneN(submit_zone, "Submit + Present", 1);
            submit_frame(&renderer);
            TracyCZoneEnd(submit_zone);

            if(take_screenshot)
            {
                renderer_save_screenshot(&renderer);

                take_screenshot = false;
            }

            TracyCZoneEnd(frame_loop_zone);
        }
    }


    printf(" renderer size is %zu", sizeof(Renderer));

    renderer_destroy(&renderer);
    return 0;
}
