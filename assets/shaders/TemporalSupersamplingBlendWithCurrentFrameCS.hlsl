#ifndef __TEMPORALSUPERSAMPLINGBLENDWIDTHCURRENTFRAMECS_HLSL__
#define __TEMPORALSUPERSAMPLINGBLENDWIDTHCURRENTFRAMECS_HLSL__

// 2nd stage of temporal supersampling.
// Blends current frame values with values reprojected from previous frame in stage 1.

#ifndef HLSL
#define HLSL
#endif

#include "./../../include/HlslCompaction.h"
#include "ShadingHelpers.hlsli"
#include "Samplers.hlsli"
#include "Rtao.hlsli"

ConstantBuffer<TemporalSupersamplingBlendWithCurrentFrameConstants> cb : register(b0);

Texture2D<float>	gi_CurrentFrameValue						: register(t0);
Texture2D<float2>	gi_CurrentFrameLocalMeanVariance			: register(t1);
Texture2D<float>	gi_CurrentFrameRayHitDistance				: register(t2);
Texture2D<uint4>	gi_ReprojTsppValueSquaredMeanRayHitDist		: register(t3);

RWTexture2D<float>	gio_Value				: register(u0);
RWTexture2D<uint>	gio_Tspp				: register(u1);
RWTexture2D<float>	gio_ValueSquaredMean	: register(u2);
RWTexture2D<float>	gio_RayHitDistance		: register(u3);
RWTexture2D<float>	go_Variance				: register(u4);
RWTexture2D<float>	go_BlurStrength			: register(u5);

[numthreads(DefaultComputeShaderParams::ThreadGroup::Width, DefaultComputeShaderParams::ThreadGroup::Height, 1)]
void CS(uint2 dispatchThreadID : SV_DispatchThreadID) {
	uint4 encodedCachedValues = gi_ReprojTsppValueSquaredMeanRayHitDist[dispatchThreadID];
	uint tspp = encodedCachedValues.x;
	float4 cachedValues = float4(tspp, f16tof32(encodedCachedValues.yzw));

	bool isCurrentFrameValueActive = true;
	if (cb.CheckerboardEnabled) {
		bool isEvenPixel = ((dispatchThreadID.x + dispatchThreadID.y) & 1) == 0;
		isCurrentFrameValueActive = cb.CheckerboardEvenPixelActivated == isEvenPixel;
	}

	float value = isCurrentFrameValueActive ? gi_CurrentFrameValue[dispatchThreadID] : Rtao::InvalidAOCoefficientValue;
	bool isValidValue = value != Rtao::InvalidAOCoefficientValue;
	float valueSquaredMean = isValidValue ? value * value : Rtao::InvalidAOCoefficientValue;
	float rayHitDistance = Rtao::InvalidAOCoefficientValue;
	float variance = Rtao::InvalidAOCoefficientValue;

	if (tspp > 0) {
		uint maxTspp = 1 / cb.MinSmoothingFactor;
		tspp = isValidValue ? min(tspp + 1, maxTspp) : tspp;

		float cachedValue = cachedValues.y;

		float2 localMeanVariance = gi_CurrentFrameLocalMeanVariance[dispatchThreadID];
		float localMean = localMeanVariance.x;
		float localVariance = localMeanVariance.y;
		if (cb.ClampCachedValues) {
			float localStdDev = max(cb.StdDevGamma * sqrt(localVariance), cb.ClampingMinStdDevTolerance);
			float nonClampedCachedValue = cachedValue;

			// Clamp value to mean +/- std.dev of local neighborhood to supress ghosting on value changing due to other occluder movements.
			// Ref: Salvi2016, Temporal-Super-Sampling
			cachedValue = clamp(cachedValue, localMean - localStdDev, localMean + localStdDev);

			// Scale down the tspp based on how strongly the cached value got clamped to give more weight to new smaples.
			float tsppScale = saturate(cb.ClampDifferenceToTsppScale * abs(cachedValue - nonClampedCachedValue));
			tspp = lerp(tspp, 0, tsppScale);
		}
		float invTspp = 1.0 / tspp;
		float a = cb.ForceUseMinSmoothingFactor ? cb.MinSmoothingFactor : max(invTspp, cb.MinSmoothingFactor);
		const float MaxSmoothingFactor = 1;
		a = min(a, MaxSmoothingFactor);

		// TODO: use average weighting instead of exponential for the first few samples
		//  to even out the weights for the noisy start instead of giving first samples mush more weight than the rest.
		// Ref: Koskela2019, Blockwise Multi-Order Feature Regression for Real-Time Path-Tracing Reconstruction

		// Value.
		value = isValidValue ? lerp(cachedValue, value, a) : cachedValue;

		// Value Squared Mean.
		float cachedSquaredMeanValue = cachedValues.z;
		valueSquaredMean = isValidValue ? lerp(cachedSquaredMeanValue, valueSquaredMean, a) : cachedSquaredMeanValue;

		// Variance.
		float temporalVariance = valueSquaredMean - value * value;
		temporalVariance = max(0, temporalVariance); // Ensure variance doesn't go negative due to imprecision.
		variance = tspp >= cb.MinTsppToUseTemporalVariance ? temporalVariance : localVariance;
		variance = max(0.1, variance);

		// RayHitDistance.
		rayHitDistance = isValidValue ? gi_CurrentFrameRayHitDistance[dispatchThreadID] : 0;
		float cachedRayHitDistance = cachedValues.w;
		rayHitDistance = isValidValue ? lerp(cachedRayHitDistance, rayHitDistance, a) : cachedRayHitDistance;
	}
	else if (isValidValue) {
		tspp = 1;
		value = value;

		rayHitDistance = gi_CurrentFrameRayHitDistance[dispatchThreadID];
		variance = gi_CurrentFrameLocalMeanVariance[dispatchThreadID].y;
		valueSquaredMean = valueSquaredMean;
	}

	float tsppRatio = min(tspp, cb.BlurStrengthMaxTspp) / float(cb.BlurStrengthMaxTspp);
	float blurStrength = pow(1 - tsppRatio, cb.BlurDecayStrength);

	gio_Tspp[dispatchThreadID] = tspp;
	gio_Value[dispatchThreadID] = value;
	gio_ValueSquaredMean[dispatchThreadID] = valueSquaredMean;
	gio_RayHitDistance[dispatchThreadID] = rayHitDistance;
	go_Variance[dispatchThreadID] = variance;
	go_BlurStrength[dispatchThreadID] = blurStrength;
}

#endif // __TEMPORALSUPERSAMPLINGBLENDWIDTHCURRENTFRAMECS_HLSL__