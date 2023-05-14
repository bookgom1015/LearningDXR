#ifndef __GAUSSIANFILTERRG3X3CS_HLSL__
#define __GAUSSIANFILTERRG3X3CS_HLSL__

#ifndef HLSL
#define HLSL
#endif

#include "./../../include/HlslCompaction.h"
#include "ShadingHelpers.hlsli"
#include "Samplers.hlsli"

cbuffer cbRootConstants : register(b0) {
	uint2 gTextureDim;
	float2 gInvTextureDim;
}

Texture2D<float2> gInputMap		: register(t0);
RWTexture2D<float2> gOutputMap	: register(u0);

static const float weights[3][3] = {
	{ 0.077847, 0.123317, 0.077847 },
	{ 0.123317, 0.195346, 0.123317 },
	{ 0.077847, 0.123317, 0.077847 },
};

#define APPROXIMATE_GAUSSIAN_3X3_VIA_HW_FILTERING 1

#if APPROXIMATE_GAUSSIAN_3X3_VIA_HW_FILTERING
// Approximate 3x3 gaussian filter using HW bilinear filtering.
// Ref: Moller2018, Real-Time Rendering (Fourth Edition), p517
// Performance improvement over 3x3 2D version (4K on 2080 Ti): 0.18ms -> 0.11ms
[numthreads(DefaultComputeShaderParams::ThreadGroup::Width, DefaultComputeShaderParams::ThreadGroup::Height, 1)]
void CS(uint2 dispatchThreadID : SV_DispatchThreadID) {
	// Set weights based on availability of neightbor samples.
	float4 weights;

	uint2 border = uint2(gTextureDim.x - 1, gTextureDim.y - 1);

	// Non-border pixels
	if (dispatchThreadID.x > 0 && dispatchThreadID.y > 0 && dispatchThreadID.x < border.x && dispatchThreadID.x < border.y) {
		weights = float4(0.077847f + 0.123317f + 0.123317f + 0.195346f,
			0.077847f + 0.123317f,
			0.077847f + 0.123317f,
			0.077847f);
	}
	// Top-left corner
	else if (dispatchThreadID.x == 0 && dispatchThreadID.y == 0) {
		weights = float4(0.195346, 0.123317, 0.123317, 0.077847) / 0.519827;
	}
	// Top-right corner
	else if (dispatchThreadID.x == border.x && dispatchThreadID.y == 0) {
		weights = float4(0.123317 + 0.195346, 0, 0.201164, 0) / 0.519827;
	}
	// Bootom-left corner
	else if (dispatchThreadID.x == 0 && dispatchThreadID.y == border.y) {
		weights = float4(0.123317 + 0.195346, 0.077847 + 0.123317, 0, 0) / 0.519827;
	}
	// Bottom-right corner
	else if (dispatchThreadID.x == border.x && dispatchThreadID.y == border.y) {
		weights = float4(0.077847 + 0.123317 + 0.123317 + 0.195346, 0, 0, 0) / 0.519827;
	}
	// Left border
	else if (dispatchThreadID.x == 0) {
		weights = float4(0.123317 + 0.195346, 0.077847 + 0.123317, 0.123317, 0.077847) / 0.720991;
	}
	// Right border
	else if (dispatchThreadID.x == border.x) {
		weights = float4(0.077847 + 0.123317 + 0.123317 + 0.195346, 0, 0.077847 + 0.123317, 0) / 0.720991;
	}
	// Top border
	else if (dispatchThreadID.y == 0) {
		weights = float4(0.123317 + 0.195346, 0.123317, 0.077847 + 0.123317, 0.077847) / 0.720991;
	}
	// Bottom border
	else {
		weights = float4(0.077847 + 0.123317 + 0.123317 + 0.195346, 0.077847 + 0.123317, 0, 0) / 0.720991;
	}

	const float2 offsets[3] = {
		float2(0.5, 0.5) + float2(-0.123317 / (0.123317 + 0.195346), -0.123317 / (0.123317 + 0.195346)),
		float2(0.5, 0.5) + float2(1, -0.077847 / (0.077847 + 0.123317)),
		float2(0.5, 0.5) + float2(-0.077847 / (0.077847 + 0.123317), 1)
	};

	float2 samples[4];
	samples[0] = gInputMap.SampleLevel(gsamLinearMirror, (dispatchThreadID + offsets[0]) * gInvTextureDim, 0);
	samples[1] = gInputMap.SampleLevel(gsamLinearMirror, (dispatchThreadID + offsets[1]) * gInvTextureDim, 0);
	samples[2] = gInputMap.SampleLevel(gsamLinearMirror, (dispatchThreadID + offsets[2]) * gInvTextureDim, 0);
	samples[3] = gInputMap[dispatchThreadID + 1];

	float4 samplesR = float4(samples[0].x, samples[1].x, samples[2].x, samples[3].x);
	float4 samplesG = float4(samples[0].y, samples[1].y, samples[2].y, samples[3].y);

	gOutputMap[dispatchThreadID] = float2(dot(samplesR, weights), dot(samplesG, weights));
}

#else 

void AddFilterContribution(inout float weightedValueSum, inout float weightSum, int row, int col, int2 dispatchThreadID) {
	int2 id = dispatchThreadID + int2(row - 1, col - 1);
	if (id.x >= 0 && id.y >= 0 && id.x < gTextureDim.x && id.y < gTextureDim.y) {
		float weight = weights[col][row];
		weightedValueSum += weight * gInputMap[id];
		weightSum += weight;
	}
}

[numthreads(DefaultComputeShaderParams::ThreadGroup::Width, DefaultComputeShaderParams::ThreadGroup::Height, 1)]
void CS(uint2 dispatchThreadID : SV_DispatchThreadID) {
	float weightSum = 0;
	float weightedValueSum = 0;

	[unroll]
	for (uint r = 0; r < 3; ++r) {
		[unroll]
		for (uint c = 0; c < 3; ++c) {
			AddFilterContribution(weightedValueSum, weightSum, r, c, dispatchThreadID);
		}
	}

	gOutputMap[dispatchThreadID] = weightedValueSum / weightSum;
}
#endif // APPROXIMATE_GAUSSIAN_3X3_VIA_HW_FILTERING

#endif // __GAUSSIANFILTERRG3X3CS_HLSL__