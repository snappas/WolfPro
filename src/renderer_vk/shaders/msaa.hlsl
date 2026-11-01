
// X3571: pow(f, e) won't work if f is negative
#pragma warning(disable : 3571)

struct RootConstants
{
	float invGamma;
	float brightness;
};

[[vk::push_constant]] RootConstants rc;

Texture2DMS<unorm float4> src;
[[vk::image_format("rgba8")]] RWTexture2D<unorm float4> dst;

[numthreads(8,8,1)]
void cs(uint3 dtID : SV_DispatchThreadID) 
{
    uint dstWidth;
    uint dstHeight;
    dst.GetDimensions(dstWidth, dstHeight);
    if(dtID.x >= dstWidth || dtID.y >= dstHeight){
        return;
    }
    
    float4 accum = float4(0, 0, 0, 0);
    for(int i = 0; i < SAMPLE_COUNT; i++){
        //accum += src.Load(dtID.xy, i);
        float4 sampled = src.Load(dtID.xy, i);
        float3 gc = pow(sampled.rgb, rc.invGamma) * rc.brightness;
        accum += float4(gc, sampled.a);

    }

    dst[dtID.xy] = accum / float(SAMPLE_COUNT);


}
