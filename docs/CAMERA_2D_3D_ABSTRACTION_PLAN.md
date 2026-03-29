# 2D + 3D Camera Abstraction Plan

## Objective
Create one camera system that supports both 2D and 3D gameplay/editor use-cases while keeping renderer integration compatible with the current engine constraints:
- Single descriptor set layout and single pipeline layout.
- Buffer suballocation model.
- Camera GPU data referenced by buffer slice offsets passed via push constants.
- No vertex/index buffer assumptions in camera API.

## Design Principles
- One core camera type with mode-specific parameters.
- One update path per frame (deterministic order).
- Input-agnostic camera logic (controllers consume abstract input state).
- Renderer-facing API exposes offsets/device addresses only.
- 2D and 3D share culling/framing metadata where possible.

## Proposed Abstraction

### 1) Camera Mode
```c
typedef enum CameraMode {
    CAMERA_MODE_2D,
    CAMERA_MODE_3D,
} CameraMode;
```

### 2) Projection Kind
```c
typedef enum CameraProjection {
    CAMERA_PROJ_ORTHOGRAPHIC,
    CAMERA_PROJ_PERSPECTIVE,
} CameraProjection;
```

### 3) Unified Camera State
```c
typedef struct CameraState {
    CameraMode mode;
    CameraProjection projection;

    // transform
    float position[3];
    float rotation_quat[4];

    // common lens
    float near_plane;
    float far_plane;

    // perspective (3D)
    float fov_y_radians;

    // orthographic (2D)
    float ortho_height_world;
    float zoom;

    // behavior
    float move_speed;
    float look_speed;
    uint32_t flags;
} CameraState;
```

### 4) Derived Per-Frame Camera Data
```c
typedef struct CameraFrameData {
    float view[16];
    float proj[16];
    float view_proj[16];
    float inv_view[16];
    float inv_proj[16];
    float inv_view_proj[16];
    float position_ws[4];
    float frustum_planes[6][4];
    float viewport_size_px[2];
    float jitter_xy[2];
} CameraFrameData;
```

## Function Plan (C API)

### Lifecycle
```c
void camera_system_init(CameraSystem* system, Renderer* renderer, uint32_t max_cameras);
void camera_system_shutdown(CameraSystem* system);
void camera_system_begin_frame(CameraSystem* system, float dt_seconds);
void camera_system_end_frame(CameraSystem* system);
```

### Camera Management
```c
CameraHandle camera_create(CameraSystem* system, const CameraState* initial);
void camera_destroy(CameraSystem* system, CameraHandle handle);
CameraState* camera_state_mut(CameraSystem* system, CameraHandle handle);
const CameraState* camera_state(const CameraSystem* system, CameraHandle handle);
```

### Mode/Projection Configuration
```c
void camera_set_mode(CameraSystem* system, CameraHandle handle, CameraMode mode);
void camera_set_projection(CameraSystem* system, CameraHandle handle, CameraProjection projection);
void camera_set_perspective(CameraSystem* system, CameraHandle handle, float fov_y_radians, float near_plane, float far_plane);
void camera_set_orthographic(CameraSystem* system, CameraHandle handle, float ortho_height_world, float zoom, float near_plane, float far_plane);
```

### 2D Utility Functions
```c
void camera2d_set_position(CameraSystem* system, CameraHandle handle, float x, float y);
void camera2d_pan(CameraSystem* system, CameraHandle handle, float dx_world, float dy_world);
void camera2d_zoom(CameraSystem* system, CameraHandle handle, float zoom_delta);
void camera2d_set_bounds(CameraSystem* system, CameraHandle handle, float min_x, float min_y, float max_x, float max_y);
void camera2d_world_to_screen(const CameraSystem* system, CameraHandle handle, float world_x, float world_y, float* out_x, float* out_y);
void camera2d_screen_to_world(const CameraSystem* system, CameraHandle handle, float screen_x, float screen_y, float* out_x, float* out_y);
```

### 3D Utility Functions
```c
void camera3d_set_position(CameraSystem* system, CameraHandle handle, float x, float y, float z);
void camera3d_set_rotation_euler(CameraSystem* system, CameraHandle handle, float yaw, float pitch, float roll);
void camera3d_look_at(CameraSystem* system, CameraHandle handle, float target_x, float target_y, float target_z);
void camera3d_move_local(CameraSystem* system, CameraHandle handle, float right, float up, float forward);
void camera3d_set_clip(CameraSystem* system, CameraHandle handle, float near_plane, float far_plane);
```

### Controller and Update
```c
typedef struct CameraInput {
    float move_x;
    float move_y;
    float move_z;
    float look_x;
    float look_y;
    float zoom;
    uint32_t buttons;
} CameraInput;

void camera_apply_input(CameraSystem* system, CameraHandle handle, const CameraInput* input, float dt_seconds);
void camera_update_matrices(CameraSystem* system, CameraHandle handle, float aspect_ratio, bool reverse_z);
```

### Renderer Bridge (Offset-Based)
```c
// writes CameraFrameData for one camera into a suballocated buffer slice
void camera_upload_gpu(CameraSystem* system, CameraHandle handle);

// renderer consumes these in push constants
uint32_t camera_gpu_offset(const CameraSystem* system, CameraHandle handle);
VkDeviceAddress camera_gpu_device_address(const CameraSystem* system, CameraHandle handle);
```

## Frame Flow
1. `camera_system_begin_frame(dt)`
2. Apply inputs/controllers (`camera_apply_input`)
3. Solve constraints/smoothing
4. Build matrices (`camera_update_matrices`)
5. Upload (`camera_upload_gpu`)
6. Renderer binds pipeline + descriptor set and pushes camera offset/device address

## 2D/3D Behavior Rules
- 2D defaults to orthographic projection.
- 3D defaults to perspective projection.
- Mode changes retain transform but reinitialize lens defaults.
- All matrices are produced by the same function path to reduce divergence bugs.
- Reverse-Z path is available for 3D perspective and optional for orthographic.

## Phased Implementation Plan

### Phase 1: Core Data + API Skeleton
- Add `camera_system.h/.c` with types and lifecycle.
- Add handle pool and storage arrays for `CameraState` and `CameraFrameData`.
- Add mode/projection setters and validation.

### Phase 2: Matrix and Conversion Functions
- Implement perspective + ortho matrix generation.
- Implement 2D screen/world conversion helpers.
- Implement 3D local movement and `look_at`.

### Phase 3: Renderer Integration
- Suballocate GPU camera buffer slices per active camera.
- Upload `CameraFrameData` once per frame.
- Expose `camera_gpu_offset` and `camera_gpu_device_address`.

### Phase 4: Controllers
- Implement minimal 2D pan/zoom controller.
- Implement minimal 3D free-fly controller.
- Keep controller layer optional and detached from window/input backend.

### Phase 5: Migration and Cleanup
- Replace direct camera math in render path with camera system queries.
- Keep temporary compatibility wrappers for old call sites.
- Remove wrappers after all passes consume new API.

## Validation Checklist
- 2D: pan, zoom, clamp bounds, world/screen conversion correctness.
- 3D: free-fly movement, look-at correctness, clip planes.
- Renderer: all passes read correct camera slice offsets.
- Reverse-Z: depth clear + compare op compatibility.
- No per-draw descriptor updates for camera state.

## Integration Notes for This Engine
- Camera GPU payload should be tightly packed and 16-byte aligned.
- Use existing offset allocator for camera buffer suballocation.
- Push constants carry camera offset (and/or device address) to shaders.
- Keep this system independent from text rendering and ECS layout until Phase 5.

## Suggested File Layout
- `camera_system.h`
- `camera_system.c`
- `camera_controllers.h`
- `camera_controllers.c`
- Optional migration notes in `docs/CAMERA_SYSTEM_DESIGN.md`

## Success Criteria
- One API serves both 2D and 3D camera needs.
- Camera setup/update no longer duplicated across render passes.
- Renderer consumes camera data only through offsets/device addresses.
- New camera mode/features can be added without changing renderer-facing contract.
