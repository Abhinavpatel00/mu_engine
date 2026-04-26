#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "renderer.h"

typedef uint32_t ModelHandle;

#define MODEL_HANDLE_INVALID UINT32_MAX

bool model_api_init(uint32_t max_models, uint32_t instance_capacity);
void model_api_shutdown(void);

bool model_api_load_meshx(const char* path, ModelHandle* out_model);
bool model_api_find_or_load_meshx(const char* path, ModelHandle* out_model);
bool model_api_load_gltf(const char* path, ModelHandle* out_model);
bool model_api_find_or_load_gltf(const char* path, ModelHandle* out_model);
void model_api_unload(ModelHandle model);

void model_api_begin_frame(const Camera* cam);
bool model_api_draw(ModelHandle model, const float model_matrix[4][4], const float color[4]);
bool draw3d(ModelHandle model, const float model_matrix[4][4], const float color[4]);
bool draw_model(const char* path, const float model_matrix[4][4]);

void model_api_prepare_frame(VkCommandBuffer cmd);
void model_api_draw_queued(VkCommandBuffer cmd);
void model_api_flush_frame(VkCommandBuffer cmd);
