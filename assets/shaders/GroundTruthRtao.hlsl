#ifndef __GROUNDTRUTHRTAO_HLSL__
#define __GROUNDTRUTHRTAO_HLSL__

#include "Samplers.hlsli"

cbuffer cbRootConstants : register (b0) {
	uint gAccumulation;
};

Texture2D gNormalMap					: register(t0);
Texture2D gDepthMap						: register(t1);
Texture2D gVelocityMap					: register(t2);

RWTexture2D<float> gLastAmbientMap		: register(u0);
RWTexture2D<float> gAccumAmbientMap		: register(u1);
RWTexture2D<uint> gAccumulationMap		: register(u2);

static const float2 gTexCoords[6] = {
	float2(0.0f, 1.0f),
	float2(0.0f, 0.0f),
	float2(1.0f, 0.0f),
	float2(0.0f, 1.0f),
	float2(1.0f, 0.0f),
	float2(1.0f, 1.0f)
};

struct VertexOut {
	float4 PosH : SV_POSITION;
	float2 TexC : TEXCOORD0;
};

VertexOut VS(uint vid : SV_VertexID) {
	VertexOut vout;

	vout.TexC = gTexCoords[vid];

	vout.PosH = float4(2.0f * vout.TexC.x - 1.0f, 1.0f - 2.0f * vout.TexC.y, 0.0f, 1.0f);

	return vout;
}

void PS(VertexOut pin) {
	uint width, height;
	gLastAmbientMap.GetDimensions(width, height);

	uint2 tex = uint2(width * pin.TexC.x, height * pin.TexC.y);

	float curr = gLastAmbientMap[tex];
	float prev = gAccumAmbientMap[tex];

	float final = (gAccumulation * prev + curr) / (gAccumulation + 1);

	gLastAmbientMap[tex] = final;
	gAccumAmbientMap[tex] = final;
}

#endif // __GROUNDTRUTHRTAO_HLSL__