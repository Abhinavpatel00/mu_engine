#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "renderer.h"
#include "gltf_gpu_mesh.h"  // ModelHandle, ModelInstanceHandle, etc.

// Instance state split by update frequency for cache efficiency

// Hot state: updated during animation frame (every frame potentially)
typedef struct ModelInstanceHot
{
	ModelHandle model;
	float anim_time;
	float anim_speed;
	uint32_t anim_flags;  // playing, paused, loop, etc
	bool palette_dirty;
	bool active;
} ModelInstanceHot;

// Render state: updated rarely, read by draw queue
typedef struct ModelInstanceRender
{
	float transform[4][4];
	float color[4];
} ModelInstanceRender;

// Skin state: GPU buffers for skinning output
typedef struct ModelInstanceSkin
{
	BufferSlice skinned_vertex_buffer;
	BufferSlice palette_buffer;
} ModelInstanceSkin;

// Instance management with sparse-set for O(1) active iteration
// dense[] = packed active instance IDs
// sparse[] = ID -> dense index mapping
// active instances occupy dense[0..count-1]
// dead instances do not appear in dense[]

bool model_instances_init(uint32_t instance_capacity);
void model_instances_shutdown(void);

// Lifecycle
ModelInstanceHandle model_instances_create(ModelHandle model);
void model_instances_destroy(ModelInstanceHandle instance);

bool model_instances_is_valid(ModelInstanceHandle instance);
ModelHandle model_instances_get_model(ModelInstanceHandle instance);

// Hot state (animation)
bool model_instances_is_playing(ModelInstanceHandle instance);
bool model_instances_is_paused(ModelInstanceHandle instance);
bool model_instances_is_looping(ModelInstanceHandle instance);
float model_instances_get_time(ModelInstanceHandle instance);
float model_instances_get_speed(ModelInstanceHandle instance);

void model_instances_set_time(ModelInstanceHandle instance, float time);
void model_instances_set_speed(ModelInstanceHandle instance, float speed);
void model_instances_set_playing(ModelInstanceHandle instance, bool playing);
void model_instances_set_paused(ModelInstanceHandle instance, bool paused);
void model_instances_set_looping(ModelInstanceHandle instance, bool loop);

// Render state (transform, color)
void model_instances_set_transform(ModelInstanceHandle instance, const float matrix[4][4]);
void model_instances_get_transform(ModelInstanceHandle instance, float out_matrix[4][4]);
void model_instances_set_color(ModelInstanceHandle instance, const float color[4]);

// Skin state (GPU buffers)
VkDeviceAddress model_instances_get_skinned_buffer(ModelInstanceHandle instance);
VkDeviceAddress model_instances_get_palette_buffer(ModelInstanceHandle instance);
void model_instances_set_palette_dirty(ModelInstanceHandle instance, bool dirty);

// Sparse-set iteration: get pointers to densely packed hot/render/skin arrays
// Returns pointer to array of instance IDs that are active, length = *count
// Use these for efficient per-frame updates
ModelInstanceHandle* model_instances_get_active_ids(uint32_t* count);
ModelInstanceHot* model_instances_get_active_hot(uint32_t* count);
ModelInstanceRender* model_instances_get_active_render(uint32_t* count);
ModelInstanceSkin* model_instances_get_active_skin(uint32_t* count);
