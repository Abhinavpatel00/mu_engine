#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "renderer.h"

typedef uint32_t ModelHandle;
typedef uint32_t ModelInstanceHandle;
typedef uint32_t AnimationClipHandle;
typedef uint32_t SkeletonHandle;
typedef uint32_t SkinHandle;

#define MODEL_HANDLE_INVALID UINT32_MAX
#define MODEL_INSTANCE_HANDLE_INVALID UINT32_MAX
#define ANIMATION_CLIP_INVALID UINT32_MAX
#define SKELETON_HANDLE_INVALID UINT32_MAX
#define SKIN_HANDLE_INVALID UINT32_MAX

typedef enum AnimationPlaybackMode
{
	ANIM_PLAY_ONCE = 0,
	ANIM_LOOP,
	ANIM_PINGPONG,
} AnimationPlaybackMode;

typedef struct AnimationState
{
	AnimationClipHandle clip;
	float               time;
	float               speed;
	float               weight;
	bool                playing;
	bool                paused;
	bool                loop;
} AnimationState;

typedef enum AnimationDebugMode
{
	ANIM_DEBUG_OFF = 0,
	ANIM_DEBUG_SKELETON,
	ANIM_DEBUG_JOINT_AXES,
	ANIM_DEBUG_WEIGHTS,
	ANIM_DEBUG_COMPUTE_SKINNING,
} AnimationDebugMode;

bool model_api_init(uint32_t max_models, uint32_t instance_capacity);
void model_api_shutdown(void);

bool model_api_load_meshx(const char* path, ModelHandle* out_model);
bool model_api_find_or_load_meshx(const char* path, ModelHandle* out_model);
void model_api_unload(ModelHandle model);

bool model_api_is_valid(ModelHandle model);
bool model_api_has_skeleton(ModelHandle model);
bool model_api_has_skinning(ModelHandle model);
bool model_api_has_animations(ModelHandle model);

uint32_t model_api_animation_count(ModelHandle model);
AnimationClipHandle model_api_find_clip(ModelHandle model, const char* name);
const char* model_api_clip_name(ModelHandle model, AnimationClipHandle clip);
float model_api_clip_duration(ModelHandle model, AnimationClipHandle clip);

ModelInstanceHandle model_instance_create(ModelHandle model);
void model_instance_destroy(ModelInstanceHandle instance);

bool model_instance_is_valid(ModelInstanceHandle instance);
ModelHandle model_instance_model(ModelInstanceHandle instance);

void model_instance_set_transform(ModelInstanceHandle instance, const float model_matrix[4][4]);
void model_instance_get_transform(ModelInstanceHandle instance, float out_model_matrix[4][4]);
void model_instance_set_color(ModelInstanceHandle instance, const float color[4]);

bool model_instance_play(ModelInstanceHandle instance, AnimationClipHandle clip, AnimationPlaybackMode mode);
bool model_instance_play_by_name(ModelInstanceHandle instance, const char* clip_name, AnimationPlaybackMode mode);
void model_instance_stop(ModelInstanceHandle instance);
void model_instance_pause(ModelInstanceHandle instance, bool paused);
void model_instance_set_time(ModelInstanceHandle instance, float time_sec);
void model_instance_set_speed(ModelInstanceHandle instance, float speed);
void model_instance_set_loop(ModelInstanceHandle instance, bool enabled);
AnimationState model_instance_animation_state(ModelInstanceHandle instance);

bool model_instance_blend_to(ModelInstanceHandle instance, AnimationClipHandle target_clip, float blend_time_sec, AnimationPlaybackMode mode);
bool model_instance_blend_to_by_name(ModelInstanceHandle instance, const char* clip_name, float blend_time_sec, AnimationPlaybackMode mode);

void animation_system_begin_frame(float dt_sec);
void animation_system_update_instance(ModelInstanceHandle instance);
void animation_system_prepare_frame(VkCommandBuffer cmd);

bool model_instance_draw(ModelInstanceHandle instance);

void animation_debug_set_mode(AnimationDebugMode mode);
void animation_debug_draw(ModelInstanceHandle instance);

void model_api_begin_frame(const Camera* cam);
bool model_api_draw(ModelHandle model, const float model_matrix[4][4], const float color[4]);
bool draw3d(ModelHandle model, const float model_matrix[4][4], const float color[4]);
bool draw_model(const char* path, const float model_matrix[4][4]);

void model_api_prepare_frame(VkCommandBuffer cmd);
void model_api_draw_queued(VkCommandBuffer cmd);
void model_api_flush_frame(VkCommandBuffer cmd);
