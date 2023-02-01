#ifndef __COMMON_HLSLI__
#define __COMMON_HLSLI__

#include "LightingUtil.hlsli"
#include "Samplers.hlsli"

struct ObjectData {
	float4x4	World;
	float4x4	TexTransform;
	uint		GeometryIndex;
	int			MaterialIndex;
};

struct MaterialData {
	float4		DiffuseAlbedo;
	float3		FresnelR0;
	float		Roughness;
	float4x4	MatTransform;
};

cbuffer cbPass : register(b0) {
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

cbuffer cbRootConstants : register(b1) {
	uint gInstanceID;
}

StructuredBuffer<ObjectData>	gObjects	: register(t0, space1);
StructuredBuffer<MaterialData>	gMaterials	: register(t0, space2);

#endif // __COMMON_HLSLI__