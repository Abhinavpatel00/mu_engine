# MeshX Format Specification (Draft v2)

Status: Draft  
Audience: Runtime, tooling, and content pipeline developers  
Date: 2026-04-27

## 1. Goals

MeshX v2 is designed to be a practical game-asset format with:

1. Stable runtime ingestion.
2. Full PBR material support.
3. Skinned and static mesh support.
4. Animation clips (skeletal and morph).
5. Deterministic parsing via formal grammar.
6. Forward-compatible extension mechanism.

This spec intentionally favors explicit data contracts over implicit defaults.

## 2. Design Principles

1. Parseability first: strict grammar, explicit token forms.
2. Runtime-friendly: supports direct upload paths and precomputed metadata.
3. Versioned contract: clear major/minor compatibility behavior.
4. Extensible without breakage: unknown optional blocks are skippable.
5. Tooling parity: textual MeshX and binary MeshXB share the same logical schema.

## 3. File Families

1. meshx: canonical text format for authoring, debugging, diffs.
2. meshxb: binary transport/runtime form with equivalent semantics.

This document defines the logical schema and the text grammar. A companion MeshXB mapping should serialize the same section model.

## 4. Versioning Rules

Header:

```text
meshx <major>.<minor>
```

Compatibility:

1. Major version mismatch: loader must fail.
2. Minor version newer than loader:
   1. Required unknown fields/sections: fail.
   2. Optional unknown fields/sections: skip with warning.
3. Writers should emit the minimum minor version required by used features.

## 5. High-Level Schema

A file is a set of named sections:

1. header
2. asset
3. buffers
4. mesh
5. materials
6. textures
7. samplers
8. skeletons optional
9. skins optional
10. animations optional
11. morph_targets optional
12. extensions optional

Recommended section order is fixed for readability, but parser may accept any order except that header must come first.

## 6. Formal Text Grammar (EBNF)

## 6.1 Lexical

```ebnf
letter          = "A".."Z" | "a".."z" | "_" ;
digit           = "0".."9" ;
hex_digit       = digit | "A".."F" | "a".."f" ;
ident           = letter, { letter | digit | "_" | "." | "/" | "-" } ;
integer         = ["-"], digit, { digit } ;
float           = ["-"], digit, { digit }, [".", { digit }], [ ("e"|"E"), ["+"|"-"], digit, { digit } ] ;
string          = '"', { any_char_except_quote_or_newline | escape }, '"' ;
boolean         = "true" | "false" ;
newline         = "\n" ;
comment         = "#", { any_char_except_newline }, newline ;
ws              = { " " | "\t" } ;
```

## 6.2 Structural

```ebnf
file            = header_line, newline, { top_level_block | comment | newline } ;

header_line     = "meshx", ws, version ;
version         = integer, ".", integer ;

top_level_block = asset_block
                | buffers_block
                | mesh_block
                | materials_block
                | textures_block
                | samplers_block
                | skeletons_block
                | skins_block
                | animations_block
                | morph_targets_block
                | extensions_block ;

block_open      = "{" ;
block_close     = "}" ;

kv_line         = ident, ws, value, newline ;
value           = integer | float | string | boolean | ident ;
```

Each concrete block below narrows valid keys and nested statements.

## 7. Core Geometry Sections

## 7.1 asset block

```text
asset {
  generator "meshx_converter 2.0"
  source "path/or/uri"
  unit_meters 1.0
  up_axis Y
  handedness right
}
```

Required keys:

1. unit_meters
2. up_axis
3. handedness

## 7.2 buffers block

Declares logical vertex/index streams and format.

```text
buffers {
  vertex_layout {
    position f32x3
    normal f32x3
    tangent f32x4
    uv0 f32x2
    uv1 f32x2 optional
    color0 unorm8x4 optional
    joints0 u16x4 optional
    weights0 unorm16x4 optional
  }

  vertex_count 1234
  index_type u32
  index_count 5678
}
```

Rules:

1. position is required.
2. tangent required for normal-mapped workflows unless material disables normal usage.
3. joints0 and weights0 are required for skinned meshes.

## 7.3 mesh block

```text
mesh "Character" {
  bounds {
    min -1.0 -1.0 -1.0
    max  1.0  2.0  1.0
  }

  submesh 0 {
    material 0
    vertex_offset 0
    vertex_count 800
    index_offset 0
    index_count 1200
    skin 0 optional
  }

  submesh 1 {
    material 1
    vertex_offset 800
    vertex_count 434
    index_offset 1200
    index_count 4478
  }
}
```

Submesh required keys:

1. material
2. vertex_offset
3. vertex_count
4. index_offset
5. index_count

## 8. Materials

Material model is physically-based and glTF-aligned where possible.

## 8.1 materials block

```text
materials {
  material 0 {
    name "Body"

    workflow metallic_roughness

    base_color_factor 1.0 1.0 1.0 1.0
    metallic_factor 0.0
    roughness_factor 0.7

    emissive_factor 0.0 0.0 0.0
    normal_scale 1.0
    occlusion_strength 1.0

    alpha_mode opaque
    alpha_cutoff 0.5
    double_sided false

    base_color_tex 0 optional
    normal_tex 1 optional
    orm_tex 2 optional
    emissive_tex 3 optional
    opacity_tex 4 optional

    sampler_override 0 optional

    clearcoat {
      enabled false
      factor 0.0
      roughness 0.0
      tex -1
      roughness_tex -1
      normal_tex -1
    }

    transmission {
      enabled false
      factor 0.0
      tex -1
    }

    sheen {
      enabled false
      color 0.0 0.0 0.0
      roughness 0.0
      color_tex -1
      roughness_tex -1
    }
  }
}
```

Material required keys:

1. workflow
2. base_color_factor
3. metallic_factor
4. roughness_factor
5. alpha_mode
6. double_sided

Material validation:

1. alpha_mode must be one of opaque, mask, blend.
2. alpha_cutoff is used only when alpha_mode is mask.
3. texture references must be valid indices into textures block, unless set to -1 for none.

## 9. Texture and Sampler Metadata

## 9.1 textures block

```text
textures {
  texture 0 {
    name "body_albedo"
    uri "body_albedo.ktx2"
    colorspace srgb
    kind sampled_2d
    mip_mode precomputed
  }

  texture 1 {
    name "body_normal"
    uri "body_normal.ktx2"
    colorspace linear
    kind sampled_2d
    mip_mode precomputed
  }
}
```

Required keys:

1. uri
2. colorspace
3. kind

## 9.2 samplers block

```text
samplers {
  sampler 0 {
    mag_filter linear
    min_filter linear
    mip_filter linear
    wrap_u repeat
    wrap_v repeat
    wrap_w repeat
    anisotropy 16.0 optional
  }
}
```

## 10. Skinning and Skeletons

## 10.1 skeletons block

```text
skeletons {
  skeleton 0 {
    name "Humanoid"
    joint_count 54

    joint 0 {
      name "root"
      parent -1
      bind_t 0 0 0
      bind_r 0 0 0 1
      bind_s 1 1 1
    }

    joint 1 {
      name "spine"
      parent 0
      bind_t 0 0.9 0
      bind_r 0 0 0 1
      bind_s 1 1 1
    }
  }
}
```

## 10.2 skins block

```text
skins {
  skin 0 {
    skeleton 0
    joint_count 54
    inverse_bind_matrices {
      m 0  ...16 floats...
      m 1  ...16 floats...
    }
  }
}
```

Rules:

1. skin joint_count must match skeleton joint_count.
2. joints0/weights0 streams must exist for skinned submeshes.

## 11. Morph Targets

```text
morph_targets {
  target_set 0 {
    name "Face"
    target 0 {
      name "Smile"
      delta_position_stream "morph/smile_pos.bin"
      delta_normal_stream "morph/smile_nrm.bin" optional
      delta_tangent_stream "morph/smile_tan.bin" optional
    }
  }
}
```

Submesh may reference a target_set via optional key:

```text
morph_set 0
```

## 12. Animation

## 12.1 animations block

```text
animations {
  clip 0 {
    name "Run"
    duration 0.933333
    ticks_per_second 30.0
    additive false

    channel 0 {
      target skeleton_joint
      target_index 1
      path translation
      interpolation linear
      key_count 4
      keys {
        k 0.0   0.0 0.0 0.0
        k 0.333 0.0 0.1 0.0
        k 0.666 0.0 0.2 0.0
        k 0.933 0.0 0.0 0.0
      }
    }

    channel 1 {
      target skeleton_joint
      target_index 1
      path rotation
      interpolation linear
      key_count 4
      keys {
        k 0.0   0.0 0.0 0.0 1.0
        k 0.333 0.0 0.2 0.0 0.98
        k 0.666 0.0 0.1 0.0 0.99
        k 0.933 0.0 0.0 0.0 1.0
      }
    }
  }
}
```

Animation channel constraints:

1. path is one of translation, rotation, scale, weights.
2. rotation keys are quaternion x y z w.
3. translation/scale keys are vec3.
4. weights keys are N floats where N equals morph target count.
5. key times must be strictly ascending.

Recommended optional data:

1. root_motion_channel index.
2. events list with (time, id_or_name).
3. compression metadata.

## 13. Extension Mechanism

Extensions are named feature blocks under extensions.

```text
extensions {
  ext "com.mu.material_anisotropy" {
    version 1
    data {
      material 0 {
        anisotropy_strength 0.65
        anisotropy_rotation 0.2
        anisotropy_tex 7
      }
    }
  }
}
```

Rules:

1. Extension names should be namespaced, for example com.mu.*.
2. Unknown extension blocks are skippable unless listed in required_extensions.

Optional header key:

```text
required_extensions "com.mu.material_anisotropy;com.mu.foobar"
```

If any required extension is unsupported, load must fail.

## 14. Defaults

To minimize ambiguity, defaults are explicit and minimal.

Material defaults:

1. base_color_factor = 1 1 1 1
2. metallic_factor = 1
3. roughness_factor = 1
4. emissive_factor = 0 0 0
5. normal_scale = 1
6. occlusion_strength = 1
7. alpha_mode = opaque
8. alpha_cutoff = 0.5
9. double_sided = false

Texture/sampler defaults should not be silently synthesized in import tools unless requested.

## 15. Validation Checklist

A compliant validator should check:

1. Grammar correctness and section ordering constraints.
2. Version compatibility.
3. Index bounds across submeshes, materials, textures, samplers, skeletons, skins.
4. Vertex/index bounds per submesh.
5. Required streams for feature usage (for example skinning needs joints/weights).
6. Animation channel path/value arity correctness.
7. Monotonic keyframe times.
8. Extension requirement fulfillment.

## 16. Runtime-Oriented Recommendations

1. Keep text meshx for tooling and tests.
2. Convert to meshxb at build time for shipping.
3. Precompute bounds per submesh and per clip root motion envelope.
4. Pre-normalize skin weights and validate sums.
5. Use texture URIs that map directly to your asset system or virtual filesystem.

## 17. Migration Strategy From Current MeshX

Phase 1:

1. Add version header and strict parser mode.
2. Preserve current geometry/material fields.
3. Add new fields as optional.

Phase 2:

1. Introduce textures/samplers blocks and index-based references.
2. Introduce skeleton/skin sections.
3. Support animation clips for skeletal channels.

Phase 3:

1. Add morph target animation channels.
2. Add extension namespace support.
3. Introduce meshxb binary writer/reader with parity tests.

## 18. Minimal Complete Example

```text
meshx 2.0

asset {
  generator "mu_meshx_tool"
  source "Barbarian.glb"
  unit_meters 1.0
  up_axis Y
  handedness right
}

buffers {
  vertex_layout {
    position f32x3
    normal f32x3
    tangent f32x4
    uv0 f32x2
    joints0 u16x4
    weights0 unorm16x4
  }
  vertex_count 6050
  index_type u32
  index_count 21369
}

textures {
  texture 0 { uri "barbarian_texture.ktx2" colorspace srgb kind sampled_2d }
}

samplers {
  sampler 0 { mag_filter linear min_filter linear mip_filter linear wrap_u repeat wrap_v repeat wrap_w repeat }
}

materials {
  material 0 {
    name "Barbarian_mat"
    workflow metallic_roughness
    base_color_factor 1 1 1 1
    metallic_factor 0
    roughness_factor 0.5
    emissive_factor 0 0 0
    normal_scale 1
    occlusion_strength 1
    alpha_mode opaque
    alpha_cutoff 0.5
    double_sided true
    base_color_tex 0
    sampler_override 0
  }
}

skeletons {
  skeleton 0 {
    name "Rig"
    joint_count 23
  }
}

skins {
  skin 0 {
    skeleton 0
    joint_count 23
    inverse_bind_matrices {
      m 0 1 0 0 0  0 1 0 0  0 0 1 0  0 0 0 1
    }
  }
}

mesh "Barbarian" {
  bounds { min -0.97 0.0 -0.52  max 0.97 2.39 0.73 }
  submesh 0 {
    material 0
    vertex_offset 0
    vertex_count 6050
    index_offset 0
    index_count 21369
    skin 0
  }
}

animations {
  clip 0 {
    name "Idle"
    duration 1.0
    ticks_per_second 30.0
    additive false
  }
}
```

## 19. Open Decisions

1. Coordinate compression policy for meshxb.
2. Quaternion tangent support for cubic interpolation.
3. Built-in material extension set versus pure extension blocks.
4. Clip event schema standardization.
5. Streaming chunk boundaries for large worlds.

## 20. Definition of Done for MeshX v2

MeshX v2 is ready when:

1. Parser and validator pass full conformance tests.
2. GLTF to MeshX conversion preserves static, skinned, and animated assets.
3. Runtime can render static + skinned + morph meshes with full PBR materials.
4. Text and binary forms round-trip without semantic loss.
5. Unknown optional extensions are safely ignored and required ones correctly gate loading.
