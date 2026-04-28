#include "model_render.h"

#include <stdlib.h>
#include <string.h>

// TODO: This file will handle render queue, sorting, indirect building.
// For now, it's a stub that will coordinate with existing render code.

static struct
{
	uint32_t max_draws;
	uint32_t current_draw_count;
	bool prepared;
} g_render_system = {0};

bool model_render_init(uint32_t max_draws)
{
	if(max_draws == 0)
		return false;

	memset(&g_render_system, 0, sizeof(g_render_system));
	g_render_system.max_draws = max_draws;
	g_render_system.current_draw_count = 0;
	g_render_system.prepared = false;

	return true;
}

void model_render_shutdown(void)
{
	memset(&g_render_system, 0, sizeof(g_render_system));
}

bool model_render_queue(ModelInstanceHandle instance)
{
	if(!model_instances_is_valid(instance))
		return false;

	if(g_render_system.current_draw_count >= g_render_system.max_draws)
		return false;

	g_render_system.current_draw_count++;
	return true;
}

bool model_render_prepare_frame(const Camera* camera, VkCommandBuffer cmd)
{
	if(!camera || !cmd)
		return false;

	// TODO: Sort, group, and build indirect commands
	// For now: mark as prepared
	g_render_system.prepared = true;
	return true;
}

void model_render_draw_queued(VkCommandBuffer cmd)
{
	if(!cmd || !g_render_system.prepared)
		return;

	// TODO: Issue vkCmdDrawIndirect commands
	// For now: just placeholder
}

void model_render_flush_frame(void)
{
	g_render_system.current_draw_count = 0;
	g_render_system.prepared = false;
}
