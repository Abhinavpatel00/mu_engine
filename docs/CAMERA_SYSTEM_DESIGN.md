# Camera System Design

## Goals
- Support both 2D and 3D cameras with one runtime system.
- Keep Vulkan integration compatible with the current bindless model (single descriptor set layout + single pipeline layout).
- Avoid per-draw descriptor churn; upload camera data once per frame and index/select cheaply.
- Make input-independent camera control (engine API does not depend on GLFW directly).
- Provide deterministic update order and smooth motion for editor and gameplay.

## Non-Goals (v1)
- Cinematic sequencer/timeline authoring UI.
- Full animation blending between camera tracks.
- Network replication policy (left to game layer).

---

## High-Level Architecture

1. **Camera Component Layer**
   - Stores camera state (`position`, `rotation`, `fov`, `near`, `far`, projection mode).
   - Supports perspective + orthographic projection.

2. **Camera Controller Layer**
   - Pluggable controllers (free-fly, orbit, first-person, follow-target, 2D pan/zoom).
   - Reads abstract input state and writes desired camera velocities/targets.

3. **Camera Solver Layer**
   - Integrates movement (accel/decel), optional spring smoothing, and constraints.
   - Produces final `view`, `proj`, `view_proj`, and camera frustum planes.

4. **Render Integration Layer**
   - Writes per-frame camera GPU block into a suballocated global buffer.
   - Uses offsets/device addresses in push constants (aligned with current renderer design).

---

## Data Model

## Core Types

```c
typedef enum CameraProjection {
    CAMERA_PROJ_PERSPECTIVE,
    CAMERA_PROJ_ORTHOGRAPHIC,
} CameraProjection;

typedef struct CameraLens {
    CameraProjection projection;
    float fov_y_radians;   // perspective
    float ortho_height;    // orthographic
    float near_plane;
    float far_plane;
} CameraLens;

typedef struct CameraTransform {
    float position[3];
    float rotation_quat[4];
} CameraTransform;

typedef struct CameraState {
    CameraTransform transform;
    CameraLens      lens;
    float           exposure;
    float           move_speed;
    float           look_speed;
    uint32_t        flags;
} CameraState;
```

## Derived Per-Frame Data

```c
typedef struct CameraMatrices {
    float view[16];
    float proj[16];
    float view_proj[16];
    float inv_view[16];
    float inv_proj[16];
    float inv_view_proj[16];
} CameraMatrices;

typedef struct CameraGpuData {
    CameraMatrices m;
    float position_ws[4];
    float frustum_planes[6][4];
    float jitter_xy[2];
    float _pad0[2];
} CameraGpuData;
```

Notes:
- Keep `CameraGpuData` 16-byte aligned.
- If many cameras are needed (shadow/reflection/editor), store an array in one GPU buffer and select by index.

---

## API Proposal

```c
// lifecycle
void camera_system_init(CameraSystem* s, Renderer* r, uint32_t max_cameras);
void camera_system_shutdown(CameraSystem* s);

// camera objects
CameraHandle camera_create(CameraSystem* s, const CameraState* initial);
void camera_destroy(CameraSystem* s, CameraHandle h);
CameraState* camera_get_mut(CameraSystem* s, CameraHandle h);

// per-frame
void camera_system_begin_frame(CameraSystem* s, float dt);
void camera_system_update_controller(CameraSystem* s, CameraHandle h, const InputState* in);
void camera_system_solve(CameraSystem* s, CameraHandle h, float dt);
void camera_system_build_gpu(CameraSystem* s, CameraHandle h, float aspect, bool reverse_z);

// renderer bridge
VkDeviceAddress camera_gpu_address(const CameraSystem* s, CameraHandle h);
uint32_t camera_gpu_offset(const CameraSystem* s, CameraHandle h);
```

Design choice:
- Keep camera update independent from render graph. Renderer only consumes final GPU data + offsets.

---

## Controller Set (Fully Featured Target)

1. **Free-Fly (Editor)**
   - WASD + vertical up/down + mouse look.
   - Shift boost, optional inertial damping.

2. **Orbit**
   - Target point + radius.
   - Rotate around target, dolly zoom, pan in view plane.

3. **First-Person**
   - Yaw/pitch clamped pitch.
   - Optional head bob and lean hooks.

4. **Follow Target**
   - Dead-zone and look-ahead.
   - Spring arm with collision hook.

5. **2D Pan/Zoom**
   - Orthographic projection.
   - Pixel-perfect option and bounds clamp.

Each controller writes to a shared `CameraIntent` to keep blending and transitions possible.

---

## Minimal Working First-Person Camera (Shortest Path)

If you want a **simple, working FPS camera first**, keep only one camera struct and update it directly inside the render loop.

```c
typedef struct FpsCamera {
   vec3  pos;
   float yaw, pitch;               // radians
   float move_speed;
   float look_speed;
   float fov_y, near_z, far_z;
} FpsCamera;

FpsCamera cam = {
   .pos = {0.0f, 1.8f, 4.0f},
   .yaw = 0.0f,
   .pitch = 0.0f,
   .move_speed = 4.0f,
   .look_speed = 0.0025f,
   .fov_y = glm_rad(75.0f),
   .near_z = 0.05f,
   .far_z = 2000.0f,
};
double last_mx = 0.0, last_my = 0.0;
glfwGetCursorPos(window, &last_mx, &last_my);
glfwSetInputMode(window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);

while (!glfwWindowShouldClose(window)) {
   glfwPollEvents();

   // --- mouse look ---
   double mx, my;
   glfwGetCursorPos(window, &mx, &my);
   float dx = (float)(mx - last_mx);
   float dy = (float)(my - last_my);
   last_mx = mx;
   last_my = my;

   cam.yaw   += dx * cam.look_speed;
   cam.pitch += dy * cam.look_speed;
   cam.pitch = glm_clamp(cam.pitch, -1.553343f, 1.553343f); // +/-89 deg

   // --- basis vectors from yaw/pitch ---
   vec3 fwd = {
      cosf(cam.pitch) * sinf(cam.yaw),
      sinf(cam.pitch),
     -cosf(cam.pitch) * cosf(cam.yaw)
   };
   glm_normalize(fwd);
   vec3 world_up = {0.0f, 1.0f, 0.0f};
   vec3 right; glm_vec3_cross(fwd, world_up, right); glm_normalize(right);
   vec3 up;    glm_vec3_cross(right, fwd, up);

   // --- WASD + QE movement (GLFW input) ---
   float dt = frame_dt_seconds;
   float speed = cam.move_speed * (glfwGetKey(window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS ? 3.0f : 1.0f);
   vec3 delta = {0};
   if (glfwGetKey(window, GLFW_KEY_W) == GLFW_PRESS) glm_vec3_muladds(fwd,   speed * dt, delta);
   if (glfwGetKey(window, GLFW_KEY_S) == GLFW_PRESS) glm_vec3_muladds(fwd,  -speed * dt, delta);
   if (glfwGetKey(window, GLFW_KEY_D) == GLFW_PRESS) glm_vec3_muladds(right, speed * dt, delta);
   if (glfwGetKey(window, GLFW_KEY_A) == GLFW_PRESS) glm_vec3_muladds(right,-speed * dt, delta);
   if (glfwGetKey(window, GLFW_KEY_E) == GLFW_PRESS) glm_vec3_muladds(world_up, speed * dt, delta);
   if (glfwGetKey(window, GLFW_KEY_Q) == GLFW_PRESS) glm_vec3_muladds(world_up,-speed * dt, delta);
   glm_vec3_add(cam.pos, delta, cam.pos);

   // --- build matrices ---
   mat4 view, proj, view_proj;
   vec3 center; glm_vec3_add(cam.pos, fwd, center);
   glm_lookat(cam.pos, center, up, view);

   // Reverse-Z: swap near/far in projection build.
   glm_perspective(cam.fov_y, swapchain_aspect, cam.far_z, cam.near_z, proj);
   proj[1][1] *= -1.0f; // Vulkan clip-space Y flip
   glm_mat4_mul(proj, view, view_proj);

   // upload view/proj/view_proj to camera GPU slot, then draw
}
```

Reverse-Z checklist (required):
1. Use the swapped near/far projection above.
2. Clear depth to `0.0` (not `1.0`).
3. Use depth compare `GREATER`/`GREATER_OR_EQUAL`.

---

## Math and Rendering Details

## Conventions
- Right-handed world, Vulkan clip space.
- Configurable depth mode:
  - Standard: $z \in [0, 1]$ with near/far.
  - Reverse-Z preferred for precision.

## Jitter Support (TAA-ready)
- Add per-frame subpixel jitter to projection.
- Store `jitter_xy` in `CameraGpuData`.

## Frustum
- Extract 6 planes from `view_proj` once per camera per frame.
- Use for CPU culling now; optional GPU culling later.

---

## Vulkan Integration Plan

Given current project constraints:
- Single descriptor set layout and single pipeline layout.
- Buffer suballocation + offsets passed through push constants.

Recommended integration:
1. Allocate one large camera buffer in `BufferPool`.
2. Suballocate one `CameraGpuData` slot per active camera per frame-in-flight.
3. Push either:
   - direct device address (`VkDeviceAddress camera_ptr`), or
   - compact camera index/offset if shader fetches from structured array.
4. Keep push constant struct <= 256 bytes.

Minimal push constants for draw path:

```c
typedef struct DrawPush {
    VkDeviceAddress vertex_ptr;
    VkDeviceAddress camera_ptr;
    uint32_t material_index;
    uint32_t _pad;
} DrawPush;
```

Shader side reads camera matrices from `camera_ptr`.

---

## Update Order

Per frame:
1. Poll platform input into `InputState`.
2. `camera_system_begin_frame(dt)`.
3. Controller update -> `CameraIntent`.
4. Solve + constraints -> final `CameraState`.
5. Build matrices/frustum/jitter.
6. Upload `CameraGpuData` to suballocated GPU memory and flush mapped range.
7. Record render commands and push camera address/offset.

This avoids temporal mismatch between input and rendered view.

---

## Serialization

Store camera presets as simple structs:
- transform, lens, controller type, controller params.
- versioned binary or JSON for tools.

Suggested fields:
- `name`, `projection`, `fov/ortho`, `near/far`, `speed`, `exposure`, `controller`.

---

## Debugging / Tooling

ImGui panel ideas:
- Active camera selector.
- Projection toggle and lens controls.
- Position/rotation numeric edit.
- Frustum visualization toggle.
- Controller mode and sensitivity.
- “Snap to defaults” and preset save/load.

Validation checks:
- `near_plane > 0` and `far_plane > near_plane`.
- Quaternion normalized before matrix build.
- NaN/Inf guard for matrices.

---

## Rollout Plan

## Phase 1 (Minimal Production)
- Core camera state + perspective/ortho.
- Free-fly + 2D pan/zoom.
- GPU upload via suballocated buffer and push `camera_ptr`.

## Phase 2
- Orbit + follow-target controllers.
- Frustum extraction and CPU culling hooks.
- Preset serialization.

## Phase 3
- Jitter/TAA integration.
- Multi-camera render passes (scene + shadow + reflection + minimap).
- Camera blending/transitions.

---

## Risks and Mitigations
- **Risk:** push constant bloat.
  - **Mitigation:** push only addresses/indices; keep full matrices in GPU buffer.
- **Risk:** camera/controller coupling with GLFW.
  - **Mitigation:** strict `InputState` abstraction.
- **Risk:** precision artifacts at long distances.
  - **Mitigation:** reverse-Z and sensible near plane.

---

## Suggested File Layout

- camera.h / camera.c (core state + math)
- camera_controller.h / camera_controller.c
- camera_gpu.h / camera_gpu.c (upload + Vulkan bridge)
- camera_debug_ui.h / camera_debug_ui.c (optional ImGui)

This split keeps rendering concerns isolated from gameplay/editor control logic.
