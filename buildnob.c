
#define NOB_IMPLEMENTATION
#include "external/nob/nob.h"
#include <sys/stat.h>


int main(int argc, char** argv)
{
    NOB_GO_REBUILD_URSELF(argc, argv);

    Nob_File_Paths object_files = {0};
    nob_mkdir_if_not_exists("build");

    const char* out = "build/app";

    const char* common_cflags[] = {

        "-std=gnu99",
        "-ggdb",
        "-DDEBUG",
// "-fverbose-asm"
    };

    const char* common_cxxflags[] = {
        "-std=gnu++17",
        "-w",
        "-fno-common",
        "-DIMGUI_IMPL_VULKAN_NO_PROTOTYPES",
        "-DIMGUI_IMPL_API=extern \"C\"",
        "-Iexternal/cimgui",
        "-Iexternal/cimgui/imgui",
        "-Iexternal/cimgui/imgui/backends",
    };

    const char* c_sources[] = {"main.c", "ext.c", "vk.c", "helpers.c", "cachestuff.c","offset_allocator.c"};

    const char* cpp_sources[] = {
        "vma.cpp",
    };

    bool any_rebuilt = false;

    // --------------------
    // Compile C
    // --------------------
    for(size_t i = 0; i < NOB_ARRAY_LEN(c_sources); i++)
    {
        const char* src = c_sources[i];

        const char* dot = strrchr(src, '.');
        size_t      len = dot ? (size_t)(dot - src) : strlen(src);

        const char* obj = nob_temp_sprintf("build/%.*s.o", (int)len, src);

        nob_da_append(&object_files, obj);

        const char* inputs[] = {src};

        int rebuild = nob_needs_rebuild(obj, inputs, 1);
        if(rebuild < 0)
            return 1;

        if(rebuild == 0)
        {
            printf("[SKIP] %s\n", src);
            continue;
        }

        printf("[BUILD] %s\n", src);

        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "clang", "-c", src, "-o", obj);

        for(size_t j = 0; j < NOB_ARRAY_LEN(common_cflags); j++)
            nob_cmd_append(&cmd, common_cflags[j]);

        if(!nob_cmd_run_sync(cmd))
            return 1;
    }
    // --------------------
    // Compile C++
    // --------------------


    for(size_t i = 0; i < NOB_ARRAY_LEN(cpp_sources); i++)
    {
        const char* src = cpp_sources[i];

        const char* dot = strrchr(src, '.');
        size_t      len = dot ? (size_t)(dot - src) : strlen(src);

        const char* obj = nob_temp_sprintf("build/%.*s.o", (int)len, src);

        nob_da_append(&object_files, obj);

        const char* inputs[] = {src};

        int rebuild = nob_needs_rebuild(obj, inputs, 1);
        if(rebuild < 0)
            return 1;

        if(rebuild == 0)
        {
            printf("[SKIP] %s\n", src);
            continue;
        }

        printf("[BUILD] %s\n", src);

        Nob_Cmd cmd = {0};
        nob_cmd_append(&cmd, "c++", "-c", src, "-o", obj);

        for(size_t j = 0; j < NOB_ARRAY_LEN(common_cxxflags); j++)
            nob_cmd_append(&cmd, common_cxxflags[j]);

        if(!nob_cmd_run_sync(cmd))
            return 1;
    }


    int link_rebuild = nob_needs_rebuild(out, object_files.items, object_files.count);
    if(link_rebuild < 0)
        return 1;

    if(link_rebuild == 0)
    {
        printf("[SKIP LINK]\n");
        return 0;
    }

    printf("[LINK] %s\n", out);

    Nob_Cmd cmd = {0};
    nob_cmd_append(&cmd, "c++", "-o", out);

    for(size_t i = 0; i < object_files.count; i++)
        nob_cmd_append(&cmd, object_files.items[i]);

    nob_cmd_append(&cmd, "-lvulkan", "-lglfw", "-ldl", "-lpthread");

    if(!nob_cmd_run_sync(cmd))
        return 1;
    return 0;
}
