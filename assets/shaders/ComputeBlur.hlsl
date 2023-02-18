#ifndef __COMPUTEBLUR_HLSL__
#define __COMPUTEBLUR_HLSL__

#include "Samplers.hlsli"

cbuffer cbBlur : register(b0) {
	float4x4	gProj;
	float4		gBlurWeights[3];
	float		gBlurRadius;
	float		gConstantPad0;
	float		gConstantPad1;
	float		gConstantPad2;
}

Texture2D gNormalMap				: register(t0);
Texture2D gDepthMap					: register(t1);

RWTexture2D<float> gInputMap		: register(u0);
RWTexture2D<float> gOutputMap		: register(u1);

static const int gMaxBlurRadius = 5;

#define N 256
#define CacheSize (N + 2 * gMaxBlurRadius)
groupshared float gCache[CacheSize];

#define Deadline 0.1f

[numthreads(N, 1, 1)]
void HorzBlurCS(int3 groupThreadID : SV_GroupThreadID, int3 dispatchThreadID : SV_DispatchThreadID) {
	// unpack into float array.
	float blurWeights[12] = {
		gBlurWeights[0].x, gBlurWeights[0].y, gBlurWeights[0].z, gBlurWeights[0].w,
		gBlurWeights[1].x, gBlurWeights[1].y, gBlurWeights[1].z, gBlurWeights[1].w,
		gBlurWeights[2].x, gBlurWeights[2].y, gBlurWeights[2].z, gBlurWeights[2].w,
	};

	// This thread group runs N threads.  To get the extra 2*BlurRadius pixels, 
	// have 2*BlurRadius threads sample an extra pixel.
	if (groupThreadID.x < gBlurRadius) {
		// Clamp out of bound samples that occur at image borders.
		int x = max(dispatchThreadID.x - gBlurRadius, 0);
		gCache[groupThreadID.x] = gInputMap[int2(x, dispatchThreadID.y)].r;
	}

	if (groupThreadID.x > N - gBlurRadius) {
		// Clamp out of bound samples that occur at image borders.
		int x = min(dispatchThreadID.x + gBlurRadius, gInputMap.Length.x - 1);
		gCache[groupThreadID.x + 2 * gBlurRadius] = gInputMap[int2(x, dispatchThreadID.y)].r;
	}

	// Clamp out of bound samples that occur at image borders.
	gCache[groupThreadID.x + gBlurRadius] = gInputMap[min(dispatchThreadID.xy, gInputMap.Length.xy - 1)].r;

	// Wait for all threads to finish.
	GroupMemoryBarrierWithGroupSync();

	//
	// Now blur each pixel.
	//
	float blurColor = 0.0f;
	float totalWeight = 0.0f;

	for (int i = -gBlurRadius; i <= gBlurRadius; i++) {
		int k = groupThreadID.x + gBlurRadius + i;

		blurColor += gCache[k] * blurWeights[gBlurRadius + i];
		totalWeight += blurWeights[gBlurRadius + i];
	}

	gOutputMap[dispatchThreadID.xy] = blurColor / totalWeight;
}

[numthreads(1, N, 1)]
void VertBlurCS(int3 groupThreadID : SV_GroupThreadID, int3 dispatchThreadID : SV_DispatchThreadID) {
	// unpack into float array.
	float blurWeights[12] = {
		gBlurWeights[0].x, gBlurWeights[0].y, gBlurWeights[0].z, gBlurWeights[0].w,
		gBlurWeights[1].x, gBlurWeights[1].y, gBlurWeights[1].z, gBlurWeights[1].w,
		gBlurWeights[2].x, gBlurWeights[2].y, gBlurWeights[2].z, gBlurWeights[2].w,
	};

	//
	// Fill local thread storage to reduce bandwidth.  To blur 
	// N pixels, we will need to load N + 2*BlurRadius pixels
	// due to the blur radius.
	//

	// This thread group runs N threads.  To get the extra 2*BlurRadius pixels, 
	// have 2*BlurRadius threads sample an extra pixel.
	if (groupThreadID.y < gBlurRadius) {
		// Clamp out of bound samples that occur at image borders.
		int y = max(dispatchThreadID.y - gBlurRadius, 0);
		gCache[groupThreadID.y] = gInputMap[int2(dispatchThreadID.x, y)].r;
	}

	if (groupThreadID.y >= N - gBlurRadius) {
		// Clamp out of bound samples that occur at image borders.
		int y = min(dispatchThreadID.y + gBlurRadius, gInputMap.Length.y - 1);
		gCache[groupThreadID.y + 2 * gBlurRadius] = gInputMap[int2(dispatchThreadID.x, y)].r;
	}

	// Clamp out of bound samples that occur at image borders.
	gCache[groupThreadID.y + gBlurRadius] = gInputMap[min(dispatchThreadID.xy, gInputMap.Length.xy - 1)].r;

	// Wait for all threads to finish.
	GroupMemoryBarrierWithGroupSync();

	//
	// Now blur each pixel.
	//
	float4 blurColor = float4(0, 0, 0, 0);
	float totalWeight = 0.0f;

	for (int i = -gBlurRadius; i <= gBlurRadius; i++) {
		int k = groupThreadID.y + gBlurRadius + i;

		blurColor += gCache[k] * blurWeights[gBlurRadius + i];
		totalWeight += blurWeights[gBlurRadius + i];
	}

	gOutputMap[dispatchThreadID.xy] = blurColor / totalWeight;
}

#endif // __COMPUTEBLUR_HLSL__