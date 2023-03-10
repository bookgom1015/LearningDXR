#ifndef __DEBUG_HLSL__
#define __DEBUG_HLSL__

#include "Common.hlsli"

static const float2 gTexCoords[6] = {
	float2(0.0f, 1.0f),
	float2(0.0f, 0.0f),
	float2(1.0f, 0.0f),
	float2(0.0f, 1.0f),
	float2(1.0f, 0.0f),
	float2(1.0f, 1.0f)
};

static const float2 gOffsets[10] = {
	float2(-0.8f, 0.8f),
	float2(-0.8f, 0.4f),
	float2(-0.8f, 0.0f),
	float2(-0.8f, -0.4f),
	float2(-0.8f, -0.8f),
	float2(0.8f, 0.8f),
	float2(0.8f, 0.4f),
	float2(0.8f, 0.0f),
	float2(0.8f, -0.4f),
	float2(0.8f, -0.8f)
};

struct VertexOut {
	float4 PosH		: SV_POSITION;
	float2 TexC		: TEXCOORD;
	uint   InstID	: INSTID;
};

VertexOut VS(uint vid : SV_VertexID, uint instanceID : SV_InstanceID) {
	VertexOut vout = (VertexOut)0.0f;

	vout.TexC = gTexCoords[vid];
	vout.InstID = instanceID;

	// Quad covering screen in NDC space.
	float2 pos = float2(2.0f * vout.TexC.x - 1.0f, 1.0f - 2.0f * vout.TexC.y) * 0.2f + gOffsets[instanceID];

	// Already in homogeneous clip space.
	vout.PosH = float4(pos, 0.0f, 1.0f);

	return vout;
}

float4 PS(VertexOut pin) : SV_Target{
	switch (pin.InstID) {
	case 0: return float4(gColorMap.Sample(gsamPointClamp, pin.TexC).rgb, 1.0f);
	case 1: return float4(gNormalMap.Sample(gsamPointClamp, pin.TexC).rgb, 1.0f);
	case 2: return float4(gVelocityMap.Sample(gsamPointClamp, pin.TexC).rg, 0.0f, 1.0f);
	case 3: return float4(gAmbientMap0.Sample(gsamPointClamp, pin.TexC).rrr, 1.0f);
	case 4: return float4(gDxrAmbientMap0.Sample(gsamPointClamp, pin.TexC).rrr, 1.0f);
	default: return (float4)1.0f;
	}
}

#endif // __DEBUG_HLSL__