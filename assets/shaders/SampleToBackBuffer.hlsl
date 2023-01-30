#ifndef __SAMPLETOBACKBUFFER_HLSL__
#define __SAMPLETOBACKBUFFER_HLSL__

#include "Common.hlsli"

static const float2 gTexCoords[6] = {
	float2(0.0f, 1.0f),
	float2(0.0f, 0.0f),
	float2(1.0f, 0.0f),
	float2(0.0f, 1.0f),
	float2(1.0f, 0.0f),
	float2(1.0f, 1.0f)
};

struct VertexOut {
	float4 PosH		: SV_POSITION;
	float2 TexC		: TEXCOORD;
};

VertexOut VS(uint vid : SV_VertexID) {
	VertexOut vout = (VertexOut)0.0f;

	vout.TexC = gTexCoords[vid];
	vout.PosH = float4(2.0f * vout.TexC.x - 1.0f, 1.0f - 2.0f * vout.TexC.y, 0.0f, 1.0f);

	return vout;
}

float4 PS(VertexOut pin) : SV_Target {
	uint x = (uint)(pin.TexC.x * 800.0f);
	uint y = (uint)(pin.TexC.y * 600.0f);

	return gUAV.Load(uint2(x, y));
}

#endif // __SAMPLETOBACKBUFFER_HLSL__