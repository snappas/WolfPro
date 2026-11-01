
#if PS

float CorrectAlpha(Texture2D texture0, SamplerState sampler0, float threshold, float alpha, float2 tc, float alphaBoost)
{
	float w, h;
	texture0.GetDimensions(w, h);
	if(min(w, h) <= 8.0)
		return alpha >= threshold ? 1.0 : 0.0;
	alpha *= 1.0 + alphaBoost * texture0.CalculateLevelOfDetail(sampler0, tc);
	float2 dtc = fwidth(tc * float2(w, h));
	float recScale = max(0.25 * (dtc.x + dtc.y), 1.0 / 16384.0);
	float scale = max(1.0 / recScale, 1.0);
	float ac = threshold + (alpha - threshold) * scale;

	return ac;
}

void AlphaTest(Texture2D texture0, SamplerState sampler0, uint alphaTest, float2 texCoords, float alphaBoost, inout float alpha){
#ifdef A2C
	//       a <  0.5
	//     - a > -0.5
	// 1.0 - a >  0.5
	// 1.0 - a >= NextFloatAbove(0.5)
	// 1.0 - a >= asfloat(0x3F000001)
	// asfloat(0x3F000001) is 0.5 * 1.0000001192092896
	if(alphaTest == 1)
		alpha = alpha > 0.0 ? 1.0 : 0.0;
	else if(alphaTest == 2)
		alpha = CorrectAlpha(texture0, sampler0, asfloat(0x3F000001), 1.0 - alpha, texCoords, alphaBoost);
	else if(alphaTest == 3)
		alpha = CorrectAlpha(texture0, sampler0, 0.5, alpha, texCoords, alphaBoost);
#else
	if((alphaTest == 1 && alpha == 0.0) ||
	   (alphaTest == 2 && alpha >= 0.5) ||
	   (alphaTest == 3 && alpha <  0.5))
	   discard;
#endif
}

#endif


struct SceneView
{
  matrix projectionMatrix;
  float4 clipPlane;
};
[[vk::binding(2)]] ConstantBuffer<SceneView> sceneView;
#if TRACE
[[vk::binding(3)]] RWByteAddressBuffer shaderIndex;
#endif

float4 unpackColorR8G8B8A8(uint color){
    uint4 res = uint4(color, (color >> 8), (color >> 16), (color >> 24)) & uint4(0xFF, 0xFF, 0xFF, 0xFF);
    return float4(res)/255.0;
}

float4 unpackColorR11G11B10(uint color){
    uint4 res = uint4(color, (color >> 11), (color >> 22), 0xFF) & uint4(0x7FF, 0x7FF, 0x3FF, 0xFF);
    return float4(res)/float4(2047.0, 2047.0, 1023.0, 255.0);
}

uint2 unpackPixelCenter(uint pixelCenterXY){
	return uint2(pixelCenterXY & 0xFFFFu, pixelCenterXY >> 16u);
}

struct ShaderIndex {
	uint shaderIndex;
	uint writeIndex;
	uint writeEnabled;
};

ShaderIndex unpackShaderIndex(uint packed){
	ShaderIndex idx;
	idx.shaderIndex = packed & 0x1FFF;
	idx.writeIndex = (packed >> 13u) & 1u;
	idx.writeEnabled = (packed >> 14u) & 1u;
	return idx;
}