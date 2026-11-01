struct RootConstants
{
    uint dstIndex; 
    uint clampMode;
    uint srcWidth;
    uint srcHeight;
    float4 weights;
};

[[vk::push_constant]] RootConstants rc;

#include "mipmap.hlsli"


[[vk::binding(1)]] [[vk::image_format("rgba16f")]] RWTexture2D<float4> src;



[numthreads(8,8,1)]
void cs(uint3 dtID : SV_DispatchThreadID) 
{
    RWTexture2D<unorm float4> dst = mips[rc.dstIndex];
    uint dstWidth;
    uint dstHeight;
    dst.GetDimensions(dstWidth, dstHeight);
    if(dtID.x >= dstWidth || dtID.y >= dstHeight){
        return;
    }


    uint2 srcDims = uint2(rc.srcWidth, rc.srcHeight);
    
    // float4 a = src[TexTC(uint2(dtID.x, dtID.y * 2), srcDims, rc.clampMode != 0)];
    // float4 b = src[TexTC(uint2(dtID.x, dtID.y * 2 + 1), srcDims, rc.clampMode != 0)];

    // dst[dtID.xy] = 0.5 * (a + b);
    float4 result = 
    rc.weights.w * src[TexTC(uint2(dtID.x, dtID.y * 2 - 3), srcDims, rc.clampMode != 0)] + 
    rc.weights.z * src[TexTC(uint2(dtID.x, dtID.y * 2 - 2), srcDims, rc.clampMode != 0)] +
    rc.weights.y * src[TexTC(uint2(dtID.x, dtID.y * 2 - 1), srcDims, rc.clampMode != 0)] + 
    rc.weights.x * src[TexTC(uint2(dtID.x, dtID.y * 2 + 0), srcDims, rc.clampMode != 0)] + 
    rc.weights.x * src[TexTC(uint2(dtID.x, dtID.y * 2 + 1), srcDims, rc.clampMode != 0)] +
    rc.weights.y * src[TexTC(uint2(dtID.x, dtID.y * 2 + 2), srcDims, rc.clampMode != 0)] + 
    rc.weights.z * src[TexTC(uint2(dtID.x, dtID.y * 2 + 3), srcDims, rc.clampMode != 0)] + 
    rc.weights.w * src[TexTC(uint2(dtID.x, dtID.y * 2 + 4), srcDims, rc.clampMode != 0)];

    dst[dtID.xy] = ltog(result);
}
