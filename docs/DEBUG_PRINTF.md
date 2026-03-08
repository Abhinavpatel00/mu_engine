# Debug Printf Support

This renderer now supports Vulkan Debug Printf for shader debugging, enabled via `VK_KHR_shader_non_semantic_info` (core in Vulkan 1.3).

## Features

- **Enable/Disable**: Control Debug Printf via `RendererDesc.enable_debug_printf`
- **Shader Support**: Works with GLSL, HLSL, and Slang shaders
- **Tool Integration**: View output in RenderDoc 1.14+ or Validation Layers

## How to Enable

### 1. In Your Application Code

Set `enable_debug_printf = true` in your `RendererDesc`:

```c
RendererDesc desc = {
    .app_name = "My App",
    // ... other settings ...
    .enable_debug_printf = true,  // Enable Debug Printf
};
```

### 2. In Your Shaders

#### GLSL Shaders

Add the extension and use `debugPrintfEXT()`:

```glsl
#version 450
#extension GL_EXT_debug_printf : enable

void main() {
    float myfloat = 3.1415f;
    debugPrintfEXT("My float is %f", myfloat);
    
    vec4 myvec = vec4(1.0, 2.0, 3.0, 4.0);
    debugPrintfEXT("My vector: %v4f", myvec);
}
```

#### HLSL/Slang Shaders

Use standard `printf()`:

```hlsl
void main() {
    float myfloat = 3.1415;
    printf("My float is %f", myfloat);
}
```

### 3. View the Output

#### Option A: RenderDoc (v1.14+)

1. Capture a frame in RenderDoc
2. Look for message counts in the Event Browser (e.g., "vkCmdDraw: 51 messages")
3. Click to view detailed output for each draw call

#### Option B: Validation Layers

**Using Environment Variables (Vulkan SDK 1.4.304+):**

```bash
# Enable Debug Printf only (disables normal validation)
export VK_LAYER_PRINTF_ONLY_PRESET=1

# Optional: Send output directly to stdout
export VK_LAYER_PRINTF_TO_STDOUT=1
```

**Using vkconfig:**

Enable the "Debug Printf Preset" and view output in the launcher window.

## Format Specifiers

The format string is more restricted than traditional printf:

### Basic Specifiers
- `%d`, `%i` - signed integer
- `%u` - unsigned integer  
- `%x`, `%X` - hexadecimal
- `%f`, `%F` - float
- `%e`, `%E` - scientific notation
- `%g`, `%G` - shortest representation
- `%a`, `%A` - hexadecimal float

### Vector Specifiers (GLSL only)
- `%v2f`, `%v3f`, `%v4f` - 2/3/4 component float vectors
- `%v2d`, `%v3d`, `%v4d` - 2/3/4 component int vectors
- `%v2u`, `%v3u`, `%v4u` - 2/3/4 component uint vectors

Vectors print with comma separators: `1.20, 2.20, 3.20, 4.20`

### Precision
Use precision modifiers: `%1.2f` for 2 decimal places

### Special
- `%%` - literal percent sign
- `%lu`, `%lx` - 64-bit unsigned/hex

### Not Supported
- No strings or chars
- No width specifiers
- No flags (+, -, #, 0, space)
- No length modifiers (except ul, lu, lx)

## Examples

### GLSL Examples

```glsl
float myfloat = 3.1415f;
vec4 floatvec = vec4(1.2f, 2.2f, 3.2f, 4.2f);
uint64_t bigvar = 0x2000000000000001ul;

debugPrintfEXT("Here's a float value to 2 decimals %1.2f", myfloat);
// Output: "Here's a float value to 2 decimals 3.14"

debugPrintfEXT("Here's a vector of floats %1.2v4f", floatvec);
// Output: "Here's a vector of floats 1.20, 2.20, 3.20, 4.20"

debugPrintfEXT("Unsigned long as decimal %lu and as hex 0x%lx", bigvar, bigvar);
// Output: "Unsigned long as decimal 2305843009213693953 and as hex 0x2000000000000001"
```

### HLSL/Slang Examples

```hlsl
float myfloat = 3.1415;
float3 myvec = float3(1.0, 2.0, 3.0);

printf("Float: %f\n", myfloat);
// Note: HLSL/Slang doesn't support %vNf format
printf("Vector: (%f, %f, %f)\n", myvec.x, myvec.y, myvec.z);
```

## Important Notes

### Performance Impact
- **Vertex Shaders**: Prints once per vertex (manageable for most meshes)
- **Fragment Shaders**: Prints once per fragment (millions of messages!)
  - Use conditionals to limit output: `if (gl_FragCoord.x < 10.0) { ... }`
  - Only enable for specific debugging sessions

### Memory Consumption
- Debug Printf consumes GPU device memory
- Many/large messages can exhaust memory
- Adjust buffer size via Validation Layer settings if needed

### Descriptor Set Usage
- Debug Printf consumes one descriptor set
- Won't work if your application uses all available descriptor sets

## Limitations

- **Vulkan Version**: Requires Vulkan 1.1 or greater
- **Extension**: `VK_KHR_shader_non_semantic_info` must be supported (core in Vulkan 1.3+)
- **Validation Layers**: Requires version 1.2.135.0 or later
- **RenderDoc**: Version 1.14 or later for Debug Printf support
- **Shader Compilation**: 
  - GLSL: Use `glslangValidator --target-env vulkan1.2`
  - HLSL: Use `dxc -spirv -fspv-target-env=vulkan1.2`
  - Slang: Use `slangc` with appropriate target

## Troubleshooting

### "VK_KHR_shader_non_semantic_info not available"
- Your GPU or driver doesn't support the extension
- Update your GPU drivers
- Use Vulkan 1.3+ compatible hardware

### No output appears
- Ensure `enable_debug_printf = true` in RendererDesc
- Verify the extension is in your shader (`#extension GL_EXT_debug_printf : enable` for GLSL)
- Check that RenderDoc 1.14+ or Validation Layers are active
- For Validation Layers, ensure `VK_LAYER_PRINTF_ONLY_PRESET=1` is set

### Too many messages
- Add conditionals in fragment shaders to limit output
- Use Debug Printf only in specific draw calls
- Consider using it only in vertex/compute shaders for most debugging

## Example Files

See the example shaders in the `shaders/` directory:
- `debug_printf_example.slang` - Slang/HLSL example
- `debug_printf_example.vert` - GLSL vertex shader example
- `debug_printf_example.frag` - GLSL fragment shader example with conditional printing

## Additional Resources

- [Vulkan Debug Printf Tutorial](https://www.lunarg.com/wp-content/uploads/2022/05/Using-Debug-Printf.pdf)
- [VK_KHR_shader_non_semantic_info Extension](https://registry.khronos.org/vulkan/specs/1.3-extensions/man/html/VK_KHR_shader_non_semantic_info.html)
- [Sascha Willems' Debug Printf Sample](https://github.com/SaschaWillems/Vulkan)
