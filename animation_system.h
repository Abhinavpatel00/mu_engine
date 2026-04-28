#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "model_instances.h"
#include "model_assets.h"
#include "gltf_gpu_mesh.h"

// Animation and skinning system
// This module handles:
//  - Clip playback state
//  - Animation layer blending
//  - Palette generation
//  - Dirty flag propagation
//  - Compute shader skinning dispatch

// Explicit layer state for blending (better than toy timer)
typedef struct AnimLayer
{
	AnimationClipHandle current;
	AnimationClipHandle target;
	float time;
	float target_time;
	float blend_t;
	float blend_duration;
	float speed;
	uint32_t flags;  // ANIM_LOOP, ANIM_PLAY_ONCE, etc
} AnimLayer;

bool animation_system_init(void);
void animation_system_shutdown(void);

// Playback control
bool animation_system_play(ModelInstanceHandle instance, ModelHandle model, AnimationClipHandle clip, AnimationPlaybackMode mode);
bool animation_system_play_by_name(ModelInstanceHandle instance, ModelHandle model, const char* clip_name, AnimationPlaybackMode mode);
void animation_system_stop(ModelInstanceHandle instance);
void animation_system_pause(ModelInstanceHandle instance, bool paused);

// Blending (explicit, not magical side effects)
bool animation_system_blend_to(ModelInstanceHandle instance, ModelHandle model, AnimationClipHandle target_clip, float blend_duration, AnimationPlaybackMode mode);
bool animation_system_blend_to_by_name(ModelInstanceHandle instance, ModelHandle model, const char* clip_name, float blend_duration, AnimationPlaybackMode mode);

// Per-frame update
// Call once per frame before skinning dispatch
void animation_system_update_frame(float dt_sec);

// Skinning dispatch
// Call after animation_system_update_frame to compute deformed geometry
// Uses batched compute dispatch instead of per-instance
void animation_system_prepare_frame(VkCommandBuffer cmd);

// Debug visualization
void animation_debug_set_mode(uint32_t mode);
void animation_debug_draw(ModelInstanceHandle instance);
