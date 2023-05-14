#ifndef __TEMPORALSUPERSAMPLINGREVERSEREPROJECTCS_HLSL__
#define __TEMPORALSUPERSAMPLINGREVERSEREPROJECTCS_HLSL__

// Stage 1 of Temporal-Supersampling.
// Samples temporal cache via vectors/reserve reprojection.
// If no valid values have been reterived from the cache, the tspp is set to 0.

#ifndef HLSL
#define HLSL
#endif

#include "./../../include/HlslCompaction.h"
#include "ShadingHelpers.hlsli"
#include "Samplers.hlsli"
#include "CrossBilateralWeights.hlsli"
#include "Rtao.hlsli"

ConstantBuffer<CrossBilateralFilterConstants> cb : register (b0);

cbuffer cbRootConstants : register (b1) {
	uint2	gTextureDim;
	float2	gInvTextureDim;
};

Texture2D<float4>	gi_CurrentFrameNormalDepth	: register(t0);
Texture2D<float2>	gi_DepthPartialDerivative	: register(t1);
Texture2D<float4>	gi_ReprojectedNormalDepth	: register(t2);
Texture2D<float4>	gi_CachedNormalDepth		: register(t3);
Texture2D<float2>	gi_Velocity					: register(t4);
Texture2D<float>	gi_CachedValue				: register(t5);
Texture2D<uint>		gi_CachedTspp				: register(t6);
Texture2D<float>	gi_CachedValueSquaredMean	: register(t7);
Texture2D<float>	gi_CachedRayHitDistance		: register(t8);

RWTexture2D<uint>	go_CachedTspp				: register(u0);
RWTexture2D<uint4>	go_ReprojectedCachedValues	: register(u1);

float4 BilateralResampleWeights(
		float	targetDepth, 
		float3	targetNormal, 
		float4	sampleDepths, 
		float3	sampleNormals[4], 
		float2	targetOffset, 
		uint2	targetIndex, 
		int2	cacheIndices[4],
		float2	ddxy) {
	bool4 isWithinBounds = bool4(
		IsWithinBounds(cacheIndices[0], gTextureDim),
		IsWithinBounds(cacheIndices[1], gTextureDim),
		IsWithinBounds(cacheIndices[2], gTextureDim),
		IsWithinBounds(cacheIndices[3], gTextureDim)
	);

	CrossBilateral::BilinearDepthNormal::Parameters params;
	params.Depth.Sigma = cb.DepthSigma;
	params.Depth.WeightCutoff = 0.5;
	params.Depth.NumMantissaBits = cb.DepthNumMantissaBits;
	params.Normal.Sigma = 1.1; // Bump the sigma a bit to add tolerance for slight geometry misalignments and/or format precision limitations.
	params.Normal.SigmaExponent = 32;

	float4 bilinearDepthNormalWeights;

	bilinearDepthNormalWeights = CrossBilateral::BilinearDepthNormal::GetWeights(
		targetDepth, 
		targetNormal, 
		targetOffset, 
		ddxy, 
		sampleDepths, 
		sampleNormals, 
		params
	);

	float4 weights = isWithinBounds * bilinearDepthNormalWeights;

	return weights;
}

[numthreads(DefaultComputeShaderParams::ThreadGroup::Width, DefaultComputeShaderParams::ThreadGroup::Height, 1)]
void CS(uint2 dispatchThreadID : SV_DispatchThreadID, uint2 groupThreadID : SV_GroupThreadID, uint2 groupID : SV_GroupID) {
	if (dispatchThreadID.x >= gTextureDim.x || dispatchThreadID.y >= gTextureDim.y) return;

	float3 reprojNormal;
	float reprojDepth;
	{
		float4 nd = gi_ReprojectedNormalDepth[dispatchThreadID];
		reprojNormal = nd.xyz;
		reprojDepth = nd.w;
	}

	float2 velocity = gi_Velocity[dispatchThreadID];

	if (reprojDepth == 1 || velocity.x > 100) {
		go_CachedTspp[dispatchThreadID] = 0;
		return;
	}

	float2 tex = (dispatchThreadID + 0.5) * gInvTextureDim;
	float2 cacheTex = tex - velocity;
	
	// Find the nearest integer index samller than the texture position.
	// The floor() ensures the that value sign is taken into consideration.
	int2 topLeftCacheIndex = floor(cacheTex * gTextureDim - 0.5);
	float2 adjustedCacheTex = (topLeftCacheIndex + 0.5) * gInvTextureDim;

	float2 cachePixelOffset = cacheTex * gTextureDim - 0.5 - topLeftCacheIndex;

	const int2 srcIndexOffsets[4] = { {0,0}, {1,0}, {0,1}, {1,1} };

	int2 cacheIndices[4] = {
		topLeftCacheIndex + srcIndexOffsets[0],
		topLeftCacheIndex + srcIndexOffsets[1],
		topLeftCacheIndex + srcIndexOffsets[2],
		topLeftCacheIndex + srcIndexOffsets[3] 
	};

	float3 cacheNormals[4];
	float4 cacheDepths = 0;
	for (int i = 0; i < 4; ++i) {
		float4 nd = gi_CachedNormalDepth.SampleLevel(gsamPointClamp, adjustedCacheTex + srcIndexOffsets[i] * gInvTextureDim, 0);
		cacheNormals[i] = nd.xyz;
		cacheDepths[i] = nd.w;
	}

	float2 ddxy = gi_DepthPartialDerivative[dispatchThreadID];

	float4 weights = BilateralResampleWeights(reprojDepth, reprojNormal, cacheDepths, cacheNormals, cachePixelOffset, dispatchThreadID, cacheIndices, ddxy);

	// Invalidate weights for invalid values in the cache.
	float4 vCacheValues = gi_CachedValue.GatherRed(gsamPointClamp, adjustedCacheTex).wzxy;
	weights = vCacheValues != Rtao::InvalidAOCoefficientValue ? weights : 0;
	float weightSum = dot(1, weights);

	float cachedValue = Rtao::InvalidAOCoefficientValue;
	float cachedValueSquaredMean = 0;
	float cachedRayHitDist = 0;

	uint tspp;
	if (weightSum > 0.001f) {
		uint4 vCachedTspp = gi_CachedTspp.GatherRed(gsamPointClamp, adjustedCacheTex).wzxy;
		// Enforce tspp of at least 1 for reprojection for valid values.
		// This is because the denoiser will fill in invalid values with filtered 
		// ones if it can. But it doesn't increase tspp.
		vCachedTspp = max(1, vCachedTspp);

		float4 nWeights = weights / weightSum; // Normalize the weights.

		// Scale the tspp by the total weight. This is to keep the tspp low for 
		// total contributions that have very low reprojection weight. While its preferred to get 
		// a weighted value even for reprojections that have low weights but still
		// satisfy consistency tests, the tspp needs to be kept small so that the Target calculated values
		// are quickly filled in over a few frames. Otherwise, bad estimates from reprojections,
		// such as on disocclussions of surfaces on rotation, are kept around long enough to create 
		// visible streaks that fade away very slow.
		// Example: rotating camera around dragon's nose up close. 
		const float tsppScale = 1;

		float cachedTspp = tsppScale * dot(nWeights, vCachedTspp);
		tspp = round(cachedTspp);

		if (tspp > 0) {
			float4 vCacheValues = gi_CachedValue.GatherRed(gsamPointClamp, adjustedCacheTex).wzxy;
			cachedValue = dot(nWeights, vCacheValues);

			float4 vCachedValueSquaredMean = gi_CachedValueSquaredMean.GatherRed(gsamPointClamp, adjustedCacheTex).wzxy;
			cachedValueSquaredMean = dot(nWeights, vCachedValueSquaredMean);

			float4 vCachedRayHitDist = gi_CachedRayHitDistance.GatherRed(gsamPointClamp, adjustedCacheTex).wzxy;
			cachedRayHitDist = dot(nWeights, vCachedRayHitDist);
		}
	}
	else {
		// No valid values can be retrieved from the cache.
		// TODO: try a greater cache footprint to find useful samples,
		//   For example a 3x3 pixel cache footprint or use lower mip cache input.
		tspp = 0;
	}

	go_CachedTspp[dispatchThreadID] = tspp;
	go_ReprojectedCachedValues[dispatchThreadID] = uint4(tspp, f32tof16(float3(cachedValue, cachedValueSquaredMean, cachedRayHitDist)));
}

#endif // __TEMPORALSUPERSAMPLINGREVERSEREPROJECTCS_HLSL__