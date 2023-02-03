#ifndef __GIZMO_HLSL__
#define __GIZMO_HLSL__

#include "Common.hlsli"

static const float3 gVertices[6] = {
	float3(0.0f, 0.0f, 0.0f),
	float3(0.25f, 0.0f, 0.0f),
	float3(0.0f, 0.0f, 0.0f),
	float3(0.0f, 0.25f, 0.0f),
	float3(0.0f, 0.0f, 0.0f),
	float3(0.0f, 0.0f, 0.25f)
};

struct VertexOut {
	float4	PosH	: SV_POSITION;
	uint	InstID	: INSTACNE_ID;
};

VertexOut VS(uint vid : SV_VertexID, uint instanceID : SV_InstanceID) {
	VertexOut vout = (VertexOut)0.0f;

	int index = vid + (instanceID * 2);
	float3 posW = gVertices[index];

	vout.PosH = mul(float4(posW, 1.0f), gUnitViewProj);
	vout.InstID = instanceID;

	return vout;
}

float4 PS(VertexOut pin) : SV_Target{
	if (pin.InstID == 0) return float4(1.0f, 0.0f, 0.0f, 1.0f);
	else if (pin.InstID == 1) return float4(0.0f, 1.0f, 0.0f, 1.0f);
	else return float4(0.0f, 0.0f, 1.0f, 1.0f);
}

#endif // __GIZMO_HLSL__