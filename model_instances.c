#include "model_instances.h"

#include <stdlib.h>
#include <string.h>

#include "helpers.h"

// Internal structure for sparse-set instance tracking
typedef struct ModelInstanceSystem_Internal
{
	// Sparse-set for active instance tracking (O(1) add/remove/iterate)
	uint32_t* sparse;   // sparse[id] -> dense index
	uint32_t* dense;    // dense[i] -> instance id
	uint32_t dense_count;
	uint32_t capacity;

	// Split storage by update frequency (cache-friendly)
	ModelInstanceHot* hot;       // Updated during animation
	ModelInstanceRender* render; // Read by draw queue
	ModelInstanceSkin* skin;     // GPU buffers for skinning

	// ID pool for stable handles
	mu_id_pool id_pool;

	// Frame state
	float frame_dt;
} ModelInstanceSystem_Internal;

static ModelInstanceSystem_Internal g_instance_system = {0};

// ============================================================================
// Sparse-Set Operations (O(1) add, remove, iterate)
// ============================================================================

static bool sparse_set_add(uint32_t id)
{
	if(g_instance_system.sparse[id] != UINT32_MAX)
		return false; // Already active
	
	if(g_instance_system.dense_count >= g_instance_system.capacity)
		return false; // At capacity

	// Append to dense array
	uint32_t dense_idx = g_instance_system.dense_count++;
	g_instance_system.dense[dense_idx] = id;
	g_instance_system.sparse[id] = dense_idx;
	return true;
}

static bool sparse_set_remove(uint32_t id)
{
	if(g_instance_system.sparse[id] == UINT32_MAX)
		return false; // Already inactive

	uint32_t dense_idx = g_instance_system.sparse[id];
	if(dense_idx >= g_instance_system.dense_count)
		return false;

	// Swap with last element
	uint32_t last_id = g_instance_system.dense[g_instance_system.dense_count - 1];
	g_instance_system.dense[dense_idx] = last_id;
	g_instance_system.sparse[last_id] = dense_idx;

	g_instance_system.dense_count--;

	// Mark as inactive
	g_instance_system.sparse[id] = UINT32_MAX;
	return true;
}

static bool sparse_set_is_active(uint32_t id)
{
	return id < g_instance_system.capacity && g_instance_system.sparse[id] != UINT32_MAX;
}

// ============================================================================
// Module Interface (Public API)
// ============================================================================

bool model_instances_init(uint32_t instance_capacity)
{
	if(instance_capacity == 0)
		return false;

	memset(&g_instance_system, 0, sizeof(g_instance_system));

	// Initialize sparse-set
	g_instance_system.capacity = instance_capacity;
	g_instance_system.sparse = (uint32_t*)calloc(instance_capacity, sizeof(uint32_t));
	g_instance_system.dense = (uint32_t*)calloc(instance_capacity, sizeof(uint32_t));
	g_instance_system.hot = (ModelInstanceHot*)calloc(instance_capacity, sizeof(ModelInstanceHot));
	g_instance_system.render = (ModelInstanceRender*)calloc(instance_capacity, sizeof(ModelInstanceRender));
	g_instance_system.skin = (ModelInstanceSkin*)calloc(instance_capacity, sizeof(ModelInstanceSkin));

	if(!g_instance_system.sparse || !g_instance_system.dense || !g_instance_system.hot
	   || !g_instance_system.render || !g_instance_system.skin)
	{
		model_instances_shutdown();
		return false;
	}

	// Initialize sparse array to "not present"
	for(uint32_t i = 0; i < instance_capacity; ++i)
	{
		g_instance_system.sparse[i] = UINT32_MAX;
	}

	// Initialize ID pool
	mu_id_pool_init(&g_instance_system.id_pool, instance_capacity);

	g_instance_system.dense_count = 0;
	g_instance_system.frame_dt = 0.0f;

	return true;
}

void model_instances_shutdown(void)
{
	if(g_instance_system.id_pool.ranges)
		mu_id_pool_deinit(&g_instance_system.id_pool);

	free(g_instance_system.sparse);
	free(g_instance_system.dense);
	free(g_instance_system.hot);
	free(g_instance_system.render);
	free(g_instance_system.skin);

	memset(&g_instance_system, 0, sizeof(g_instance_system));
}

// ============================================================================
// Lifecycle
// ============================================================================

ModelInstanceHandle model_instances_create(ModelHandle model)
{
	if(!model)
		return MODEL_INSTANCE_HANDLE_INVALID;

	uint32_t id = MODEL_INSTANCE_HANDLE_INVALID;
	if(!mu_id_pool_create_id(&g_instance_system.id_pool, &id))
		return MODEL_INSTANCE_HANDLE_INVALID;

	if(!sparse_set_add(id))
	{
		mu_id_pool_destroy_id(&g_instance_system.id_pool, id);
		return MODEL_INSTANCE_HANDLE_INVALID;
	}

	// Initialize hot state
	ModelInstanceHot* h = &g_instance_system.hot[id];
	h->model = model;
	h->anim_time = 0.0f;
	h->anim_speed = 1.0f;
	h->anim_flags = 1; // ANIM_LOOP
	h->palette_dirty = true;
	h->active = true;

	// Initialize render state
	ModelInstanceRender* r = &g_instance_system.render[id];
	{
		// Identity matrix
		memset(r->transform, 0, sizeof(r->transform));
		r->transform[0][0] = 1.0f;
		r->transform[1][1] = 1.0f;
		r->transform[2][2] = 1.0f;
		r->transform[3][3] = 1.0f;

		// White color
		r->color[0] = 1.0f;
		r->color[1] = 1.0f;
		r->color[2] = 1.0f;
		r->color[3] = 1.0f;
	}

	// Skin state initialized to zeros (caller allocates buffers if needed)
	memset(&g_instance_system.skin[id], 0, sizeof(ModelInstanceSkin));

	return (ModelInstanceHandle)id;
}

void model_instances_destroy(ModelInstanceHandle instance)
{
	if(!sparse_set_is_active(instance))
		return;

	// Mark as inactive
	g_instance_system.hot[instance].active = false;
	sparse_set_remove(instance);

	// Clear all data
	memset(&g_instance_system.hot[instance], 0, sizeof(ModelInstanceHot));
	memset(&g_instance_system.render[instance], 0, sizeof(ModelInstanceRender));
	memset(&g_instance_system.skin[instance], 0, sizeof(ModelInstanceSkin));

	mu_id_pool_destroy_id(&g_instance_system.id_pool, instance);
}

bool model_instances_is_valid(ModelInstanceHandle instance)
{
	return sparse_set_is_active(instance) && g_instance_system.hot[instance].active;
}

ModelHandle model_instances_get_model(ModelInstanceHandle instance)
{
	if(!model_instances_is_valid(instance))
		return MODEL_HANDLE_INVALID;
	return g_instance_system.hot[instance].model;
}

// ============================================================================
// Hot State (Animation)
// ============================================================================

bool model_instances_is_playing(ModelInstanceHandle instance)
{
	if(!model_instances_is_valid(instance))
		return false;
	return (g_instance_system.hot[instance].anim_flags & 1) != 0;
}

bool model_instances_is_paused(ModelInstanceHandle instance)
{
	if(!model_instances_is_valid(instance))
		return false;
	return (g_instance_system.hot[instance].anim_flags & 2) != 0;
}

bool model_instances_is_looping(ModelInstanceHandle instance)
{
	if(!model_instances_is_valid(instance))
		return false;
	return (g_instance_system.hot[instance].anim_flags & 4) != 0;
}

float model_instances_get_time(ModelInstanceHandle instance)
{
	if(!model_instances_is_valid(instance))
		return 0.0f;
	return g_instance_system.hot[instance].anim_time;
}

float model_instances_get_speed(ModelInstanceHandle instance)
{
	if(!model_instances_is_valid(instance))
		return 1.0f;
	return g_instance_system.hot[instance].anim_speed;
}

void model_instances_set_time(ModelInstanceHandle instance, float time)
{
	if(!model_instances_is_valid(instance))
		return;
	g_instance_system.hot[instance].anim_time = time < 0.0f ? 0.0f : time;
}

void model_instances_set_speed(ModelInstanceHandle instance, float speed)
{
	if(!model_instances_is_valid(instance))
		return;
	g_instance_system.hot[instance].anim_speed = speed;
}

void model_instances_set_playing(ModelInstanceHandle instance, bool playing)
{
	if(!model_instances_is_valid(instance))
		return;
	if(playing)
		g_instance_system.hot[instance].anim_flags |= 1;
	else
		g_instance_system.hot[instance].anim_flags &= ~1;
}

void model_instances_set_paused(ModelInstanceHandle instance, bool paused)
{
	if(!model_instances_is_valid(instance))
		return;
	if(paused)
		g_instance_system.hot[instance].anim_flags |= 2;
	else
		g_instance_system.hot[instance].anim_flags &= ~2;
}

void model_instances_set_looping(ModelInstanceHandle instance, bool loop)
{
	if(!model_instances_is_valid(instance))
		return;
	if(loop)
		g_instance_system.hot[instance].anim_flags |= 4;
	else
		g_instance_system.hot[instance].anim_flags &= ~4;
}

// ============================================================================
// Render State
// ============================================================================

void model_instances_set_transform(ModelInstanceHandle instance, const float matrix[4][4])
{
	if(!model_instances_is_valid(instance) || !matrix)
		return;
	memcpy(g_instance_system.render[instance].transform, matrix, sizeof(float[4][4]));
}

void model_instances_get_transform(ModelInstanceHandle instance, float out_matrix[4][4])
{
	if(!model_instances_is_valid(instance) || !out_matrix)
		return;
	memcpy(out_matrix, g_instance_system.render[instance].transform, sizeof(float[4][4]));
}

void model_instances_set_color(ModelInstanceHandle instance, const float color[4])
{
	if(!model_instances_is_valid(instance) || !color)
		return;
	memcpy(g_instance_system.render[instance].color, color, sizeof(float[4]));
}

// ============================================================================
// Skin State
// ============================================================================

VkDeviceAddress model_instances_get_skinned_buffer(ModelInstanceHandle instance)
{
	if(!model_instances_is_valid(instance))
		return 0;
	BufferSlice* slice = &g_instance_system.skin[instance].skinned_vertex_buffer;
	if(slice->buffer == VK_NULL_HANDLE)
		return 0;
	// Would need access to renderer to compute device address
	// For now, return 0 - this will be filled in by animation system
	return 0;
}

VkDeviceAddress model_instances_get_palette_buffer(ModelInstanceHandle instance)
{
	if(!model_instances_is_valid(instance))
		return 0;
	BufferSlice* slice = &g_instance_system.skin[instance].palette_buffer;
	if(slice->buffer == VK_NULL_HANDLE)
		return 0;
	return 0;
}

void model_instances_set_palette_dirty(ModelInstanceHandle instance, bool dirty)
{
	if(!model_instances_is_valid(instance))
		return;
	g_instance_system.hot[instance].palette_dirty = dirty;
}

// ============================================================================
// Sparse-Set Iteration (Dense arrays for efficient per-frame updates)
// ============================================================================

ModelInstanceHandle* model_instances_get_active_ids(uint32_t* count)
{
	if(!count)
		return NULL;
	*count = g_instance_system.dense_count;
	return g_instance_system.dense_count > 0 ? g_instance_system.dense : NULL;
}

ModelInstanceHot* model_instances_get_active_hot(uint32_t* count)
{
	if(!count)
		return NULL;
	*count = g_instance_system.dense_count;
	return g_instance_system.hot;
}

ModelInstanceRender* model_instances_get_active_render(uint32_t* count)
{
	if(!count)
		return NULL;
	*count = g_instance_system.dense_count;
	return g_instance_system.render;
}

ModelInstanceSkin* model_instances_get_active_skin(uint32_t* count)
{
	if(!count)
		return NULL;
	*count = g_instance_system.dense_count;
	return g_instance_system.skin;
}
