struct Push
{
    uint64_t voxel_ptr;
    uint64_t face_ptr;
    uint chunk_size;
};

[[vk::push_constant]]
Push push;

struct PackedFace
{
    uint data0;
    uint data1;
};

[numthreads(8,8,8)]
[shader("compute")]
void cs_main(uint3 tid : SV_DispatchThreadID)
{
    uint x = tid.x;
    uint y = tid.y;
    uint z = tid.z;

    if(x>=push.chunk_size ||
       y>=push.chunk_size ||
       z>=push.chunk_size)
        return;

    uint voxelIndex =
        x +
        y*push.chunk_size +
        z*push.chunk_size*push.chunk_size;

    StructuredBuffer<uint> voxels =
        (StructuredBuffer<uint>)push.voxel_ptr;

    RWStructuredBuffer<uint2> faces =
        (RWStructuredBuffer<uint2>)push.face_ptr;

    uint type = voxels[voxelIndex];

    if(type==0)
        return;

    uint base = voxelIndex*3;

    faces[base+0] =
        uint2((x)|(y<<8)|(z<<16)|(0<<30), type);

    faces[base+1] =
        uint2((x)|(y<<8)|(z<<16)|(1<<30), type);

    faces[base+2] =
        uint2((x)|(y<<8)|(z<<16)|(2<<30), type);
}
