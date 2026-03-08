uint32_t squirrel_noise5(int position, uint32_t seed)
{
    const uint32_t BIT_NOISE1 = 0xd2a80a3f;
    const uint32_t BIT_NOISE2 = 0xa884f197;
    const uint32_t BIT_NOISE3 = 0x6c736f4b;

    uint32_t mangled = position;
    mangled *= BIT_NOISE1;
    mangled += seed;
    mangled ^= (mangled >> 8);
    mangled += BIT_NOISE2;
    mangled ^= (mangled << 8);
    mangled *= BIT_NOISE3;
    mangled ^= (mangled >> 8);

    return mangled;
}
