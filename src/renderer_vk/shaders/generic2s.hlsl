//.\dxc.exe -spirv -T vs_6_0 -E vs C:\Users\Ryan\Documents\GitHub\RTCW-MP\src\renderer_vk\shaders\triangle.hlsl -Fh triangle_vs.h -Vn triangle_vs
//.\dxc.exe -spirv -T ps_6_0 -E ps C:\Users\Ryan\Documents\GitHub\RTCW-MP\src\renderer_vk\shaders\triangle.hlsl -Fh triangle_ps.h -Vn triangle_ps
#include "generic.hlsli"

struct VOut
{
    [[vk::location(0)]] float4 position : SV_Position;
    [[vk::location(1)]] float2 tc1 : TEXCOORD0;
    [[vk::location(2)]] float2 tc2 : TEXCOORD1;
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
    [[vk::location(1)]] float2 tc1 : TEXCOORD0;
    [[vk::location(2)]] float2 tc2 : TEXCOORD1;
};



VOut vs(VIn input)
{
    float4 positionVS = mul(rc.modelViewMatrix, float4(input.position.xyz, 1.0));
    VOut output;
	output.position = mul(sceneView.projectionMatrix, positionVS);
    output.tc1 = input.tc1;
    output.tc2 = input.tc2;

    return output;
}

#endif

#if PS

struct RootConstants
{
    [[vk::offset(64)]]
    //texture index 11 bits 22
    //sampler index 2 bits 4
    //alpha test 2 bits
    //texenv 2 bits
    uint packedData; 
    uint pixelCenterXY;
    uint shaderIndex;
    
};
[[vk::push_constant]] RootConstants rc;

#include "game_textures.hlsli"

struct UnpackedConstants {
    uint textureIndex1;
    uint samplerIndex1;
    uint textureIndex2;
    uint samplerIndex2;
};

UnpackedConstants unpackConstants(uint packed){
    UnpackedConstants unpack;
    unpack.textureIndex1 = packed & 0x7FFu;
    unpack.samplerIndex1 = (packed >> 11u) & 3u;
    unpack.textureIndex2 = (packed >> 13u) & 0x7FFu;
    unpack.samplerIndex2 = (packed >> 24u) & 3u;
    return unpack;
}

float4 texEnv(float4 p, float4 s, uint texEnv){
    if(texEnv == 100){ //MODULATE
        return p*s;
    }else if(texEnv == 101){ //ADD
        float3 c = p.rgb + s.rgb;
        float a = p.a * s.a;
        return float4(c,a);
    }else{ //DECAL
        float3 c = s.rgb * s.a + p.rgb * (1.0 - s.a);
        float a = p.a;
        return float4(c,a);
    }
}


[earlydepthstencil]
float4 ps(VOut input) : SV_Target
{
    
#if TRACE
    ShaderIndex index = unpackShaderIndex(rc.shaderIndex);
    uint2 XY = unpackPixelCenter(rc.pixelCenterXY);
    if(all(uint2(input.position.xy) == XY) && index.writeEnabled){
        shaderIndex.Store(index.writeIndex * 4, index.shaderIndex);
    }
#endif

    UnpackedConstants uc = unpackConstants(rc.packedData);
    float4 color1 = texture[uc.textureIndex1].Sample(mySampler[uc.samplerIndex1], input.tc1);
    float4 color2 = texture[uc.textureIndex2].Sample(mySampler[uc.samplerIndex2], input.tc2);
    return color1 * color2;
}

#endif