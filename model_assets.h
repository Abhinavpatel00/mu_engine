#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "renderer.h"
#include "gltf_gpu_mesh.h"  // ModelHandle, MODEL_HANDLE_INVALID, etc.

// Asset ownership and GPU lifetime
// This module is responsible for:
//  - Mesh import (MeshX, GLTF)
//  - GPU buffer allocation and lifetime
//  - Material textures and GPU packing
//  - Animation clip metadata storage
//  - Asset teardown

bool model_assets_init(uint32_t max_models);
void model_assets_shutdown(void);

bool model_assets_load_meshx(const char* path, ModelHandle* out_model);
bool model_assets_find_or_load_meshx(const char* path, ModelHandle* out_model);

void model_assets_unload(ModelHandle model);
bool model_assets_is_valid(ModelHandle model);
bool model_assets_has_skeleton(ModelHandle model);
bool model_assets_has_skinning(ModelHandle model);
bool model_assets_has_animations(ModelHandle model);

uint32_t model_assets_animation_count(ModelHandle model);
uint32_t model_assets_find_clip(ModelHandle model, const char* name);
const char* model_assets_clip_name(ModelHandle model, uint32_t clip_id);
float model_assets_clip_duration(ModelHandle model, uint32_t clip_id);

// Internal: upload any pending GPU transfers
bool model_assets_upload_pending(VkCommandBuffer cmd);

// Debug
const char* model_assets_debug_path(ModelHandle model);
