
[[vk::binding(0)]] [[vk::image_format("rgba8")]] RWTexture2D<unorm float4> mips[12];

int2 TexTC(int2 tc, int2 dims, bool clampMode){
    int2 maxTC = dims - int2(1,1);
    if(clampMode){
        return clamp(tc, int2(0,0), maxTC);
    }else{
        return (tc + 16 * dims) & maxTC;
    }
    
}

float4 gtol(float4 input){
    return float4(input.rgb * input.rgb, input.a);
}

float4 ltog(float4 input){
    return float4(sqrt(input.rgb), input.a);
}