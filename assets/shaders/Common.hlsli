#ifndef __COMMON_HLSLI__
#define __COMMON_HLSLI__

#ifndef HLSL
#define HLSL
#endif

#include "./../../include/HlslCompaction.h"
#include "LightingUtil.hlsli"
#include "Samplers.hlsli"

ConstantBuffer<PassConstants> cbPass : register(b0);

cbuffer cbRootConstants : register(b1) {
	uint gInstanceID;
	bool gIsRaytracing;
}

StructuredBuffer<ObjectData> gObjects										: register(t0, space1);
StructuredBuffer<MaterialData> gMaterials									: register(t0, space2);

Texture2D gColorMap														: register(t0);
Texture2D gAlbedoMap													: register(t1);
Texture2D gNormalDepthMap												: register(t2);
Texture2D gDepthMap														: register(t3);
Texture2D gSpecularMap													: register(t4);
Texture2D gVelocityMap													: register(t5);
Texture2D gReprojectedNormalDepthMap									: register(t6);
Texture2D gShadowMap													: register(t7);
Texture2D gDxrShadowMap0												: register(t8);
Texture2D gDxrShadowMap1												: register(t9);
Texture2D gAmbientMap0													: register(t10);
Texture2D gAmbientMap1													: register(t11);
Texture2D gRandomVector													: register(t12);
Texture2D gDxrAmbientMap0												: register(t13);
Texture2D gCachedNormalDepthMap											: register(t14);
Texture2D gDisocclusionBlurStrengthMap									: register(t15);
Texture2D gDxrTsppMap0													: register(t16);
Texture2D gDxrTsppMap1													: register(t17);
Texture2D gDxrTemporalAOCoefficientMap0									: register(t18);
Texture2D gDxrTemporalAOCoefficientMap1									: register(t19);
Texture2D gDxrCoefficientSquaredMeanMap0								: register(t20);
Texture2D gDxrCoefficientSquaredMeanMap1								: register(t21);
Texture2D gDxrRayHitDistanceMap0										: register(t22);
Texture2D gDxrRayHitDistanceMap1										: register(t23);
Texture2D gDxrLocalMeanVarianceMap0										: register(t24);
Texture2D gDxrLocalMeanVarianceMap1										: register(t25);
Texture2D gDxrVarianceMap0												: register(t26);
Texture2D gDxrVarianceMap1												: register(t27);

RWTexture2D<float> guDxrShadowMap0										: register(u0);
RWTexture2D<float> guDxrShadowMap1										: register(u1);
RWTexture2D<float> guDxrAmbientMap0										: register(u2);
RWTexture2D<float2> guLinearDepthDerivativesMap							: register(u3);
RWTexture2D<uint4> guDxrTsppCoefficientSquaredMeanRayHitDistanceMap		: register(u4);
RWTexture2D<uint> guDisocclusionBlurStrengthMap							: register(u5);
RWTexture2D<uint> guDxrTsppMap0											: register(u6);
RWTexture2D<uint> guDxrTsppMap1											: register(u7);
RWTexture2D<float> guTemporalAOCoefficientMap0							: register(u8);
RWTexture2D<float> guTemporalAOCoefficientMap1							: register(u9);
RWTexture2D<float> guDxrCoefficientSquaredMeanMap0						: register(u10);
RWTexture2D<float> guDxrCoefficientSquaredMeanMap1						: register(u11);
RWTexture2D<float> guDxrRayHitDistanceMap0								: register(u12);
RWTexture2D<float> guDxrRayHitDistanceMap1								: register(u13);
RWTexture2D<float2> guDxrLocalMeanVarianceMap0							: register(u14);
RWTexture2D<float2> guDxrLocalMeanVarianceMap1							: register(u15);
RWTexture2D<float> guDxrVarianceMap0									: register(u16);
RWTexture2D<float> guDxrVarianceMap1									: register(u17);

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

#endif // __COMMON_HLSLI__