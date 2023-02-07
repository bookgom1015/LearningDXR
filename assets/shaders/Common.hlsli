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
	float4x4	gUnitViewProj;
	float4x4	gViewProjTex;
	float4x4	gShadowTransform;
	float3		gEyePosW;
	float		gPassConstantsPad0;
	float4		gAmbientLight;
	Light		gLights[MaxLights];
};

cbuffer cbRootConstants : register(b1) {
	uint gInstanceID;
	bool gIsRaytracing;
}

StructuredBuffer<ObjectData> gObjects		: register(t0, space1);
StructuredBuffer<MaterialData> gMaterials	: register(t0, space2);

Texture2D gColorMap							: register(t0);
Texture2D gAlbedoMap						: register(t1);
Texture2D gNormalMap						: register(t2);
Texture2D gDepthMap							: register(t3);
Texture2D gSpecularMap						: register(t4);
Texture2D gShadowMap						: register(t5);
Texture2D gDxrShadowMap						: register(t6);

float CalcShadowFactor(float4 shadowPosH) {
	shadowPosH.xyz /= shadowPosH.w;

	float depth = shadowPosH.z;

	uint width, height, numMips;
	gShadowMap.GetDimensions(0, width, height, numMips);

	float dx = 1.0f / (float)width;

	float percentLit = 0.0f;
	const float2 offsets[9] = {
		float2(-dx,  -dx), float2(0.0f,  -dx), float2(dx,  -dx),
		float2(-dx, 0.0f), float2(0.0f, 0.0f), float2(dx, 0.0f),
		float2(-dx,  +dx), float2(0.0f,  +dx), float2(dx,  +dx)
	};

	[unroll]
	for (int i = 0; i < 9; ++i) {
		percentLit += gShadowMap.SampleCmpLevelZero(gsamShadow, shadowPosH.xy + offsets[i], depth).r;
	}

	return percentLit / 9.0f;
}

float NdcDepthToViewDepth(float z_ndc) {
	// z_ndc = A + B/viewZ, where gProj[2,2]=A and gProj[3,2]=B.
	float viewZ = gProj[3][2] / (z_ndc - gProj[2][2]);
	return viewZ;
}

#endif // __COMMON_HLSLI__