#ifndef __COMMON_HLSLI__
#define __COMMON_HLSLI__

#include "LightingUtil.hlsli"
#include "Samplers.hlsli"

struct ObjectData {
	float4x4	World;
	float4x4	PrevWorld;
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
	float4x4	gPrevViewProj;
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
Texture2D gVelocityMap						: register(t5);
Texture2D gShadowMap						: register(t6);
Texture2D gDxrShadowMap0					: register(t7);
Texture2D gDxrShadowMap1					: register(t8);
Texture2D gAmbientMap0						: register(t9);
Texture2D gAmbientMap1						: register(t10);
Texture2D gRandomVector						: register(t11);
Texture2D gDxrAmbientMap0					: register(t12);
Texture2D gDxrAmbientMap1					: register(t13);

RWTexture2D<float> guDxrShadowMap0			: register(u0);
RWTexture2D<float> guDxrShadowMap1			: register(u1);
RWTexture2D<float> guDxrAmbientMap0			: register(u2);
RWTexture2D<float> guDxrAmbientMap1			: register(u3);
RWTexture2D<uint> guAccumulation			: register(u4);

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

float2 CalcVelocity(float4 curr_pos, float4 prev_pos) {
	curr_pos.xy = (curr_pos.xy + (float2)1.0f) / 2.0f;
	curr_pos.y = 1.0f - curr_pos.y;

	prev_pos.xy = (prev_pos.xy + (float2)1.0f) / 2.0f;
	prev_pos.y = 1.0f - prev_pos.y;

	return (curr_pos - prev_pos).xy;
}

#endif // __COMMON_HLSLI__