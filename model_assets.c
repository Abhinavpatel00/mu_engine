#include "model_assets.h"

#include <stdlib.h>
#include <string.h>

#include "gltf_gpu_mesh.h"

// TODO: This module owns mesh loading and GPU asset lifetime.
// For now, it will wrap the existing gltf_gpu_mesh code.
// Gradual migration will move loading logic here.

static struct
{
	bool initialized;
} g_assets_system = {0};

bool model_assets_init(uint32_t max_models)
{
	if(max_models == 0)
		return false;

	memset(&g_assets_system, 0, sizeof(g_assets_system));
	g_assets_system.initialized = true;

	// TODO: Initialize model pools and GPU allocators
	return true;
}

void model_assets_shutdown(void)
{
	if(g_assets_system.initialized)
	{
		// TODO: Free all loaded models
		g_assets_system.initialized = false;
	}
}

bool model_assets_load_meshx(const char* path, ModelHandle* out_model)
{
	if(!path || !out_model)
		return false;

	// TODO: Parse MeshX file, create GPU buffers
	// For now: placeholder
	return false;
}

bool model_assets_find_or_load_meshx(const char* path, ModelHandle* out_model)
{
	if(!path || !out_model)
		return false;

	// TODO: Check cache, load if not present
	return model_assets_load_meshx(path, out_model);
}

void model_assets_unload(ModelHandle model)
{
	if(model == MODEL_HANDLE_INVALID)
		return;

	// TODO: Free GPU buffers, textures, clip metadata
}

bool model_assets_is_valid(ModelHandle model)
{
	return model != MODEL_HANDLE_INVALID;
}

bool model_assets_has_skeleton(ModelHandle model)
{
	if(!model_assets_is_valid(model))
		return false;

	// TODO: Check model metadata
	return false;
}

bool model_assets_has_skinning(ModelHandle model)
{
	if(!model_assets_is_valid(model))
		return false;

	// TODO: Check model metadata
	return false;
}

bool model_assets_has_animations(ModelHandle model)
{
	if(!model_assets_is_valid(model))
		return false;

	// TODO: Check clip count
	return false;
}

uint32_t model_assets_animation_count(ModelHandle model)
{
	if(!model_assets_is_valid(model))
		return 0;

	// TODO: Return clip count
	return 0;
}

uint32_t model_assets_find_clip(ModelHandle model, const char* name)
{
	if(!model_assets_is_valid(model) || !name)
		return ANIMATION_CLIP_INVALID;

	// TODO: Search clips by name
	return ANIMATION_CLIP_INVALID;
}

const char* model_assets_clip_name(ModelHandle model, uint32_t clip_id)
{
	if(!model_assets_is_valid(model))
		return NULL;

	// TODO: Return clip name
	return NULL;
}

float model_assets_clip_duration(ModelHandle model, uint32_t clip_id)
{
	if(!model_assets_is_valid(model))
		return 0.0f;

	// TODO: Return clip duration
	return 0.0f;
}

bool model_assets_upload_pending(VkCommandBuffer cmd)
{
	if(!cmd)
		return false;

	// TODO: Replay pending GPU uploads with barriers
	return true;
}

const char* model_assets_debug_path(ModelHandle model)
{
	if(!model_assets_is_valid(model))
		return NULL;

	// TODO: Return debug path for debugging
	return NULL;
}
