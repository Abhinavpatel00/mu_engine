#include "animation_system.h"

#include <stdlib.h>
#include <string.h>

// TODO: This file will coordinate animation playback, blending, and skinning.
// For now, it's a stub that coordinates with model_instances.c

static struct
{
	bool initialized;
	float frame_dt;
} g_animation_system = {0};

bool animation_system_init(void)
{
	memset(&g_animation_system, 0, sizeof(g_animation_system));
	g_animation_system.initialized = true;
	return true;
}

void animation_system_shutdown(void)
{
	g_animation_system.initialized = false;
}

bool animation_system_play(ModelInstanceHandle instance, ModelHandle model, AnimationClipHandle clip, AnimationPlaybackMode mode)
{
	(void)instance;
	(void)model;
	(void)clip;
	(void)mode;
	// TODO: Implement animation playback
	// For now: just set hot state flags
	if(model_instances_is_valid(instance))
	{
		model_instances_set_playing(instance, true);
		model_instances_set_looping(instance, mode != ANIM_PLAY_ONCE);
		model_instances_set_time(instance, 0.0f);
		return true;
	}
	return false;
}

bool animation_system_play_by_name(ModelInstanceHandle instance, ModelHandle model, const char* clip_name, AnimationPlaybackMode mode)
{
	(void)clip_name;
	// TODO: Resolve clip name to ID, then call animation_system_play
	return animation_system_play(instance, model, 0, mode);
}

void animation_system_stop(ModelInstanceHandle instance)
{
	if(model_instances_is_valid(instance))
	{
		model_instances_set_playing(instance, false);
		model_instances_set_time(instance, 0.0f);
	}
}

void animation_system_pause(ModelInstanceHandle instance, bool paused)
{
	if(model_instances_is_valid(instance))
		model_instances_set_paused(instance, paused);
}

bool animation_system_blend_to(ModelInstanceHandle instance, ModelHandle model, AnimationClipHandle target_clip, float blend_duration, AnimationPlaybackMode mode)
{
	(void)blend_duration;
	// TODO: Implement proper blending with AnimLayer
	// For now: just switch to target clip
	return animation_system_play(instance, model, target_clip, mode);
}

bool animation_system_blend_to_by_name(ModelInstanceHandle instance, ModelHandle model, const char* clip_name, float blend_duration, AnimationPlaybackMode mode)
{
	(void)clip_name;
	(void)blend_duration;
	// TODO: Resolve clip name, then blend
	return animation_system_play(instance, model, 0, mode);
}

void animation_system_update_frame(float dt_sec)
{
	g_animation_system.frame_dt = dt_sec;

	// Update animation times for all active instances using sparse-set iteration
	uint32_t count = 0;
	ModelInstanceHandle* ids = model_instances_get_active_ids(&count);
	ModelInstanceHot* hot = model_instances_get_active_hot(&count);

	if(ids && hot)
	{
		for(uint32_t i = 0; i < count; ++i)
		{
			// hot[i] corresponds to instance ids[i]
			if(hot[i].active && (hot[i].anim_flags & 1))  // PLAYING flag
			{
				// Simple playback: just advance time
				hot[i].anim_time += hot[i].anim_speed * dt_sec;
				// Mark palette dirty to trigger recompute during frame prep
				hot[i].palette_dirty = true;
			}
		}
	}
}
