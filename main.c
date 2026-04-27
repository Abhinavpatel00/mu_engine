#include "renderer.h"
#include "passes.h"
#include "gltf_gpu_mesh.h"
#include "tinytypes.h"

static const char* GLTF_MODEL_PATHS[] = {
    "gameassets/Barbarian.meshx",
    "gameassets/blockychar/character-a.meshx",
};

#define GLTF_MODEL_COUNT (sizeof(GLTF_MODEL_PATHS) / sizeof(GLTF_MODEL_PATHS[0]))


int main(void)
{
    graphics_init();

    if(!model_api_init(256, 8192))
    {
        renderer_destroy(&renderer);
        return 1;
    }

    Camera cam = {0};
    camera_defaults_3d(&cam);
    camera3d_set_position(&cam, 0.0f, 0.6f, 4.0f);
    camera3d_set_rotation_yaw_pitch(&cam, 0.0f, 0.0f);

    while(!glfwWindowShouldClose(renderer.window))
    {
        TracyCFrameMark;
        pipeline_rebuild(&renderer);
        frame_start(&renderer, &cam);

        model_api_begin_frame(&cam);
        {
            float spacing = 1.6f;
            float start_x = -0.5f * spacing * (float)(GLTF_MODEL_COUNT - 1);

            forEach(i, GLTF_MODEL_COUNT)
            {
                float model[4][4];
                glm_mat4_identity(model);
                glm_translate(model, (vec3){start_x + spacing * (float)i, 0.0f, 0.0f});
                draw_model(GLTF_MODEL_PATHS[i], model);
            }
        }

        VkCommandBuffer cmd        = renderer.frames[renderer.current_frame].cmdbuf;
        GpuProfiler*    frame_prof = &renderer.gpuprofiler[renderer.current_frame];

        vk_cmd_begin(cmd, false);
        gpu_profiler_begin_frame(frame_prof, cmd);
        {
            {
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, renderer.bindless_system.pipeline_layout,
                                        0, 1, &renderer.bindless_system.set, 0, NULL);
                vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, renderer.bindless_system.pipeline_layout,
                                        0, 1, &renderer.bindless_system.set, 0, NULL);

                rt_transition_all(cmd, &renderer.depth[renderer.swapchain.current_image], VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                                  VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
                                  VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT);

                rt_transition_all(cmd, &renderer.hdr_color[renderer.swapchain.current_image], VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                  VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT);

                image_transition_swapchain(cmd, &renderer.swapchain, VK_IMAGE_LAYOUT_GENERAL,
                                           VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT, VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT);
                flush_barriers(cmd);

                model_api_prepare_frame(cmd);
            }


            VkRenderingAttachmentInfo color = {
                .sType            = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView        = renderer.hdr_color[renderer.swapchain.current_image].view,
                .imageLayout      = renderer.hdr_color[renderer.swapchain.current_image].mip_states[0].layout,
                .loadOp           = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp          = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue.color = {{0.10f, 0.12f, 0.15f, 1.0f}},
            };

            VkRenderingAttachmentInfo depth = {
                .sType                   = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
                .imageView               = renderer.depth[renderer.swapchain.current_image].view,
                .imageLayout             = VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL,
                .loadOp                  = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp                 = VK_ATTACHMENT_STORE_OP_STORE,
                .clearValue.depthStencil = {0.0f, 0},
            };

            VkRenderingInfo rendering = {
                .sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
                .renderArea.extent    = renderer.swapchain.extent,
                .layerCount           = 1,
                .colorAttachmentCount = 1,
                .pColorAttachments    = &color,
                .pDepthAttachment     = &depth,
            };

            vkCmdBeginRendering(cmd, &rendering);
            model_api_draw_queued(cmd);
            vkCmdEndRendering(cmd);

            post_pass();
            pass_smaa();
            pass_ldr_to_swapchain();

            image_transition_swapchain(cmd, &renderer.swapchain, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                       VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, 0);
            flush_barriers(cmd);
        }
        vk_cmd_end(cmd);

        submit_frame(&renderer);
    }

    model_api_shutdown();
    renderer_destroy(&renderer);
    return 0;
}
