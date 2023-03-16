#ifndef __TEMPORALSUPERSAMPLINGREVERSEREPROJECT_HLSL__
#define __TEMPORALSUPERSAMPLINGREVERSEREPROJECT_HLSL__

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

ConstantBuffer<CrossBilateralFilterConstants> cbFilter : register (b0);

cbuffer cbRootConstants : register (b1) {
	uint2 gDimension;
};

Texture2D<float4> gNormalDepthMap										: register(t0);
Texture2D<float2> gVelocityMap											: register(t1);
Texture2D<float4> gReprojectedNormalDepthMap							: register(t2);
Texture2D<float4> gCachedNormalDepthMap									: register(t3);
Texture2D<float> gCachedTemporalAOCoefficientMap						: register(t4);
Texture2D<uint> gCachedTemporalSupersamplingMap							: register(t5);
Texture2D<float> gCachedCoefficientSquaredMeanMap						: register(t6);
Texture2D<float> gCachedRayHitDistanceMap								: register(t7);

RWTexture2D<uint> goTemporalSupersamplingMap							: register(u0);
RWTexture2D<float2> giLinearDepthDerivativesMap							: register(u1);
RWTexture2D<uint4> goTsppCoefficientSquaredMeanRayHitDistanceMap		: register(u2);

float4 BilateralResampleWeights(
		float	targetDepth, 
		float3	targetNormal, 
		float4	sampleDepths, 
		float3	sampleNormals[4], 
		float2	targetOffset, 
		uint2	targetIndex, 
		int2	sampleIndices[4], 
		float2	ddxy) {
	bool4 isWithinBounds = bool4(
		IsWithinBounds(sampleIndices[0], gDimension),
		IsWithinBounds(sampleIndices[1], gDimension),
		IsWithinBounds(sampleIndices[2], gDimension),
		IsWithinBounds(sampleIndices[3], gDimension)
	);

	CrossBilateral::BilinearDepthNormal::Parameters params;
	params.Depth.Sigma = cbFilter.DepthSigma;
	params.Depth.WeightCutoff = 0.5f;
	params.Depth.NumMantissaBits = cbFilter.DepthNumMantissaBits;
	params.Normal.Sigma = 1.1f; // Bump the sigma a bit to add tolerance for slight geometry misalignments and/or format precision limitations.
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
	if (dispatchThreadID.x >= gDimension.x || dispatchThreadID.y >= gDimension.y) return;

	float2 tex = float2((dispatchThreadID.x + 0.5f) / gDimension.x, (dispatchThreadID.y + 0.5f) / gDimension.y);

	float3 reprojNormal;
	float reprojDepth;
	{
		float4 nd = gReprojectedNormalDepthMap.SampleLevel(gsamPointClamp, tex, 0);
		reprojNormal = nd.xyz;
		reprojDepth = nd.w;
	}

	if (reprojDepth == 1) {
		goTemporalSupersamplingMap[dispatchThreadID] = 0;
		return;
	}

	float2 velocity = gVelocityMap.SampleLevel(gsamPointClamp, tex, 0);
	float2 prevTex = tex - velocity;
	
	int2 topLeftIndex = floor(prevTex * gDimension - 0.5f);
	float2 adjustedPrevTex = prevTex * gDimension - 0.5f - topLeftIndex;

	const int2 offsets[4] = { {0,0}, {1,0}, {0,1}, {1,1} };

	int2 sampleIndices[4] = {
		topLeftIndex + offsets[0],
		topLeftIndex + offsets[1],
		topLeftIndex + offsets[2],
		topLeftIndex + offsets[3] 
	};

	float3 nearNormals[4];
	float4 nearDepths;
	{
		for (int i = 0; i < 4; ++i) {
			float4 nd = gCachedNormalDepthMap.SampleLevel(gsamPointClamp, prevTex, 0, offsets[i]);
			nearNormals[i] = nd.xyz;
			nearDepths[i] = nd.w;
		}
	}

	float2 ddxy = giLinearDepthDerivativesMap[dispatchThreadID];
	float4 weights = BilateralResampleWeights(reprojDepth, reprojNormal, nearDepths, nearNormals, adjustedPrevTex, dispatchThreadID, sampleIndices, ddxy);

	// Invalidate weights for invalid values in the cache.
	float4 cachedCoefficients = gCachedTemporalAOCoefficientMap.GatherRed(gsamPointClamp, adjustedPrevTex).wzxy;
	weights = cachedCoefficients != Rtao::InvalidAOCoefficientValue ? weights : 0;
	float weightSum = weights.x + weights.y + weights.z + weights.w;

	float cachedCoefficient = Rtao::InvalidAOCoefficientValue;
	float cachedCoefficientSquaredMean = 0;
	float cachedRayHitDist = 0;

	uint tspp;
	if (weightSum > 0.001f) {
		uint4 vCachedTspp = gCachedTemporalSupersamplingMap.GatherRed(gsamPointClamp, adjustedPrevTex).wzxy;
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
		float tsppScale = 1;

		float cachedTspp = tsppScale * dot(nWeights, vCachedTspp);
		tspp = round(cachedTspp);

		if (tspp > 0) {
			cachedCoefficient = dot(nWeights, cachedCoefficients);

			float4 cachedSquaredMean = gCachedCoefficientSquaredMeanMap.GatherRed(gsamPointClamp, adjustedPrevTex).wzxy;
			cachedCoefficientSquaredMean = dot(nWeights, cachedSquaredMean);

			float4 vCachedRayHitDist = gCachedRayHitDistanceMap.GatherRed(gsamPointClamp, adjustedPrevTex).wzxy;
			cachedRayHitDist = dot(nWeights, vCachedRayHitDist);
		}
	}
	else {
		// No valid values can be retrieved from the cache.
		// TODO: try a greater cache footprint to find useful samples,
		//   For example a 3x3 pixel cache footprint or use lower mip cache input.
		tspp = 0;
	}

	goTemporalSupersamplingMap[dispatchThreadID] = tspp;
	goTsppCoefficientSquaredMeanRayHitDistanceMap[dispatchThreadID] = uint4(tspp, f32tof16(float3(cachedCoefficient, cachedCoefficientSquaredMean, cachedRayHitDist)));
}

#endif // __TEMPORALSUPERSAMPLINGREVERSEREPROJECT_HLSL__