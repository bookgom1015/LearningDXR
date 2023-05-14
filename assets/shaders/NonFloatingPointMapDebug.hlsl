#ifndef __NONFLOATINGPOINTMAPDEUBG_HLSL__
#define __NONFLOATINGPOINTMAPDEUBG_HLSL__

#ifndef HLSL
#define HLSL
#endif

#include "./../../include/HlslCompaction.h"
#include "CoordinatesFittedToScreen.hlsli"

ConstantBuffer<DebugConstants> cb : register(b0);

cbuffer cbRootConstants : register(b1) {
	uint2 gTextureDim;
}

Texture2D<uint4> gTsppValueSquaredMeanRayHitDistance	: register(t0);
Texture2D<uint> gTspp									: register(t1);

struct VertexOut {
	float4 PosH		: SV_POSITION;
	float2 TexC		: TEXCOORD;
};

VertexOut VS(uint vid : SV_VertexID) {
	VertexOut vout = (VertexOut)0.0f;

	vout.TexC = gTexCoords[vid];

	// Quad covering screen in NDC space.
	float2 pos = float2(2 * vout.TexC.x - 1, 1 - 2 * vout.TexC.y) * 0.35 + float2(0.65, -0.65);

	// Already in homogeneous clip space.
	vout.PosH = float4(pos, 0, 1);

	return vout;
}

float4 PS(VertexOut pin) : SV_Target{
	uint2 srcIndex = pin.TexC * gTextureDim - 0.5;
	const float3 MinTsppColor = float3(153, 18, 15) / 255;
	const float3 MaxTsppColor = float3(170, 220, 200) / 255;
	uint tspp = gTspp[srcIndex];
	float normalizedTspp = min(1.f, tspp / 22);
	return float4(lerp(MinTsppColor, MaxTsppColor, normalizedTspp), 1);
}

#endif // __NONFLOATINGPOINTMAPDEUBG_HLSL__