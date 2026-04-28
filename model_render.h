#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "renderer.h"
#include "model_assets.h"
#include "model_instances.h"

// Render queue and submission
// This module handles:
//  - Queueing draw requests
//  - Sorting and grouping by state
//  - Indirect command building
//  - GPU submission and barriers

bool model_render_init(uint32_t max_draws);
void model_render_shutdown(void);

// Queue a draw call (can be called multiple times per frame)
// Uses model instance handle which contains transform/color
bool model_render_queue(ModelInstanceHandle instance);

// Per-frame preparation
// Call after model_instances frame state is updated
// Builds and uploads indirect commands
bool model_render_prepare_frame(const Camera* camera, VkCommandBuffer cmd);

// Submit all queued draws to command buffer
// Must call model_render_prepare_frame first
void model_render_draw_queued(VkCommandBuffer cmd);

// Clear queue for next frame
void model_render_flush_frame(void);
