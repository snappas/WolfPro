//.\dxc.exe -spirv -T vs_6_0 -E vs C:\Users\Ryan\Documents\GitHub\RTCW-MP\src\renderer_vk\shaders\triangle.hlsl -Fh triangle_vs.h -Vn triangle_vs
//.\dxc.exe -spirv -T ps_6_0 -E ps C:\Users\Ryan\Documents\GitHub\RTCW-MP\src\renderer_vk\shaders\triangle.hlsl -Fh triangle_ps.h -Vn triangle_ps
#include "generic.hlsli"

struct VOut
{
    [[vk::location(0)]] float4 position : SV_Position;
    [[vk::location(1)]] float4 color : COLOR0;
    [[vk::location(2)]] float2 tc : TEXCOORD0;
};

#if VS

struct RootConstants
{
	matrix modelViewMatrix;
};
[[vk::push_constant]] RootConstants rc;

struct VIn
{
    [[vk::location(0)]] float4 position : SV_Position;
    [[vk::location(1)]] float4 color : COLOR0;
    [[vk::location(2)]] float2 tc : TEXCOORD0;
};



VOut vs(VIn input)
{
    VOut output;
	output.position = mul(sceneView.projectionMatrix, mul(rc.modelViewMatrix, float4(input.position.xyz, 1.0)));
    output.color = input.color;
    output.tc = input.tc;

    return output;
}

#endif

#if PS

struct RootConstants
{
    [[vk::offset(64)]]
    uint packedData;
    uint pixelCenterXY;
    uint shaderIndex;
};
[[vk::push_constant]] RootConstants rc;

#include "game_textures.hlsli"

struct UnpackedData {
    uint textureIndex; //11 bits
    uint samplerIndex; //2
    uint alphaTest; //2
    float alphaBoost; //8
};

UnpackedData unpackPushConstants(uint packed){
    UnpackedData unpack;
    unpack.textureIndex = packed & 0x7FFu; //11 bits
    unpack.samplerIndex = (packed >> 11u) & 3u; //2
    unpack.alphaTest = (packed >> 13u) & 0x3u;
    unpack.alphaBoost = float((packed >> 15u) & 0xFFu) / 255.0;
    return unpack;
}

#if !AT
[earlydepthstencil]
#endif
float4 ps(VOut input) : SV_Target
{
     
    UnpackedData uc = unpackPushConstants(rc.packedData);

    float4 color = input.color * texture[uc.textureIndex].Sample(mySampler[uc.samplerIndex], input.tc);
    #if AT
    AlphaTest(texture[uc.textureIndex], mySampler[uc.samplerIndex], uc.alphaTest, input.tc, uc.alphaBoost, color.a);
    #endif

#if TRACE
    ShaderIndex index = unpackShaderIndex(rc.shaderIndex);
    uint2 XY = unpackPixelCenter(rc.pixelCenterXY);
    if(all(uint2(input.position.xy) == XY) && index.writeEnabled){
        shaderIndex.Store(index.writeIndex * 4, index.shaderIndex);
    }
#endif

    return color;
}

#endif