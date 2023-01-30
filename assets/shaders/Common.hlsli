#ifndef __COMMON_HLSLI__
#define __COMMON_HLSLI__

#include "LightingUtil.hlsl"
#include "Samplers.hlsl"

cbuffer cbPerObject : register(b0) {
	float4x4 gWorld;
	float4x4 gTexTransform;
};

cbuffer cbPass : register(b1) {
	float4x4	gView;
	float4x4	gInvView;
	float4x4	gProj;
	float4x4	gInvProj;
	float4x4	gViewProj;
	float4x4	gInvViewProj;
	float3		gEyePosW;
	float		gPassConstantsPad1;
	float4		gAmbientLight;
	Light		gLights[MaxLights];
};

cbuffer cbMaterial : register(b2) {
	float4		gDiffuseAlbedo;
	float3		gFresnelR0;
	float		gRoughness;
	float4x4	gMatTransform;
};

#endif // __COMMON_HLSLI__