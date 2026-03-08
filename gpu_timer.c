
#include "gpu_timer.h"

#include <string.h>
#include <stdio.h>
#include <assert.h>


static uint32_t gpu_prof_write_stamp(VkCommandBuffer cmd, GpuProfiler* p, VkPipelineStageFlags2 stage)
{
    assert(p->cursor < p->capacity);

    uint32_t id = p->cursor++;

    vkCmdWriteTimestamp2(cmd, stage, p->pool, id);

    return id;
}


bool gpu_prof_init(GpuProfiler* p, VkDevice device, VkPhysicalDevice gpu, uint32_t max_queries)
{
    assert(p);
    assert(max_queries > 0);

    memset(p, 0, sizeof(*p));

    p->device   = device;
    p->capacity = max_queries;

    VkPhysicalDeviceProperties props;
    vkGetPhysicalDeviceProperties(gpu, &props);

    p->timestamp_period_ns = props.limits.timestampPeriod;

    VkQueryPoolCreateInfo info = {
        .sType      = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType  = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = max_queries,
    };

    return vkCreateQueryPool(device, &info, NULL, &p->pool) == VK_SUCCESS;
}


void gpu_prof_destroy(GpuProfiler* p)
{
    if(p->pool)
        vkDestroyQueryPool(p->device, p->pool, NULL);

    memset(p, 0, sizeof(*p));
}


void gpu_prof_begin_frame(VkCommandBuffer cmd, GpuProfiler* p)
{
    p->cursor      = 0;
    p->scope_count = 0;
    p->stack_top   = 0;

    vkCmdResetQueryPool(cmd, p->pool, 0, p->capacity);

    gpu_prof_scope_begin(cmd, p, "frame", VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
}


void gpu_prof_end_frame(VkCommandBuffer cmd, GpuProfiler* p)
{
    if(p->stack_top)
    {
        gpu_prof_scope_end(cmd, p, VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT);
    }
}


void gpu_prof_scope_begin(VkCommandBuffer cmd, GpuProfiler* p, const char* name, VkPipelineStageFlags2 stage)
{
    assert(p->scope_count < GPU_PROF_MAX_SCOPES);

    uint32_t id = p->scope_count++;

    GpuScope* s = &p->scopes[id];

    strncpy(s->name, name ? name : "scope", GPU_PROF_NAME_MAX - 1);

    s->name[GPU_PROF_NAME_MAX - 1] = 0;

    s->depth = p->stack_top;

    s->begin = gpu_prof_write_stamp(cmd, p, stage);

    s->end = UINT32_MAX;

    p->stack[p->stack_top++] = id;
}


void gpu_prof_scope_end(VkCommandBuffer cmd, GpuProfiler* p, VkPipelineStageFlags2 stage)
{
    assert(p->stack_top > 0);

    uint32_t id = p->stack[--p->stack_top];

    GpuScope* s = &p->scopes[id];

    s->end = gpu_prof_write_stamp(cmd, p, stage);
}


void gpu_prof_resolve(GpuProfiler* p)
{
    if(p->cursor == 0)
        return;

    VkResult r = vkGetQueryPoolResults(p->device, p->pool, 0, p->cursor, sizeof(uint64_t) * p->cursor, p->timestamps,
                                       sizeof(uint64_t), VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WAIT_BIT);

    if(r != VK_SUCCESS)
        return;

    p->resolved_count = 0;

    for(uint32_t i = 0; i < p->scope_count; i++)
    {
        GpuScope* s = &p->scopes[i];

        if(s->end == UINT32_MAX)
            continue;

        uint64_t a = p->timestamps[s->begin];

        uint64_t b = p->timestamps[s->end];

        double ns = (double)(b - a) * p->timestamp_period_ns;

        GpuResolvedScope* out = &p->resolved[p->resolved_count++];

        strcpy(out->name, s->name);

        out->depth = s->depth;

        out->us = (float)(ns / 1000.0);
    }
}


bool gpu_prof_get_us(GpuProfiler* p, const char* name, float* out)
{
    for(uint32_t i = 0; i < p->resolved_count; i++)
    {
        if(strcmp(p->resolved[i].name, name) == 0)
        {
            *out = p->resolved[i].us;
            return true;
        }
    }

    return false;
}


void gpu_prof_dump(GpuProfiler* p)
{
    for(uint32_t i = 0; i < p->resolved_count; i++)
    {
        GpuResolvedScope* s = &p->resolved[i];

        for(uint32_t d = 0; d < s->depth; d++)
            printf("  ");

        printf("%-16s %.3f us\n", s->name, s->us);
    }
}
