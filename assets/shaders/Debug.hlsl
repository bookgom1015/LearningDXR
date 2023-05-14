#ifndef __DEBUG_HLSL__
#define __DEBUG_HLSL__

#ifndef HLSL
#define HLSL
#endif

#include "./../../include/HlslCompaction.h"
#include "Samplers.hlsli"
#include "CoordinatesFittedToScreen.hlsli"

ConstantBuffer<DebugConstants> cb : register(b0);

cbuffer gRootConstants : register(b1) {
	uint gDisplayMask0;
	uint gDisplayMask1;
	uint gDisplayMask2;
	uint gDisplayMask3;
	uint gDisplayMask4;
}

Texture2D gDebugMap0	: register(t0);
Texture2D gDebugMap1	: register(t1);
Texture2D gDebugMap2	: register(t2);
Texture2D gDebugMap3	: register(t3);
Texture2D gDebugMap4	: register(t4);

static const float2 gOffsets[DebugShadeParams::MapCount] = {
	float2(-0.8,  0.8),
	float2(-0.8,  0.4),
	float2(-0.8,  0.0),
	float2(-0.8, -0.4),
	float2(-0.8, -0.8)
	//float2(0.8,  0.8),
	//float2(0.8,  0.4),
	//float2(0.8,  0.0),
	//float2(0.8, -0.4),
	//float2(0.8, -0.8)
};

struct VertexOut {
	float4 PosH		: SV_POSITION;
	float2 TexC		: TEXCOORD;
	uint   InstID	: INSTID;
};

VertexOut VS(uint vid : SV_VertexID, uint instanceID : SV_InstanceID) {
	VertexOut vout = (VertexOut)0;

	vout.TexC = gTexCoords[vid];
	vout.InstID = instanceID;

	// Quad covering screen in NDC space.
	float2 pos = float2(2 * vout.TexC.x - 1, 1 - 2 * vout.TexC.y) * 0.2 + gOffsets[instanceID];

	// Already in homogeneous clip space.
	vout.PosH = float4(pos, 0, 1);

	return vout;
}

uint GetSampleMask(int index) {
	switch (index) {
	case 0: return gDisplayMask0;
	case 1: return gDisplayMask1;
	case 2: return gDisplayMask2;
	case 3: return gDisplayMask3;
	case 4: return gDisplayMask4;
	}
	return 0;
}

float4 SampleColor(in Texture2D map, int index, float2 tex) {
	uint mask = GetSampleMask(index);
	switch (mask) {
	case DebugShadeParams::DisplayMark::RGB: {
		float3 samp = map.SampleLevel(gsamPointClamp, tex, 0).rgb;
		return float4(samp, 1);
	}
	case DebugShadeParams::DisplayMark::RG: {
		float2 samp = map.SampleLevel(gsamPointClamp, tex, 0).rg;
		return float4(samp, 0, 1);
	}
	case DebugShadeParams::DisplayMark::RRR: {
		float3 samp = map.SampleLevel(gsamPointClamp, tex, 0).rrr;
		return float4(samp, 1);
	}
	case DebugShadeParams::DisplayMark::GGG: {
		float3 samp = map.SampleLevel(gsamPointClamp, tex, 0).ggg;
		return float4(samp, 1);
	}
	case DebugShadeParams::DisplayMark::AAA: {
		float3 samp = map.SampleLevel(gsamPointClamp, tex, 0).aaa;
		return float4(samp, 1);
	}
	case DebugShadeParams::DisplayMark::RayHitDist: {
		const float3 MinDistanceColor = float3(15, 18, 153) / 255;
		const float3 MaxDistanceColor = float3(170, 220, 200) / 255;
		float hitDistance = map.SampleLevel(gsamPointClamp, tex, 0).x;
		float hitCoef = hitDistance / cb.RtaoOcclusionRadius;
		return hitCoef >= 0 ? float4(lerp(MinDistanceColor, MaxDistanceColor, hitCoef), 1) : 1;
	}
	}
	return 1;
}

float4 PS(VertexOut pin) : SV_Target {
	switch (pin.InstID) {
	case 0: return SampleColor(gDebugMap0, 0, pin.TexC);
	case 1: return SampleColor(gDebugMap1, 1, pin.TexC);
	case 2: return SampleColor(gDebugMap2, 2, pin.TexC);
	case 3: return SampleColor(gDebugMap3, 3, pin.TexC);
	case 4: return SampleColor(gDebugMap4, 4, pin.TexC);
	}
	return 0;
}

#endif // __DEBUG_HLSL__