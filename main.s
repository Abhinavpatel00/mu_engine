	.file	"main.c"
# GNU C23 (GCC) version 15.2.1 20260103 (x86_64-pc-linux-gnu)
#	compiled by GNU C version 15.2.1 20260103, GMP version 6.3.0, MPFR version 4.2.2, MPC version 1.3.1, isl version isl-0.27-GMP

# GGC heuristics: --param ggc-min-expand=100 --param ggc-min-heapsize=131072
# options passed: -march=tigerlake -mmmx -mpopcnt -msse -msse2 -msse3 -mssse3 -msse4.1 -msse4.2 -mavx -mavx2 -mno-sse4a -mno-fma4 -mno-xop -mfma -mavx512f -mbmi -mbmi2 -maes -mpclmul -mavx512vl -mavx512bw -mavx512dq -mavx512cd -mavx512vbmi -mavx512ifma -mavx512vpopcntdq -mavx512vbmi2 -mgfni -mvpclmulqdq -mavx512vnni -mavx512bitalg -mno-avx512bf16 -mavx512vp2intersect -mno-3dnow -madx -mabm -mno-cldemote -mclflushopt -mclwb -mno-clzero -mcx16 -mno-enqcmd -mf16c -mfsgsbase -mfxsr -mno-hle -msahf -mno-lwp -mlzcnt -mmovbe -mmovdir64b -mmovdiri -mno-mwaitx -mno-pconfig -mpku -mprfchw -mno-ptwrite -mrdpid -mrdrnd -mrdseed -mno-rtm -mno-serialize -mno-sgx -msha -mshstk -mno-tbm -mno-tsxldtrk -mvaes -mno-waitpkg -mno-wbnoinvd -mxsave -mxsavec -mxsaveopt -mxsaves -mno-amx-tile -mno-amx-int8 -mno-amx-bf16 -mno-uintr -mno-hreset -mno-kl -mno-widekl -mno-avxvnni -mno-avx512fp16 -mno-avxifma -mno-avxvnniint8 -mno-avxneconvert -mno-cmpccxadd -mno-amx-fp16 -mno-prefetchi -mno-raoint -mno-amx-complex -mno-avxvnniint16 -mno-sm3 -mno-sha512 -mno-sm4 -mno-apxf -mno-usermsr -mno-avx10.2 -mno-amx-avx512 -mno-amx-tf32 -mno-amx-transpose -mno-amx-fp8 -mno-movrs -mno-amx-movrs --param=l1-cache-size=48 --param=l1-cache-line-size=64 --param=l2-cache-size=8192 -mtune=tigerlake -O3
	.text
	.section	.rodata.str1.8,"aMS",@progbits,1
	.align 8
.LC1:
	.string	"compiledshaders/triangle.vert.spv"
	.align 8
.LC2:
	.string	"compiledshaders/triangle.frag.spv"
	.section	.rodata.str1.1,"aMS",@progbits,1
.LC3:
	.string	"VK_KHR_wayland_surface"
.LC4:
	.string	"VK_KHR_swapchain"
.LC6:
	.string	"My Renderer"
	.section	.text.startup,"ax",@progbits
	.p2align 4
	.globl	main
	.type	main, @function
main:
.LFB8285:
	.cfi_startproc
	leaq	8(%rsp), %r10	#,
	.cfi_def_cfa 10, 0
	andq	$-64, %rsp	#,
	pushq	-8(%r10)	#
	pushq	%rbp	#
	movq	%rsp, %rbp	#,
	.cfi_escape 0x10,0x6,0x2,0x76,0
	pushq	%r15	#
	pushq	%r14	#
	pushq	%r13	#
	pushq	%r12	#
	pushq	%r10	#
	.cfi_escape 0xf,0x3,0x76,0x58,0x6
	.cfi_escape 0x10,0xf,0x2,0x76,0x78
	.cfi_escape 0x10,0xe,0x2,0x76,0x70
	.cfi_escape 0x10,0xd,0x2,0x76,0x68
	.cfi_escape 0x10,0xc,0x2,0x76,0x60
	pushq	%rbx	#
	.cfi_escape 0x10,0x3,0x2,0x76,0x50
	leaq	-2672(%rbp), %rbx	#, tmp629
	subq	$5312, %rsp	#,
	movq	%fs:40, %rax	# MEM[(<address-space-1> long unsigned int *)40B], _262
	movq	%rax, -56(%rbp)	# _262, D.92887
	leaq	-2536(%rbp), %rax	#, _262
	movq	%rbx, -5312(%rbp)	# tmp629, %sfp
	vmovq	%rax, %xmm7	# _262, _262
	vpinsrq	$1, %rbx, %xmm7, %xmm7	# tmp629, _262, _263
	vmovdqa	%xmm7, -5328(%rbp)	# _263, %sfp
# main.c:285:     VK_CHECK(volkInitialize());
	call	volkInitialize@PLT	#
# main.c:287:     if(!is_instance_extension_supported("VK_KHR_wayland_surface"))
	leaq	.LC3(%rip), %rdi	#,
	call	is_instance_extension_supported@PLT	#
# main.c:287:     if(!is_instance_extension_supported("VK_KHR_wayland_surface"))
	testb	%al, %al	# _1
	jne	.L2	#,
# main.c:288:         glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_X11);
	movl	$393220, %esi	#,
	movl	$327683, %edi	#,
	call	glfwInitHint@PLT	#
.L3:
# main.c:293:     glfwInit();
	call	glfwInit@PLT	#
# main.c:294:     const char* dev_exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
	leaq	.LC4(%rip), %rax	#, tmp677
# main.c:297:     const char** glfw_exts      = glfwGetRequiredInstanceExtensions(&glfw_ext_count);
	leaq	-5292(%rbp), %rdi	#, tmp290
# main.c:296:     u32          glfw_ext_count = 0;
	movl	$0, -5292(%rbp)	#, glfw_ext_count
# main.c:294:     const char* dev_exts[] = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};
	movq	%rax, -4008(%rbp)	# tmp677, dev_exts[0]
# main.c:324:     Renderer          renderer         = {0};
	leaq	-3120(%rbp), %r15	#, tmp630
# main.c:297:     const char** glfw_exts      = glfwGetRequiredInstanceExtensions(&glfw_ext_count);
	call	glfwGetRequiredInstanceExtensions@PLT	#
# main.c:300:     RendererDesc desc = {
	xorl	%esi, %esi	#
	movl	$712, %edx	#,
	leaq	-4720(%rbp), %rdi	#,
# main.c:297:     const char** glfw_exts      = glfwGetRequiredInstanceExtensions(&glfw_ext_count);
	movq	%rax, %r12	#, glfw_exts
# main.c:300:     RendererDesc desc = {
	call	memset@PLT	#
	movq	.LC5(%rip), %rax	#, tmp295
	movl	$257, %r9d	#,
# main.c:324:     Renderer          renderer         = {0};
	movl	$3008, %edx	#,
	xorl	%esi, %esi	#
	movq	%r15, %rdi	# tmp630,
# main.c:300:     RendererDesc desc = {
	movw	%r9w, -4668(%rbp)	#, MEM <vector(2) unsigned char> [(_Bool *)&desc + 52B]
	movq	%rax, -4720(%rbp)	# tmp295, MEM <vector(2) unsigned int> [(unsigned int *)&desc]
	leaq	.LC6(%rip), %rax	#, tmp678
	movq	%rax, -4712(%rbp)	# tmp678, desc.app_name
	leaq	-4008(%rbp), %rax	#, tmp297
	movq	%rax, -4688(%rbp)	# tmp297, desc.device_extensions
	movl	-5292(%rbp), %eax	# glfw_ext_count, glfw_ext_count
	movq	%r12, -4696(%rbp)	# glfw_exts, desc.instance_extensions
	movl	%eax, -4676(%rbp)	# glfw_ext_count, desc.instance_extension_count
	movq	.LC8(%rip), %rax	#, tmp300
	movl	$1, -4672(%rbp)	#, desc.device_extension_count
	movq	%rax, -4664(%rbp)	# tmp300, MEM <vector(2) unsigned int> [(unsigned int *)&desc + 56B]
	movl	$44, -4028(%rbp)	#, desc.swapchain_preferred_format
	movl	$10, -4020(%rbp)	#, desc.swapchain_extra_usage_flags
# main.c:324:     Renderer          renderer         = {0};
	call	memset@PLT	#
# main.c:326:     renderer_create(&renderer, &desc);
	leaq	-4720(%rbp), %rsi	#,
	movq	%r15, %rdi	# tmp630,
	call	renderer_create@PLT	#
# vk.h:371:     GraphicsPipelineConfig cfg = {0};
	vpxor	%xmm0, %xmm0, %xmm0	# tmp307
# vk.h:392:     return cfg;
	movabsq	$12884902145, %rax	#, tmp679
# vk.h:371:     GraphicsPipelineConfig cfg = {0};
	movq	$0, -3536(%rbp)	#, MEM[(struct GraphicsPipelineConfig *)_377]
	vmovdqa	%ymm0, -3856(%rbp)	# tmp307, MEM[(struct GraphicsPipelineConfig *)_377]
# main.c:334:     render_pipelines.pipelines[TRIANGLE_PIPELINE] = create_graphics_pipeline(&renderer, &cfg);
	leaq	-3520(%rbp), %rsi	#, tmp324
	movq	%r15, %rdi	# tmp630,
# vk.h:371:     GraphicsPipelineConfig cfg = {0};
	vmovdqa	%ymm0, -3824(%rbp)	# tmp307, MEM[(struct GraphicsPipelineConfig *)_377]
	vmovdqa	%ymm0, -3792(%rbp)	# tmp307, MEM[(struct GraphicsPipelineConfig *)_377]
	vmovdqa	%ymm0, -3760(%rbp)	# tmp307, MEM[(struct GraphicsPipelineConfig *)_377]
	vmovdqa	%ymm0, -3728(%rbp)	# tmp307, MEM[(struct GraphicsPipelineConfig *)_377]
	vmovdqa	%ymm0, -3696(%rbp)	# tmp307, MEM[(struct GraphicsPipelineConfig *)_377]
	vmovdqa	%ymm0, -3920(%rbp)	# tmp307, MEM[(struct GraphicsPipelineConfig *)_377]
	vmovdqa	%ymm0, -3888(%rbp)	# tmp307, MEM[(struct GraphicsPipelineConfig *)_377]
	vmovdqa	%ymm0, -3664(%rbp)	# tmp307, MEM[(struct GraphicsPipelineConfig *)_377]
	vmovdqa	%ymm0, -3632(%rbp)	# tmp307, MEM[(struct GraphicsPipelineConfig *)_377]
	vmovdqa	%ymm0, -3600(%rbp)	# tmp307, MEM[(struct GraphicsPipelineConfig *)_377]
	vmovdqa	%ymm0, -3568(%rbp)	# tmp307, MEM[(struct GraphicsPipelineConfig *)_377]
# vk.h:392:     return cfg;
	movq	%rax, -3888(%rbp)	# tmp679, MEM <unsigned long> [(struct GraphicsPipelineConfig *)_377 + 32B]
# main.c:330:     cfg.vert_path                                 = "compiledshaders/triangle.vert.spv";
	leaq	.LC2(%rip), %rax	#, tmp286
# vk.h:392:     return cfg;
	movl	$3, -3892(%rbp)	#, MEM <unsigned int> [(struct GraphicsPipelineConfig *)_377 + 28B]
# vk.h:355:     return (ColorAttachmentBlend){
	movl	$1, -3856(%rbp)	#, MEM[(struct GraphicsPipelineConfig *)_377].blends[0].src_color
	movl	$1, -3844(%rbp)	#, MEM[(struct GraphicsPipelineConfig *)_377].blends[0].src_alpha
	movl	$15, -3832(%rbp)	#, MEM[(struct GraphicsPipelineConfig *)_377].blends[0].write_mask
	movl	$1, -3824(%rbp)	#, MEM[(struct GraphicsPipelineConfig *)_377].blends[1].src_color
	movl	$1, -3812(%rbp)	#, MEM[(struct GraphicsPipelineConfig *)_377].blends[1].src_alpha
	movl	$15, -3800(%rbp)	#, MEM[(struct GraphicsPipelineConfig *)_377].blends[1].write_mask
	movl	$1, -3792(%rbp)	#, MEM[(struct GraphicsPipelineConfig *)_377].blends[2].src_color
	movl	$1, -3780(%rbp)	#, MEM[(struct GraphicsPipelineConfig *)_377].blends[2].src_alpha
	movl	$15, -3768(%rbp)	#, MEM[(struct GraphicsPipelineConfig *)_377].blends[2].write_mask
	movl	$1, -3760(%rbp)	#, MEM[(struct GraphicsPipelineConfig *)_377].blends[3].src_color
	movl	$1, -3748(%rbp)	#, MEM[(struct GraphicsPipelineConfig *)_377].blends[3].src_alpha
	movl	$15, -3736(%rbp)	#, MEM[(struct GraphicsPipelineConfig *)_377].blends[3].write_mask
	movl	$1, -3728(%rbp)	#, MEM[(struct GraphicsPipelineConfig *)_377].blends[4].src_color
	movl	$1, -3716(%rbp)	#, MEM[(struct GraphicsPipelineConfig *)_377].blends[4].src_alpha
	movl	$15, -3704(%rbp)	#, MEM[(struct GraphicsPipelineConfig *)_377].blends[4].write_mask
	movl	$1, -3696(%rbp)	#, MEM[(struct GraphicsPipelineConfig *)_377].blends[5].src_color
	movl	$1, -3684(%rbp)	#, MEM[(struct GraphicsPipelineConfig *)_377].blends[5].src_alpha
	movl	$15, -3672(%rbp)	#, MEM[(struct GraphicsPipelineConfig *)_377].blends[5].write_mask
# vk.h:392:     return cfg;
	vmovdqa	-3920(%rbp), %ymm1	# MEM[(struct GraphicsPipelineConfig *)_377], MEM[(struct GraphicsPipelineConfig *)_377]
# vk.h:355:     return (ColorAttachmentBlend){
	movl	$1, -3664(%rbp)	#, MEM[(struct GraphicsPipelineConfig *)_377].blends[6].src_color
# main.c:330:     cfg.vert_path                                 = "compiledshaders/triangle.vert.spv";
	vmovq	.LC30(%rip), %xmm7	#, tmp680
# vk.h:392:     return cfg;
	vmovdqu	%ymm1, -3520(%rbp)	# MEM[(struct GraphicsPipelineConfig *)_377], cfg
	vmovdqa	-3888(%rbp), %ymm1	# MEM[(struct GraphicsPipelineConfig *)_377], MEM[(struct GraphicsPipelineConfig *)_377]
# vk.h:355:     return (ColorAttachmentBlend){
	movl	$1, -3652(%rbp)	#, MEM[(struct GraphicsPipelineConfig *)_377].blends[6].src_alpha
# vk.h:392:     return cfg;
	vmovdqu	%ymm1, -3488(%rbp)	# MEM[(struct GraphicsPipelineConfig *)_377], cfg
	vmovdqa	-3856(%rbp), %ymm1	# MEM[(struct GraphicsPipelineConfig *)_377], MEM[(struct GraphicsPipelineConfig *)_377]
# vk.h:355:     return (ColorAttachmentBlend){
	movl	$15, -3640(%rbp)	#, MEM[(struct GraphicsPipelineConfig *)_377].blends[6].write_mask
# vk.h:392:     return cfg;
	vmovdqu	%ymm1, -3456(%rbp)	# MEM[(struct GraphicsPipelineConfig *)_377], cfg
	vmovdqa	-3824(%rbp), %ymm1	# MEM[(struct GraphicsPipelineConfig *)_377], MEM[(struct GraphicsPipelineConfig *)_377]
# vk.h:355:     return (ColorAttachmentBlend){
	movl	$1, -3632(%rbp)	#, MEM[(struct GraphicsPipelineConfig *)_377].blends[7].src_color
# vk.h:392:     return cfg;
	vmovdqu	%ymm1, -3424(%rbp)	# MEM[(struct GraphicsPipelineConfig *)_377], cfg
	vmovdqa	-3792(%rbp), %ymm1	# MEM[(struct GraphicsPipelineConfig *)_377], MEM[(struct GraphicsPipelineConfig *)_377]
# vk.h:355:     return (ColorAttachmentBlend){
	movl	$1, -3620(%rbp)	#, MEM[(struct GraphicsPipelineConfig *)_377].blends[7].src_alpha
# vk.h:392:     return cfg;
	vmovdqu	%ymm1, -3392(%rbp)	# MEM[(struct GraphicsPipelineConfig *)_377], cfg
	vmovdqa	-3760(%rbp), %ymm1	# MEM[(struct GraphicsPipelineConfig *)_377], MEM[(struct GraphicsPipelineConfig *)_377]
# vk.h:355:     return (ColorAttachmentBlend){
	movl	$15, -3608(%rbp)	#, MEM[(struct GraphicsPipelineConfig *)_377].blends[7].write_mask
# vk.h:392:     return cfg;
	vmovdqu	%ymm1, -3360(%rbp)	# MEM[(struct GraphicsPipelineConfig *)_377], cfg
	vmovdqa	-3728(%rbp), %ymm1	# MEM[(struct GraphicsPipelineConfig *)_377], MEM[(struct GraphicsPipelineConfig *)_377]
	movq	$0, -3136(%rbp)	#, cfg
	vmovdqu	%ymm1, -3328(%rbp)	# MEM[(struct GraphicsPipelineConfig *)_377], cfg
	vmovdqa	-3696(%rbp), %ymm1	# MEM[(struct GraphicsPipelineConfig *)_377], MEM[(struct GraphicsPipelineConfig *)_377]
# main.c:332:     cfg.color_attachment_count                    = 1;
	movl	$1, -3480(%rbp)	#, cfg.color_attachment_count
# vk.h:392:     return cfg;
	vmovdqu	%ymm1, -3296(%rbp)	# MEM[(struct GraphicsPipelineConfig *)_377], cfg
	vmovdqa	-3664(%rbp), %ymm1	# MEM[(struct GraphicsPipelineConfig *)_377], MEM[(struct GraphicsPipelineConfig *)_377]
	vmovdqu	%ymm1, -3264(%rbp)	# MEM[(struct GraphicsPipelineConfig *)_377], cfg
	vmovdqa	-3632(%rbp), %ymm1	# MEM[(struct GraphicsPipelineConfig *)_377], MEM[(struct GraphicsPipelineConfig *)_377]
	vmovdqu	%ymm1, -3232(%rbp)	# MEM[(struct GraphicsPipelineConfig *)_377], cfg
	vpxor	%xmm1, %xmm1, %xmm1	# tmp644
	vmovdqu	%ymm1, -3200(%rbp)	# tmp644, cfg
	vmovdqu	%ymm1, -3168(%rbp)	# tmp645, cfg
# main.c:330:     cfg.vert_path                                 = "compiledshaders/triangle.vert.spv";
	vpinsrq	$1, %rax, %xmm7, %xmm1	# tmp286, tmp680, _260
# main.c:333:     cfg.color_formats                             = &renderer.swapchain.format;
	leaq	-2520(%rbp), %rax	#, tmp323
# main.c:330:     cfg.vert_path                                 = "compiledshaders/triangle.vert.spv";
	vmovdqa	%xmm1, -3520(%rbp)	# _260, MEM <vector(2) long unsigned int> [(const char * *)&cfg]
# main.c:333:     cfg.color_formats                             = &renderer.swapchain.format;
	movq	%rax, -3472(%rbp)	# tmp323, cfg.color_formats
# main.c:334:     render_pipelines.pipelines[TRIANGLE_PIPELINE] = create_graphics_pipeline(&renderer, &cfg);
	vzeroupper
	call	create_graphics_pipeline@PLT	#
# main.c:345:     VkBufferCreateInfo ci = {
	vpxor	%xmm0, %xmm0, %xmm0	# tmp307
	movl	$12, -5056(%rbp)	#, ci.sType
# main.c:356:     vmaCreateBuffer(renderer.vmaallocator, &ci, &aci, &vertex_buffer, &alloc, &info);
	movq	-3032(%rbp), %rdi	# renderer.vmaallocator,
# main.c:334:     render_pipelines.pipelines[TRIANGLE_PIPELINE] = create_graphics_pipeline(&renderer, &cfg);
	movq	%rax, -5336(%rbp)	#, %sfp
# main.c:351:     VmaAllocationCreateInfo aci = {.usage = VMA_MEMORY_USAGE_AUTO,
	movq	.LC9(%rip), %rax	#, tmp328
# main.c:356:     vmaCreateBuffer(renderer.vmaallocator, &ci, &aci, &vertex_buffer, &alloc, &info);
	leaq	-5280(%rbp), %rcx	#, tmp331
	leaq	-5200(%rbp), %rdx	#, tmp332
# main.c:345:     VkBufferCreateInfo ci = {
	vmovdqu8	%ymm0, -5052(%rbp)	# tmp307, MEM <char[52]> [(struct VkBufferCreateInfo *)&ci + 4B]
# main.c:356:     vmaCreateBuffer(renderer.vmaallocator, &ci, &aci, &vertex_buffer, &alloc, &info);
	leaq	-5056(%rbp), %rsi	#, tmp333
	leaq	-4992(%rbp), %r9	#,
# main.c:345:     VkBufferCreateInfo ci = {
	vmovdqu8	%ymm0, -5032(%rbp)	# tmp307, MEM <char[52]> [(struct VkBufferCreateInfo *)&ci + 4B]
# main.c:356:     vmaCreateBuffer(renderer.vmaallocator, &ci, &aci, &vertex_buffer, &alloc, &info);
	leaq	-5272(%rbp), %r8	#,
# main.c:351:     VmaAllocationCreateInfo aci = {.usage = VMA_MEMORY_USAGE_AUTO,
	movq	%rax, -5200(%rbp)	# tmp328, MEM <vector(2) unsigned int> [(unsigned int *)&aci]
# main.c:345:     VkBufferCreateInfo ci = {
	movq	$3333300, -5032(%rbp)	#, ci.size
	movl	$131104, -5024(%rbp)	#, ci.usage
# main.c:351:     VmaAllocationCreateInfo aci = {.usage = VMA_MEMORY_USAGE_AUTO,
	movq	$0, -5160(%rbp)	#, MEM <char[40]> [(struct VmaAllocationCreateInfo *)&aci + 8B]
	vmovdqu8	%ymm0, -5192(%rbp)	# tmp307, MEM <char[40]> [(struct VmaAllocationCreateInfo *)&aci + 8B]
# main.c:356:     vmaCreateBuffer(renderer.vmaallocator, &ci, &aci, &vertex_buffer, &alloc, &info);
	vzeroupper
	call	vmaCreateBuffer@PLT	#
	movq	-4960(%rbp), %r9	# info.pMappedData, ivtmp.128
	vmovss	.LC10(%rip), %xmm18	#, tmp631
# main.c:361:     for(int y = 0; y < 23; y++)
	xorl	%r8d, %r8d	# y
	vmovss	.LC11(%rip), %xmm17	#, tmp632
	vmovss	.LC13(%rip), %xmm21	#, tmp633
	vxorps	%xmm11, %xmm11, %xmm11	# tmp359
	vxorps	%xmm22, %xmm22, %xmm22	# tmp653
	movq	.LC18(%rip), %rdi	#, tmp638
	movq	.LC14(%rip), %rsi	#, tmp634
	leaq	1320(%r9), %rdx	#, ivtmp.127
	vmovss	.LC15(%rip), %xmm14	#, tmp635
	movq	.LC19(%rip), %rcx	#, tmp622
	vmovq	.LC20(%rip), %xmm9	#, tmp625
	vmovss	.LC21(%rip), %xmm20	#, tmp626
	vmovss	.LC22(%rip), %xmm19	#, tmp627
	movq	.LC0(%rip), %r10	#, vect_vec_iv_.74
# main.c:364:             float fx = x * 0.1f;
	vmovaps	.LC28(%rip), %xmm16	#, tmp349
# main.c:368:             cpu_vertices[index++] = (Vertex){{fx + 0.005f, fy}, {0, 1, 0}};
	vmovaps	.LC29(%rip), %xmm15	#, tmp353
	.p2align 4,,10
	.p2align 3
.L4:
# main.c:365:             float fy = y * 0.1f;
	vcvtsi2ssl	%r8d, %xmm22, %xmm10	# y, tmp653, tmp654
	vxorps	%xmm7, %xmm7, %xmm7	# tmp681
# main.c:369:             cpu_vertices[index++] = (Vertex){{fx, fy + 0.005f}, {0, 0, 1}};
	vmovq	%r10, %xmm6	# vect_vec_iv_.74, vect_vec_iv_.74
	movq	%r9, %rax	# ivtmp.128, ivtmp.114
# main.c:365:             float fy = y * 0.1f;
	vmulss	%xmm20, %xmm10, %xmm10	# tmp626, _6, fy
# main.c:369:             cpu_vertices[index++] = (Vertex){{fx, fy + 0.005f}, {0, 0, 1}};
	vaddss	%xmm19, %xmm10, %xmm23	# tmp627, fy, _17
	vunpcklps	%xmm7, %xmm10, %xmm8	# tmp681, fy, _480
	vunpcklps	%xmm17, %xmm10, %xmm12	# tmp632, fy, _471
	vunpcklps	%xmm7, %xmm23, %xmm7	# tmp682, _17, _482
	.p2align 4,,10
	.p2align 3
.L6:
# main.c:364:             float fx = x * 0.1f;
	vmovq	%xmm6, %xmm0	# vect_vec_iv_.74, tmp346
# main.c:367:             cpu_vertices[index++] = (Vertex){{fx, fy}, {4, 0, 0}};
	movq	%rdi, 8(%rax)	# tmp638, MEM <vector(2) float> [(float *)_14 + 8B]
	addq	$120, %rax	#, ivtmp.114
# main.c:362:         for(int x = 0; x < 23; x++)
	vpaddd	%xmm9, %xmm6, %xmm6	# tmp625, vect_vec_iv_.74, vect_vec_iv_.74
# main.c:364:             float fx = x * 0.1f;
	vcvtdq2ps	%xmm0, %xmm0	# tmp346, tmp347
# main.c:364:             float fx = x * 0.1f;
	vmovq	%xmm0, %xmm0	# tmp347, tmp350
# main.c:367:             cpu_vertices[index++] = (Vertex){{fx, fy}, {4, 0, 0}};
	vmovlps	%xmm8, -96(%rax)	# _480, MEM <vector(2) float> [(float *)_14 + 24B]
# main.c:364:             float fx = x * 0.1f;
	vmulps	%xmm16, %xmm0, %xmm0	# tmp349, tmp350, tmp351
# main.c:367:             cpu_vertices[index++] = (Vertex){{fx, fy}, {4, 0, 0}};
	movq	%rsi, -88(%rax)	# tmp634, MEM <vector(2) float> [(float *)_14 + 32B]
	movq	$0, -72(%rax)	#, MEM <vector(2) float> [(float *)_14 + 48B]
	vmovlps	%xmm12, -56(%rax)	# _471, MEM <vector(2) float> [(float *)_14 + 64B]
	movq	$0, -48(%rax)	#, MEM <vector(2) float> [(float *)_14 + 72B]
	movq	%rcx, -32(%rax)	# tmp622, MEM <vector(2) float> [(float *)_14 + 88B]
# main.c:368:             cpu_vertices[index++] = (Vertex){{fx + 0.005f, fy}, {0, 1, 0}};
	vmovq	%xmm0, %xmm1	# tmp351, tmp354
	vunpcklps	%xmm10, %xmm0, %xmm13	# fy, tmp357, _474
	vmovaps	%xmm0, %xmm4	# tmp351, tmp357
# main.c:367:             cpu_vertices[index++] = (Vertex){{fx, fy}, {4, 0, 0}};
	vmovlps	%xmm7, -16(%rax)	# _482, MEM <vector(2) float> [(float *)_14 + 104B]
# main.c:368:             cpu_vertices[index++] = (Vertex){{fx + 0.005f, fy}, {0, 1, 0}};
	vaddps	%xmm15, %xmm1, %xmm1	# tmp353, tmp354, tmp355
	vmovshdup	%xmm0, %xmm3	# tmp351, tmp360
# main.c:367:             cpu_vertices[index++] = (Vertex){{fx, fy}, {4, 0, 0}};
	vunpcklps	%xmm23, %xmm4, %xmm4	# _17, tmp357, _497
	movq	%rcx, -8(%rax)	# tmp622, MEM <vector(2) float> [(float *)_14 + 112B]
	vmovlps	%xmm4, -80(%rax)	# _497, MEM <vector(2) float> [(float *)_14 + 40B]
	vinsertps	$64, %xmm1, %xmm0, %xmm2	#, vect__13.77, tmp351, _470
	vblendps	$1, %xmm0, %xmm13, %xmm0	#, tmp351, _474, _492
	vmovlps	%xmm0, -120(%rax)	# _492, MEM <vector(2) float> [(float *)_14]
	vunpcklps	%xmm10, %xmm2, %xmm0	# fy, tmp373, _502
	vmovshdup	%xmm2, %xmm2	# _470, tmp377
	vunpcklps	%xmm3, %xmm11, %xmm5	# tmp360, tmp359, _476
	vinsertps	$16, %xmm1, %xmm5, %xmm1	#, vect__13.77, _476, _494
	vunpcklps	%xmm3, %xmm14, %xmm3	# tmp360, tmp635, _499
	vmovlps	%xmm0, -40(%rax)	# _502, MEM <vector(2) float> [(float *)_14 + 80B]
	vmovlps	%xmm1, -104(%rax)	# _494, MEM <vector(2) float> [(float *)_14 + 16B]
	vmovlps	%xmm3, -64(%rax)	# _499, MEM <vector(2) float> [(float *)_14 + 56B]
	vunpcklps	%xmm2, %xmm11, %xmm2	# tmp377, tmp359, _504
	vmovlps	%xmm2, -24(%rax)	# _504, MEM <vector(2) float> [(float *)_14 + 96B]
	cmpq	%rax, %rdx	# ivtmp.114, ivtmp.127
	jne	.L6	#,
# main.c:361:     for(int y = 0; y < 23; y++)
	incl	%r8d	# y
# main.c:367:             cpu_vertices[index++] = (Vertex){{fx, fy}, {4, 0, 0}};
	movl	$0x00000000, 12(%rdx)	#, MEM[(float *)_146 + 12B]
# main.c:361:     for(int y = 0; y < 23; y++)
	addq	$1380, %r9	#, ivtmp.128
	addq	$1380, %rdx	#, ivtmp.127
# main.c:367:             cpu_vertices[index++] = (Vertex){{fx, fy}, {4, 0, 0}};
	vmovss	%xmm18, -1380(%rdx)	# tmp631, MEM[(float *)_146]
	vmovss	%xmm10, -1376(%rdx)	# fy, MEM[(float *)_146 + 4B]
	vmovss	%xmm17, -1372(%rdx)	# tmp632, MEM[(float *)_146 + 8B]
	movl	$0x00000000, -1364(%rdx)	#, MEM[(float *)_146 + 16B]
# main.c:368:             cpu_vertices[index++] = (Vertex){{fx + 0.005f, fy}, {0, 1, 0}};
	vmovss	%xmm21, -1360(%rdx)	# tmp633, MEM[(float *)_146 + 20B]
	vmovss	%xmm10, -1356(%rdx)	# fy, MEM[(float *)_146 + 24B]
	movl	$0x00000000, -1352(%rdx)	#, MEM[(float *)_146 + 28B]
	movq	%rsi, -1348(%rdx)	# tmp634, MEM <vector(2) float> [(float *)_146 + 32B]
# main.c:369:             cpu_vertices[index++] = (Vertex){{fx, fy + 0.005f}, {0, 0, 1}};
	vmovss	%xmm18, -1340(%rdx)	# tmp631, MEM[(float *)_146 + 40B]
	vmovss	%xmm23, -1336(%rdx)	# _17, MEM[(float *)_146 + 44B]
	movq	$0, -1332(%rdx)	#, MEM <vector(2) float> [(float *)_146 + 48B]
	vmovss	%xmm14, -1324(%rdx)	# tmp635, MEM[(float *)_146 + 56B]
# main.c:361:     for(int y = 0; y < 23; y++)
	cmpl	$23, %r8d	#, y
	jne	.L4	#,
# main.c:373:     VkBufferDeviceAddressInfo addrInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = vertex_buffer};
	movq	-5280(%rbp), %rax	# vertex_buffer, vertex_buffer
	movq	$0, -5260(%rbp)	#, MEM <char[12]> [(struct VkBufferDeviceAddressInfo *)&addrInfo + 4B]
# main.c:375:     VkDeviceAddress gpu_address = vkGetBufferDeviceAddress(renderer.device, &addrInfo);
	leaq	-5264(%rbp), %rsi	#, tmp388
# main.c:221:             switch(d->type)
	leaq	.L23(%rip), %r12	#, tmp642
# main.c:373:     VkBufferDeviceAddressInfo addrInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = vertex_buffer};
	movl	$0, -5252(%rbp)	#, MEM <char[12]> [(struct VkBufferDeviceAddressInfo *)&addrInfo + 4B]
# main.c:375:     VkDeviceAddress gpu_address = vkGetBufferDeviceAddress(renderer.device, &addrInfo);
	movq	-3096(%rbp), %rdi	# renderer.device,
# main.c:373:     VkBufferDeviceAddressInfo addrInfo = {.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO, .buffer = vertex_buffer};
	movl	$1000244001, -5264(%rbp)	#, addrInfo.sType
	movq	%rax, -5248(%rbp)	# vertex_buffer, addrInfo.buffer
# main.c:375:     VkDeviceAddress gpu_address = vkGetBufferDeviceAddress(renderer.device, &addrInfo);
	call	*vkGetBufferDeviceAddress(%rip)	# vkGetBufferDeviceAddress
	movq	%rax, -5344(%rbp)	#, %sfp
	.p2align 4,,10
	.p2align 3
.L8:
# main.c:382:     while(!glfwWindowShouldClose(renderer.window))
	movq	-2480(%rbp), %rdi	# renderer.window,
	call	glfwWindowShouldClose@PLT	#
# main.c:382:     while(!glfwWindowShouldClose(renderer.window))
	testl	%eax, %eax	# _96
	jne	.L62	#,
.L30:
# main.c:384:         glfwPollEvents();
	call	glfwPollEvents@PLT	#
# main.c:386:         glfwGetFramebufferSize(renderer.window, &fb_w, &fb_h);
	movq	-2480(%rbp), %rdi	# renderer.window,
	leaq	-5284(%rbp), %rdx	#, tmp391
	leaq	-5288(%rbp), %rsi	#, tmp392
	call	glfwGetFramebufferSize@PLT	#
# main.c:388:                                              || fb_h != (int)renderer.swapchain.extent.height;
	cmpb	$0, g_framebuffer_resized(%rip)	#, g_framebuffer_resized
# main.c:387:         renderer.swapchain.needs_recreate |= g_framebuffer_resized || fb_w != (int)renderer.swapchain.extent.width
	movl	-5288(%rbp), %eax	# fb_w, pretmp_453
# main.c:388:                                              || fb_h != (int)renderer.swapchain.extent.height;
	jne	.L60	#,
# main.c:387:         renderer.swapchain.needs_recreate |= g_framebuffer_resized || fb_w != (int)renderer.swapchain.extent.width
	cmpl	%eax, -2508(%rbp)	# pretmp_453, renderer.swapchain.extent.width
	je	.L12	#,
.L60:
# main.c:387:         renderer.swapchain.needs_recreate |= g_framebuffer_resized || fb_w != (int)renderer.swapchain.extent.width
	movb	$1, -2491(%rbp)	#, renderer.swapchain.needs_recreate
# main.c:391:         if(fb_w == 0 || fb_h == 0)
	testl	%eax, %eax	# pretmp_453
	je	.L10	#,
# main.c:391:         if(fb_w == 0 || fb_h == 0)
	movl	-5284(%rbp), %r8d	# fb_h,
	testl	%r8d, %r8d	#
	je	.L10	#,
# main.c:398:             vkDeviceWaitIdle(renderer.device);
	movq	-3096(%rbp), %rdi	# renderer.device, pretmp_8
.L11:
	call	*vkDeviceWaitIdle(%rip)	# vkDeviceWaitIdle
# main.c:399:             vk_swapchain_recreate(renderer.device, renderer.physical_device, &renderer.swapchain, fb_w, fb_h,
	subq	$8, %rsp	#,
	movl	-5284(%rbp), %r8d	# fb_h,
	movl	-5288(%rbp), %ecx	# fb_w,
	pushq	-2336(%rbp)	# renderer.one_time_gfx_pool
	leaq	-2992(%rbp), %rdx	#, tmp401
	movq	-3104(%rbp), %rsi	# renderer.physical_device,
	movq	-3096(%rbp), %rdi	# renderer.device,
	movq	-3080(%rbp), %r9	# renderer.graphics_queue,
	call	vk_swapchain_recreate@PLT	#
# main.c:401:             g_framebuffer_resized             = false;
	movb	$0, g_framebuffer_resized(%rip)	#, g_framebuffer_resized
# main.c:402:             renderer.swapchain.needs_recreate = false;
	movb	$0, -2491(%rbp)	#, renderer.swapchain.needs_recreate
# main.c:403:             continue;  // restart frame cleanly
	popq	%rsi	#
	popq	%rdi	#
# main.c:382:     while(!glfwWindowShouldClose(renderer.window))
	movq	-2480(%rbp), %rdi	# renderer.window,
	call	glfwWindowShouldClose@PLT	#
# main.c:382:     while(!glfwWindowShouldClose(renderer.window))
	testl	%eax, %eax	# _96
	je	.L30	#,
.L62:
# main.c:502: }
	movq	-56(%rbp), %rax	# D.92887, tmp655
	subq	%fs:40, %rax	# MEM[(<address-space-1> long unsigned int *)40B], tmp655
	jne	.L63	#,
	leaq	-48(%rbp), %rsp	#,
	xorl	%eax, %eax	#
	popq	%rbx	#
	popq	%r10	#
	.cfi_remember_state
	.cfi_def_cfa 10, 0
	popq	%r12	#
	popq	%r13	#
	popq	%r14	#
	popq	%r15	#
	popq	%rbp	#
	leaq	-8(%r10), %rsp	#,
	.cfi_def_cfa 7, 8
	ret	
.L10:
	.cfi_restore_state
# main.c:393:             glfwWaitEvents();
	call	glfwWaitEvents@PLT	#
# main.c:394:             continue;
	jmp	.L8	#
.L12:
# main.c:388:                                              || fb_h != (int)renderer.swapchain.extent.height;
	movl	-5284(%rbp), %ecx	# fb_h, fb_h.9_30
# main.c:388:                                              || fb_h != (int)renderer.swapchain.extent.height;
	cmpl	%ecx, -2504(%rbp)	# fb_h.9_30, renderer.swapchain.extent.height
	setne	%dl	#, _132
	orb	-2491(%rbp), %dl	# renderer.swapchain.needs_recreate, _195
# main.c:387:         renderer.swapchain.needs_recreate |= g_framebuffer_resized || fb_w != (int)renderer.swapchain.extent.width
	movb	%dl, -2491(%rbp)	# _195, renderer.swapchain.needs_recreate
# main.c:391:         if(fb_w == 0 || fb_h == 0)
	testl	%eax, %eax	# pretmp_453
	je	.L10	#,
# main.c:391:         if(fb_w == 0 || fb_h == 0)
	testl	%ecx, %ecx	# fb_h.9_30
	je	.L10	#,
# main.c:398:             vkDeviceWaitIdle(renderer.device);
	movq	-3096(%rbp), %rdi	# renderer.device, pretmp_8
# main.c:396:         if(renderer.swapchain.needs_recreate)
	testb	%dl, %dl	# _195
	jne	.L11	#,
# main.c:406:             vkWaitForFences(renderer.device, 1, &renderer.frames[renderer.current_frame].in_flight_fence, VK_TRUE, UINT64_MAX);
	movl	-2376(%rbp), %edx	# renderer.current_frame, _372
# main.c:406:             vkWaitForFences(renderer.device, 1, &renderer.frames[renderer.current_frame].in_flight_fence, VK_TRUE, UINT64_MAX);
	movq	$-1, %r8	#,
	movl	$1, %ecx	#,
	movl	$1, %esi	#,
# main.c:406:             vkWaitForFences(renderer.device, 1, &renderer.frames[renderer.current_frame].in_flight_fence, VK_TRUE, UINT64_MAX);
	addq	$21, %rdx	#, _366
	salq	$5, %rdx	#, _357
	addq	%r15, %rdx	# tmp630, _47
# main.c:406:             vkWaitForFences(renderer.device, 1, &renderer.frames[renderer.current_frame].in_flight_fence, VK_TRUE, UINT64_MAX);
	call	*vkWaitForFences(%rip)	# vkWaitForFences
# main.c:407:             vkResetFences(renderer.device, 1, &renderer.frames[renderer.current_frame].in_flight_fence);
	movl	-2376(%rbp), %edx	# renderer.current_frame, _320
# main.c:407:             vkResetFences(renderer.device, 1, &renderer.frames[renderer.current_frame].in_flight_fence);
	movq	-3096(%rbp), %rdi	# renderer.device,
	movl	$1, %esi	#,
# main.c:407:             vkResetFences(renderer.device, 1, &renderer.frames[renderer.current_frame].in_flight_fence);
	addq	$21, %rdx	#, _140
	salq	$5, %rdx	#, _104
	addq	%r15, %rdx	# tmp630, _51
# main.c:407:             vkResetFences(renderer.device, 1, &renderer.frames[renderer.current_frame].in_flight_fence);
	call	*vkResetFences(%rip)	# vkResetFences
# main.c:410:             vkResetCommandPool(renderer.device, renderer.frames[renderer.current_frame].cmdbufpool, 0);
	movl	-2376(%rbp), %eax	# renderer.current_frame, renderer.current_frame
	movq	-3096(%rbp), %rdi	# renderer.device,
	xorl	%edx, %edx	#
	salq	$5, %rax	#, tmp419
	movq	-2464(%rax,%rbp), %rsi	# renderer.frames[_54].cmdbufpool, renderer.frames[_54].cmdbufpool
	call	*vkResetCommandPool(%rip)	# vkResetCommandPool
# main.c:411:             vk_swapchain_acquire(renderer.device, &renderer.swapchain,
	movl	-2376(%rbp), %eax	# renderer.current_frame, renderer.current_frame
# vk.h:458:     VkResult r = vkAcquireNextImageKHR(device, sc->swapchain, timeout, image_available, fence, &sc->current_image);
	movq	-5312(%rbp), %r9	# %sfp,
	xorl	%r8d, %r8d	#
	movq	-2536(%rbp), %rsi	# MEM[(struct FlowSwapchain *)&renderer + 128B].swapchain,
	movq	-3096(%rbp), %rdi	# renderer.device,
	movq	$-1, %rdx	#,
# main.c:411:             vk_swapchain_acquire(renderer.device, &renderer.swapchain,
	salq	$5, %rax	#, tmp429
# vk.h:458:     VkResult r = vkAcquireNextImageKHR(device, sc->swapchain, timeout, image_available, fence, &sc->current_image);
	movq	-2456(%rax,%rbp), %rcx	# renderer.frames[_57].image_available_semaphore, renderer.frames[_57].image_available_semaphore
	call	*vkAcquireNextImageKHR(%rip)	# vkAcquireNextImageKHR
# vk.h:460:     if(r == VK_SUCCESS)
	testl	%eax, %eax	# r
	je	.L16	#,
# vk.h:463:     if(r == VK_SUBOPTIMAL_KHR || r == VK_ERROR_OUT_OF_DATE_KHR)
	cmpl	$1000001003, %eax	#, r
	je	.L33	#,
	cmpl	$-1000001004, %eax	#, r
	je	.L33	#,
.L16:
# main.c:416:         vk_cmd_begin(renderer.frames[renderer.current_frame].cmdbuf, false);
	movl	-2376(%rbp), %eax	# renderer.current_frame, renderer.current_frame
# helpers.h:166:     VkCommandBufferBeginInfo ci = {
	movl	$42, -4800(%rbp)	#, MEM[(struct VkCommandBufferBeginInfo *)_376].sType
# helpers.h:173:     VK_CHECK(vkBeginCommandBuffer(cmd, &ci));
	leaq	-4800(%rbp), %r13	#, tmp445
# main.c:447:             Pass pass = {.type        = PASS_GRAPHICS,
	leaq	-4864(%rbp), %r14	#, tmp628
# helpers.h:166:     VkCommandBufferBeginInfo ci = {
	movq	$0, -4792(%rbp)	#, MEM[(struct VkCommandBufferBeginInfo *)_376].pNext
# helpers.h:173:     VK_CHECK(vkBeginCommandBuffer(cmd, &ci));
	movq	%r13, %rsi	# tmp445,
# main.c:416:         vk_cmd_begin(renderer.frames[renderer.current_frame].cmdbuf, false);
	salq	$5, %rax	#, tmp442
# helpers.h:166:     VkCommandBufferBeginInfo ci = {
	movl	$0, -4784(%rbp)	#, MEM[(struct VkCommandBufferBeginInfo *)_376].flags
	movq	$0, -4776(%rbp)	#, MEM[(struct VkCommandBufferBeginInfo *)_376].pInheritanceInfo
# main.c:416:         vk_cmd_begin(renderer.frames[renderer.current_frame].cmdbuf, false);
	movq	-2472(%rax,%rbp), %rdi	# renderer.frames[_60].cmdbuf, _61
# helpers.h:173:     VK_CHECK(vkBeginCommandBuffer(cmd, &ci));
	call	*vkBeginCommandBuffer(%rip)	# vkBeginCommandBuffer
# main.c:419:             vkCmdBindDescriptorSets(renderer.frames[renderer.current_frame].cmdbuf, VK_PIPELINE_BIND_POINT_GRAPHICS,
	movl	-2376(%rbp), %eax	# renderer.current_frame, renderer.current_frame
	xorl	%ecx, %ecx	#
	xorl	%esi, %esi	#
	leaq	-2352(%rbp), %r9	#,
	movl	$1, %r8d	#,
	salq	$5, %rax	#, tmp452
	movq	-2472(%rax,%rbp), %rdi	# renderer.frames[_64].cmdbuf, renderer.frames[_64].cmdbuf
	pushq	$0	#
	pushq	$0	#
	movq	-2344(%rbp), %rdx	# renderer.bindless_system.pipeline_layout,
	call	*vkCmdBindDescriptorSets(%rip)	# vkCmdBindDescriptorSets
# main.c:421:             vkCmdBindDescriptorSets(renderer.frames[renderer.current_frame].cmdbuf, VK_PIPELINE_BIND_POINT_COMPUTE,
	movl	-2376(%rbp), %eax	# renderer.current_frame, renderer.current_frame
	leaq	-2352(%rbp), %r9	#,
	xorl	%ecx, %ecx	#
	movl	$1, %r8d	#,
	movl	$1, %esi	#,
	salq	$5, %rax	#, tmp462
	movq	-2472(%rax,%rbp), %rdi	# renderer.frames[_68].cmdbuf, renderer.frames[_68].cmdbuf
	pushq	$0	#
	pushq	$0	#
	movq	-2344(%rbp), %rdx	# renderer.bindless_system.pipeline_layout,
	call	*vkCmdBindDescriptorSets(%rip)	# vkCmdBindDescriptorSets
# main.c:423:             image_transition_swapchain(renderer.frames[renderer.current_frame].cmdbuf, &renderer.swapchain,
	movl	-2376(%rbp), %eax	# renderer.current_frame, renderer.current_frame
	addq	$32, %rsp	#,
	movl	$256, %r8d	#,
	movl	$1024, %ecx	#,
	leaq	-2992(%rbp), %rsi	#, tmp624
	movl	$2, %edx	#,
	salq	$5, %rax	#, tmp471
	movq	%rsi, -5304(%rbp)	# tmp624, %sfp
	movq	-2472(%rax,%rbp), %rdi	# renderer.frames[_70].cmdbuf, renderer.frames[_70].cmdbuf
	call	image_transition_swapchain@PLT	#
# main.c:429:                                            .imageView = renderer.swapchain.image_views[renderer.swapchain.current_image],
	movl	-2672(%rbp), %eax	# renderer.swapchain.current_image, _72
# main.c:428:         VkRenderingAttachmentInfo color = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
	vpxor	%xmm0, %xmm0, %xmm0	# tmp475
	vmovaps	.LC23(%rip), %xmm7	#, tmp684
	vmovdqu8	%ymm0, -3996(%rbp)	# tmp475, MEM <char[68]> [(struct VkRenderingAttachmentInfo *)&color + 4B]
# main.c:207:         vkCmdBeginRendering(cmd, pass->graphics.rendering);
	movq	%r13, %rsi	# tmp445,
# main.c:428:         VkRenderingAttachmentInfo color = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
	movq	-2664(%rbp,%rax,8), %rdx	# renderer.swapchain.image_views[_72], renderer.swapchain.image_views[_72]
# main.c:430:                                            .imageLayout = renderer.swapchain.states[renderer.swapchain.current_image].layout,
	salq	$5, %rax	#, tmp481
# main.c:435:         VkRenderingInfo rendering = {.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
	vmovdqu8	%ymm0, -4796(%rbp)	# tmp475, MEM <char[68]> [(struct VkRenderingInfo *)_376 + 4B]
# main.c:428:         VkRenderingAttachmentInfo color = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
	movl	-2976(%rax,%rbp), %eax	# renderer.swapchain.states[_72].layout, renderer.swapchain.states[_72].layout
# main.c:435:         VkRenderingInfo rendering = {.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
	vmovdqu8	%ymm0, -4764(%rbp)	# tmp475, MEM <char[68]> [(struct VkRenderingInfo *)_376 + 4B]
# main.c:428:         VkRenderingAttachmentInfo color = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
	vmovdqu8	%ymm0, -3964(%rbp)	# tmp475, MEM <char[68]> [(struct VkRenderingAttachmentInfo *)&color + 4B]
	movl	%eax, -3976(%rbp)	# renderer.swapchain.states[_72].layout, color.imageLayout
# main.c:435:         VkRenderingInfo rendering = {.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
	movq	-2508(%rbp), %rax	# renderer.swapchain.extent, renderer.swapchain.extent
# main.c:428:         VkRenderingAttachmentInfo color = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
	vmovups	%xmm7, -3948(%rbp)	# tmp684, MEM[(union VkClearValue *)&color + 52B]
# main.c:442:             DrawCmd draw = {.type = DRAW_DIRECT,
	vmovdqa	.LC24(%rip), %xmm7	#, tmp685
# main.c:435:         VkRenderingInfo rendering = {.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
	movq	%rax, -4772(%rbp)	# renderer.swapchain.extent, MEM[(struct VkRenderingInfo *)_376].renderArea.extent
	leaq	-4000(%rbp), %rax	#, tmp488
	movq	%rax, -4752(%rbp)	# tmp488, MEM[(struct VkRenderingInfo *)_376].pColorAttachments
# main.c:447:             Pass pass = {.type        = PASS_GRAPHICS,
	movq	.LC25(%rip), %rax	#, tmp491
	vmovdqa	%ymm0, -3920(%rbp)	# tmp475, MEM <char[160]> [(struct Pass *)_377]
	vmovdqa	%ymm0, -3792(%rbp)	# tmp475, MEM <char[160]> [(struct Pass *)_377]
# main.c:442:             DrawCmd draw = {.type = DRAW_DIRECT,
	vmovdqu	%xmm7, -4856(%rbp)	# tmp685, MEM <vector(4) unsigned int> [(union  *)_256 + 8B]
# main.c:447:             Pass pass = {.type        = PASS_GRAPHICS,
	vmovdqa	%ymm0, -3888(%rbp)	# tmp475, MEM <char[160]> [(struct Pass *)_377]
	vmovdqa	%ymm0, -3856(%rbp)	# tmp475, MEM <char[160]> [(struct Pass *)_377]
	vmovdqa	%ymm0, -3824(%rbp)	# tmp475, MEM <char[160]> [(struct Pass *)_377]
# main.c:428:         VkRenderingAttachmentInfo color = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
	movq	%rdx, -3984(%rbp)	# renderer.swapchain.image_views[_72], color.imageView
# main.c:447:             Pass pass = {.type        = PASS_GRAPHICS,
	movq	%rax, -3912(%rbp)	# tmp491, MEM <vector(2) unsigned int> [(unsigned int *)_377 + 8B]
	movq	%r13, -3776(%rbp)	# tmp445, MEM[(struct Pass *)_377].D.92609.graphics.rendering
# main.c:428:         VkRenderingAttachmentInfo color = {.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO,
	movl	$0, -3932(%rbp)	#, MEM <char[68]> [(struct VkRenderingAttachmentInfo *)&color + 4B]
	movl	$1000044001, -4000(%rbp)	#, color.sType
	movl	$1, -3956(%rbp)	#, color.loadOp
# main.c:435:         VkRenderingInfo rendering = {.sType                = VK_STRUCTURE_TYPE_RENDERING_INFO,
	movl	$0, -4732(%rbp)	#, MEM <char[68]> [(struct VkRenderingInfo *)_376 + 4B]
	movl	$1000044000, -4800(%rbp)	#, MEM[(struct VkRenderingInfo *)_376].sType
	movl	$1, -4764(%rbp)	#, MEM[(struct VkRenderingInfo *)_376].layerCount
	movl	$1, -4756(%rbp)	#, MEM[(struct VkRenderingInfo *)_376].colorAttachmentCount
# main.c:442:             DrawCmd draw = {.type = DRAW_DIRECT,
	movl	$0, -4864(%rbp)	#, MEM[(struct DrawCmd *)_256].type
# main.c:447:             Pass pass = {.type        = PASS_GRAPHICS,
	movl	$1, -3768(%rbp)	#, MEM[(struct Pass *)_377].D.92609.graphics.draw_count
# main.c:454:             memcpy(pass.push_data, &push, sizeof(push));
	movq	-5344(%rbp), %rax	# %sfp, gpu_address
# main.c:447:             Pass pass = {.type        = PASS_GRAPHICS,
	movq	%r14, -3760(%rbp)	# tmp628, MEM[(struct Pass *)_377].D.92609.graphics.draws
# main.c:454:             memcpy(pass.push_data, &push, sizeof(push));
	movq	%rax, -3904(%rbp)	# gpu_address, MEM <unsigned long> [(char * {ref-all})_377 + 16B]
# main.c:456:             execute_pass(&renderer, &render_pipelines, renderer.frames[renderer.current_frame].cmdbuf, &pass);
	movl	-2376(%rbp), %eax	# renderer.current_frame, renderer.current_frame
	salq	$5, %rax	#, tmp496
	movq	-2472(%rax,%rbp), %rbx	# renderer.frames[_75].cmdbuf, _76
# main.c:207:         vkCmdBeginRendering(cmd, pass->graphics.rendering);
	vzeroupper
# main.c:217:         for(uint32_t i = 0; i < pass->graphics.draw_count; i++)
	xorl	%r13d, %r13d	# ivtmp.108
# main.c:207:         vkCmdBeginRendering(cmd, pass->graphics.rendering);
	movq	%rbx, %rdi	# _76,
	call	*vkCmdBeginRendering(%rip)	# vkCmdBeginRendering
# main.c:210:         vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline);
	movq	-5336(%rbp), %rdx	# %sfp,
	xorl	%esi, %esi	#
	movq	%rbx, %rdi	# _76,
	call	*vkCmdBindPipeline(%rip)	# vkCmdBindPipeline
# main.c:212:         vk_cmd_set_viewport_scissor(cmd, r->swapchain.extent);
	movq	-2508(%rbp), %rsi	# renderer.swapchain.extent,
	movq	%rbx, %rdi	# _76,
	call	vk_cmd_set_viewport_scissor@PLT	#
# main.c:214:         vkCmdPushConstants(cmd, r->bindless_system.pipeline_layout, pass->push_stages, 0, pass->push_size, pass->push_data);
	movl	-3912(%rbp), %edx	# MEM[(struct Pass *)_377].push_stages,
	movl	-3908(%rbp), %r8d	# MEM[(struct Pass *)_377].push_size,
	leaq	-3904(%rbp), %r9	#,
	movq	-2344(%rbp), %rsi	# renderer.bindless_system.pipeline_layout,
	xorl	%ecx, %ecx	#
	movq	%rbx, %rdi	# _76,
	call	*vkCmdPushConstants(%rip)	# vkCmdPushConstants
# main.c:217:         for(uint32_t i = 0; i < pass->graphics.draw_count; i++)
	movl	-3768(%rbp), %edx	# MEM[(struct Pass *)_377].D.92609.graphics.draw_count, prephitmp_441
# main.c:217:         for(uint32_t i = 0; i < pass->graphics.draw_count; i++)
	testl	%edx, %edx	# prephitmp_441
	je	.L28	#,
	.p2align 4,,10
	.p2align 3
.L18:
# main.c:219:             const DrawCmd* d = &pass->graphics.draws[i];
	leaq	0(%r13,%r13,2), %rax	#, _487
	salq	$4, %rax	#, tmp570
	addq	-3760(%rbp), %rax	# MEM[(struct Pass *)_377].D.92609.graphics.draws, d
# main.c:221:             switch(d->type)
	cmpl	$4, (%rax)	#, d_323->type
	ja	.L21	#,
	movl	(%rax), %edx	# d_323->type, d_323->type
	movslq	(%r12,%rdx,4), %rdx	#, tmp575
	addq	%r12, %rdx	# tmp642, tmp576
	jmp	*%rdx	# tmp576
	.section	.rodata
	.align 4
	.align 4
.L23:
	.long	.L27-.L23
	.long	.L26-.L23
	.long	.L25-.L23
	.long	.L24-.L23
	.long	.L22-.L23
	.section	.text.startup
.L24:
# main.c:240:                     vkCmdDrawIndexedIndirect(cmd, d->indexed_indirect.buffer, d->indexed_indirect.offset,
	movq	16(%rax), %rdx	# d_323->D.92581.indexed_indirect.offset, d_323->D.92581.indexed_indirect.offset
	movl	24(%rax), %ecx	# d_323->D.92581.indexed_indirect.drawCount, d_323->D.92581.indexed_indirect.drawCount
	movq	%rbx, %rdi	# _76,
	movq	8(%rax), %rsi	# d_323->D.92581.indexed_indirect.buffer, d_323->D.92581.indexed_indirect.buffer
	movl	28(%rax), %r8d	# d_323->D.92581.indexed_indirect.stride,
	call	*vkCmdDrawIndexedIndirect(%rip)	# vkCmdDrawIndexedIndirect
# main.c:217:         for(uint32_t i = 0; i < pass->graphics.draw_count; i++)
	movl	-3768(%rbp), %edx	# MEM[(struct Pass *)_377].D.92609.graphics.draw_count, prephitmp_441
.L21:
# main.c:217:         for(uint32_t i = 0; i < pass->graphics.draw_count; i++)
	incq	%r13	# ivtmp.108
	cmpl	%edx, %r13d	# prephitmp_441, ivtmp.108
	jb	.L18	#,
.L28:
# main.c:254:         vkCmdEndRendering(cmd);
	movq	%rbx, %rdi	# _76,
	call	*vkCmdEndRendering(%rip)	# vkCmdEndRendering
# main.c:458:         image_transition_swapchain(renderer.frames[renderer.current_frame].cmdbuf, &renderer.swapchain,
	movl	-2376(%rbp), %eax	# renderer.current_frame, renderer.current_frame
	movq	-5304(%rbp), %rsi	# %sfp,
	xorl	%r8d, %r8d	#
	movl	$65536, %ecx	#,
	movl	$1000001002, %edx	#,
	salq	$5, %rax	#, tmp514
	movq	-2472(%rax,%rbp), %rdi	# renderer.frames[_77].cmdbuf, renderer.frames[_77].cmdbuf
	call	image_transition_swapchain@PLT	#
# main.c:460:         vk_cmd_end(renderer.frames[renderer.current_frame].cmdbuf);
	movl	-2376(%rbp), %eax	# renderer.current_frame, renderer.current_frame
	salq	$5, %rax	#, tmp520
# helpers.h:177:     VK_CHECK(vkEndCommandBuffer(cmd));
	movq	-2472(%rax,%rbp), %rdi	# renderer.frames[_79].cmdbuf, renderer.frames[_79].cmdbuf
	call	*vkEndCommandBuffer(%rip)	# vkEndCommandBuffer
# main.c:464:             .commandBuffer = renderer.frames[renderer.current_frame].cmdbuf,
	movl	-2376(%rbp), %eax	# renderer.current_frame, _81
# main.c:462:         VkCommandBufferSubmitInfo cmd_info = {
	vpxor	%xmm1, %xmm1, %xmm1	# tmp525
# main.c:466:         VkSemaphoreSubmitInfo wait_info = {
	vpxor	%xmm0, %xmm0, %xmm0	# tmp532
# main.c:462:         VkCommandBufferSubmitInfo cmd_info = {
	vmovdqu8	%xmm1, -5228(%rbp)	# tmp525, MEM <char[28]> [(struct VkCommandBufferSubmitInfo *)&cmd_info + 4B]
# main.c:492:         VK_CHECK(vkQueueSubmit2(renderer.graphics_queue, 1, &submit, renderer.frames[renderer.current_frame].in_flight_fence));
	movq	-3080(%rbp), %rdi	# renderer.graphics_queue,
	movl	$1, %esi	#,
# main.c:464:             .commandBuffer = renderer.frames[renderer.current_frame].cmdbuf,
	addq	$20, %rax	#, tmp527
# main.c:462:         VkCommandBufferSubmitInfo cmd_info = {
	vmovdqu8	%xmm1, -5216(%rbp)	# tmp525, MEM <char[28]> [(struct VkCommandBufferSubmitInfo *)&cmd_info + 4B]
# main.c:464:             .commandBuffer = renderer.frames[renderer.current_frame].cmdbuf,
	salq	$5, %rax	#, tmp528
# main.c:466:         VkSemaphoreSubmitInfo wait_info = {
	vmovdqu8	%ymm0, -5148(%rbp)	# tmp532, MEM <char[44]> [(struct VkSemaphoreSubmitInfo *)&wait_info + 4B]
# main.c:462:         VkCommandBufferSubmitInfo cmd_info = {
	movq	-3112(%rbp,%rax), %rdx	# renderer.frames[_81].cmdbuf, renderer.frames[_81].cmdbuf
# main.c:474:         VkSemaphoreSubmitInfo signal_info = {
	vmovdqu8	%ymm0, -5100(%rbp)	# tmp532, MEM <char[44]> [(struct VkSemaphoreSubmitInfo *)&signal_info + 4B]
# main.c:492:         VK_CHECK(vkQueueSubmit2(renderer.graphics_queue, 1, &submit, renderer.frames[renderer.current_frame].in_flight_fence));
	movq	-3088(%rbp,%rax), %rcx	# renderer.frames[_81].in_flight_fence, renderer.frames[_81].in_flight_fence
# main.c:482:         VkSubmitInfo2 submit = {
	vmovdqu8	%ymm0, -4924(%rbp)	# tmp532, MEM <char[52]> [(struct VkSubmitInfo2 *)&submit + 4B]
# main.c:462:         VkCommandBufferSubmitInfo cmd_info = {
	movq	%rdx, -5216(%rbp)	# renderer.frames[_81].cmdbuf, cmd_info.commandBuffer
# main.c:466:         VkSemaphoreSubmitInfo wait_info = {
	movq	-3096(%rbp,%rax), %rdx	# renderer.frames[_81].image_available_semaphore, renderer.frames[_81].image_available_semaphore
# main.c:482:         VkSubmitInfo2 submit = {
	vmovdqu8	%ymm0, -4904(%rbp)	# tmp532, MEM <char[52]> [(struct VkSubmitInfo2 *)&submit + 4B]
# main.c:466:         VkSemaphoreSubmitInfo wait_info = {
	movq	%rdx, -5136(%rbp)	# renderer.frames[_81].image_available_semaphore, wait_info.semaphore
# main.c:476:             .semaphore   = renderer.swapchain.render_finished[renderer.swapchain.current_image],
	movl	-2672(%rbp), %edx	# renderer.swapchain.current_image, renderer.swapchain.current_image
# main.c:466:         VkSemaphoreSubmitInfo wait_info = {
	vmovdqu8	%xmm1, -5120(%rbp)	# tmp525, MEM <char[44]> [(struct VkSemaphoreSubmitInfo *)&wait_info + 4B]
# main.c:474:         VkSemaphoreSubmitInfo signal_info = {
	movq	-2600(%rbp,%rdx,8), %rdx	# renderer.swapchain.render_finished[_84], renderer.swapchain.render_finished[_84]
	vmovdqu8	%xmm1, -5072(%rbp)	# tmp525, MEM <char[44]> [(struct VkSemaphoreSubmitInfo *)&signal_info + 4B]
# main.c:462:         VkCommandBufferSubmitInfo cmd_info = {
	movl	$1000314006, -5232(%rbp)	#, cmd_info.sType
# main.c:474:         VkSemaphoreSubmitInfo signal_info = {
	movq	%rdx, -5088(%rbp)	# renderer.swapchain.render_finished[_84], signal_info.semaphore
# main.c:482:         VkSubmitInfo2 submit = {
	leaq	-5152(%rbp), %rdx	#, tmp544
	movq	%rdx, -4904(%rbp)	# tmp544, submit.pWaitSemaphoreInfos
	leaq	-5232(%rbp), %rdx	#, tmp545
	movq	%rdx, -4888(%rbp)	# tmp545, submit.pCommandBufferInfos
	leaq	-5104(%rbp), %rdx	#, tmp546
	movq	%rdx, -4872(%rbp)	# tmp546, submit.pSignalSemaphoreInfos
# main.c:492:         VK_CHECK(vkQueueSubmit2(renderer.graphics_queue, 1, &submit, renderer.frames[renderer.current_frame].in_flight_fence));
	leaq	-4928(%rbp), %rdx	#, tmp553
# main.c:466:         VkSemaphoreSubmitInfo wait_info = {
	movl	$1000314005, -5152(%rbp)	#, wait_info.sType
	movq	$1024, -5120(%rbp)	#, wait_info.stageMask
# main.c:474:         VkSemaphoreSubmitInfo signal_info = {
	movl	$1000314005, -5104(%rbp)	#, signal_info.sType
	movq	$65536, -5072(%rbp)	#, signal_info.stageMask
# main.c:482:         VkSubmitInfo2 submit = {
	movl	$1000314004, -4928(%rbp)	#, submit.sType
	movl	$1, -4908(%rbp)	#, submit.waitSemaphoreInfoCount
	movl	$1, -4896(%rbp)	#, submit.commandBufferInfoCount
	movl	$1, -4880(%rbp)	#, submit.signalSemaphoreInfoCount
# main.c:492:         VK_CHECK(vkQueueSubmit2(renderer.graphics_queue, 1, &submit, renderer.frames[renderer.current_frame].in_flight_fence));
	vzeroupper
	call	*vkQueueSubmit2(%rip)	# vkQueueSubmit2
# main.c:494:                              &renderer.swapchain.render_finished[renderer.swapchain.current_image], 1);
	movl	-2672(%rbp), %eax	# renderer.swapchain.current_image, _103
# vk.h:475:     VkPresentInfoKHR info = {.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
	vpxor	%xmm0, %xmm0, %xmm0	# tmp532
	vmovdqa	-5328(%rbp), %xmm7	# %sfp, _263
	vmovdqu8	%ymm0, -4860(%rbp)	# tmp532, MEM <char[60]> [(struct VkPresentInfoKHR *)_256 + 4B]
# vk.h:482:     VkResult r = vkQueuePresentKHR(present_queue, &info);
	movq	-3088(%rbp), %rdi	# renderer.present_queue,
	movq	%r14, %rsi	# tmp628,
# main.c:494:                              &renderer.swapchain.render_finished[renderer.swapchain.current_image], 1);
	leaq	520(%r15,%rax,8), %rax	#, _90
# vk.h:475:     VkPresentInfoKHR info = {.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
	vmovdqu8	%ymm0, -4832(%rbp)	# tmp532, MEM <char[60]> [(struct VkPresentInfoKHR *)_256 + 4B]
	movq	%rax, -4840(%rbp)	# _90, MEM[(struct VkPresentInfoKHR *)_256].pWaitSemaphores
	movl	$1000001001, -4864(%rbp)	#, MEM[(struct VkPresentInfoKHR *)_256].sType
	movl	$1, -4848(%rbp)	#, MEM[(struct VkPresentInfoKHR *)_256].waitSemaphoreCount
	movl	$1, -4832(%rbp)	#, MEM[(struct VkPresentInfoKHR *)_256].swapchainCount
	vmovdqu	%xmm7, -4824(%rbp)	# _263, MEM <vector(2) long unsigned int> [(void *)_256 + 40B]
# vk.h:482:     VkResult r = vkQueuePresentKHR(present_queue, &info);
	vzeroupper
	call	*vkQueuePresentKHR(%rip)	# vkQueuePresentKHR
# vk.h:484:     if(r == VK_SUBOPTIMAL_KHR || r == VK_ERROR_OUT_OF_DATE_KHR)
	cmpl	$1000001003, %eax	#, r
	je	.L19	#,
	cmpl	$-1000001004, %eax	#, r
	je	.L19	#,
.L20:
# main.c:495:         renderer.current_frame = (renderer.current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
	movl	-2376(%rbp), %eax	# renderer.current_frame, tmp688
# main.c:495:         renderer.current_frame = (renderer.current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
	movl	$2863311531, %ecx	#, tmp610
# main.c:495:         renderer.current_frame = (renderer.current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
	leal	1(%rax), %edx	#,
	movq	%rdx, %rax	#,
# main.c:495:         renderer.current_frame = (renderer.current_frame + 1) % MAX_FRAMES_IN_FLIGHT;
	imulq	%rcx, %rdx	# tmp610, tmp609
	shrq	$33, %rdx	#, tmp612
	leal	(%rdx,%rdx,2), %edx	#, tmp616
	subl	%edx, %eax	# tmp616, tmp617
	movl	%eax, -2376(%rbp)	# tmp617, renderer.current_frame
	jmp	.L8	#
.L25:
# main.c:234:                     vkCmdDrawIndexed(cmd, d->indexed.indexCount, d->indexed.instanceCount, d->indexed.firstIndex,
	movl	12(%rax), %edx	# d_323->D.92581.indexed.instanceCount, d_323->D.92581.indexed.instanceCount
	movl	16(%rax), %ecx	# d_323->D.92581.indexed.firstIndex, d_323->D.92581.indexed.firstIndex
	movq	%rbx, %rdi	# _76,
	movl	8(%rax), %esi	# d_323->D.92581.indexed.indexCount, d_323->D.92581.indexed.indexCount
	movl	24(%rax), %r9d	# d_323->D.92581.indexed.firstInstance,
	movl	20(%rax), %r8d	# d_323->D.92581.indexed.vertexOffset,
	call	*vkCmdDrawIndexed(%rip)	# vkCmdDrawIndexed
# main.c:217:         for(uint32_t i = 0; i < pass->graphics.draw_count; i++)
	movl	-3768(%rbp), %edx	# MEM[(struct Pass *)_377].D.92609.graphics.draw_count, prephitmp_441
# main.c:237:                 break;
	jmp	.L21	#
.L26:
# main.c:229:                     vkCmdDrawIndirect(cmd, d->indirect.buffer, d->indirect.offset, d->indirect.drawCount, d->indirect.stride);
	movq	16(%rax), %rdx	# d_323->D.92581.indirect.offset, d_323->D.92581.indirect.offset
	movl	24(%rax), %ecx	# d_323->D.92581.indirect.drawCount, d_323->D.92581.indirect.drawCount
	movq	%rbx, %rdi	# _76,
	movq	8(%rax), %rsi	# d_323->D.92581.indirect.buffer, d_323->D.92581.indirect.buffer
	movl	28(%rax), %r8d	# d_323->D.92581.indirect.stride,
	call	*vkCmdDrawIndirect(%rip)	# vkCmdDrawIndirect
# main.c:217:         for(uint32_t i = 0; i < pass->graphics.draw_count; i++)
	movl	-3768(%rbp), %edx	# MEM[(struct Pass *)_377].D.92609.graphics.draw_count, prephitmp_441
# main.c:231:                 break;
	jmp	.L21	#
.L27:
# main.c:224:                     vkCmdDraw(cmd, d->direct.vertexCount, d->direct.instanceCount, d->direct.firstVertex, d->direct.firstInstance);
	movl	12(%rax), %edx	# d_323->D.92581.direct.instanceCount, d_323->D.92581.direct.instanceCount
	movl	16(%rax), %ecx	# d_323->D.92581.direct.firstVertex, d_323->D.92581.direct.firstVertex
	movq	%rbx, %rdi	# _76,
	movl	8(%rax), %esi	# d_323->D.92581.direct.vertexCount, d_323->D.92581.direct.vertexCount
	movl	20(%rax), %r8d	# d_323->D.92581.direct.firstInstance,
	call	*vkCmdDraw(%rip)	# vkCmdDraw
# main.c:217:         for(uint32_t i = 0; i < pass->graphics.draw_count; i++)
	movl	-3768(%rbp), %edx	# MEM[(struct Pass *)_377].D.92609.graphics.draw_count, prephitmp_441
# main.c:226:                 break;
	jmp	.L21	#
.L22:
# main.c:246:                     vkCmdDrawIndexedIndirectCount(cmd, d->indexed_indirect_count.buffer, d->indexed_indirect_count.offset,
	movq	24(%rax), %rcx	# d_323->D.92581.indexed_indirect_count.countBuffer, d_323->D.92581.indexed_indirect_count.countBuffer
	movq	16(%rax), %rdx	# d_323->D.92581.indexed_indirect_count.offset, d_323->D.92581.indexed_indirect_count.offset
	subq	$8, %rsp	#,
	movq	%rbx, %rdi	# _76,
	movl	40(%rax), %r9d	# d_323->D.92581.indexed_indirect_count.maxDrawCount, d_323->D.92581.indexed_indirect_count.maxDrawCount
	movq	32(%rax), %r8	# d_323->D.92581.indexed_indirect_count.countOffset, d_323->D.92581.indexed_indirect_count.countOffset
	movq	8(%rax), %rsi	# d_323->D.92581.indexed_indirect_count.buffer, d_323->D.92581.indexed_indirect_count.buffer
	movl	44(%rax), %eax	# d_323->D.92581.indexed_indirect_count.stride, d_323->D.92581.indexed_indirect_count.stride
	pushq	%rax	# d_323->D.92581.indexed_indirect_count.stride
	call	*vkCmdDrawIndexedIndirectCount(%rip)	# vkCmdDrawIndexedIndirectCount
# main.c:250:                 break;
	popq	%rax	#
# main.c:217:         for(uint32_t i = 0; i < pass->graphics.draw_count; i++)
	movl	-3768(%rbp), %edx	# MEM[(struct Pass *)_377].D.92609.graphics.draw_count, prephitmp_441
# main.c:250:                 break;
	popq	%rcx	#
	jmp	.L21	#
.L19:
# vk.h:486:         sc->needs_recreate = true;
	movb	$1, -2491(%rbp)	#, MEM[(struct FlowSwapchain *)&renderer + 128B].needs_recreate
# vk.h:487:         return false;
	jmp	.L20	#
.L33:
# vk.h:465:         sc->needs_recreate = true;
	movb	$1, -2491(%rbp)	#, MEM[(struct FlowSwapchain *)&renderer + 128B].needs_recreate
# vk.h:466:         return false;
	jmp	.L16	#
.L2:
# main.c:290:         glfwInitHint(GLFW_PLATFORM, GLFW_PLATFORM_WAYLAND);
	movl	$393219, %esi	#,
	movl	$327683, %edi	#,
	call	glfwInitHint@PLT	#
	jmp	.L3	#
.L63:
# main.c:502: }
	call	__stack_chk_fail@PLT	#
	.cfi_endproc
.LFE8285:
	.size	main, .-main
	.local	g_framebuffer_resized
	.comm	g_framebuffer_resized,1,1
	.section	.rodata.cst8,"aM",@progbits,8
	.align 8
.LC0:
	.long	0
	.long	1
	.align 8
.LC5:
	.long	800
	.long	600
	.align 8
.LC8:
	.long	4369
	.long	7
	.align 8
.LC9:
	.long	1028
	.long	7
	.section	.rodata.cst4,"aM",@progbits,4
	.align 4
.LC10:
	.long	1074580685
	.set	.LC11,.LC18
	.align 4
.LC13:
	.long	1074601657
	.section	.rodata.cst8
	.align 8
.LC14:
	.long	1065353216
	.long	0
	.set	.LC15,.LC23+12
	.align 8
.LC18:
	.long	1082130432
	.long	0
	.align 8
.LC19:
	.long	0
	.long	1065353216
	.align 8
.LC20:
	.long	2
	.long	2
	.set	.LC21,.LC23
	.set	.LC22,.LC29
	.section	.rodata.cst16,"aM",@progbits,16
	.align 16
.LC23:
	.long	1036831949
	.long	1036831949
	.long	1036831949
	.long	1065353216
	.align 16
.LC24:
	.long	166665
	.long	1
	.long	0
	.long	0
	.section	.rodata.cst8
	.align 8
.LC25:
	.long	1
	.long	8
	.section	.rodata.cst16
	.align 16
.LC28:
	.long	1036831949
	.long	1036831949
	.long	0
	.long	0
	.align 16
.LC29:
	.long	1000593162
	.long	1000593162
	.long	0
	.long	0
	.section	.data.rel.ro.local,"aw"
	.align 8
.LC30:
	.quad	.LC1
	.ident	"GCC: (GNU) 15.2.1 20260103"
	.section	.note.GNU-stack,"",@progbits
