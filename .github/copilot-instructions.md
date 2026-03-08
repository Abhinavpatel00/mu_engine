# Copilot Instructions

- This renderer uses a single descriptor set layout and a single pipeline layout for all pipelines (bindless model).
- New buffers are suballocated from larger GPU buffers; pass offsets via push constants and manage ranges with the offset allocator.
I don't use vertex buffers and index buffers. I use offsets to buffer slice via push constant.