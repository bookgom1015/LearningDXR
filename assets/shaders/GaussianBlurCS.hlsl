#ifndef __COMPUTEGAUSSIANBLUR_HLSL__
#define __COMPUTEGAUSSIANBLUR_HLSL__

#ifndef HLSL
#define HLSL
#endif

#include "./../../include/HlslCompaction.h"
#include "Samplers.hlsli"

cbuffer cbBlur : register(b0) {
	float4x4	gProj;
	float4		gBlurWeights[3];
	float		gBlurRadius;
	float		gConstantPad0;
	float		gConstantPad1;
	float		gConstantPad2;
}

cbuffer cbRootConstants : register(b1) {
	uint2 gDimension;
}

Texture2D<float3>	gi_Normal	: register(t0);
Texture2D<float>	gi_Depth	: register(t1);

Texture2D<float>	gi_Input	: register(t2);
RWTexture2D<float>	go_gOutput	: register(u0);

#define CacheSize (GaussianBlurComputeShaderParams::ThreadGroup::Size + 2 * GaussianBlurComputeShaderParams::MaxBlurRadius)
groupshared float gCache[CacheSize];

#define Deadline 0.1f

[numthreads(GaussianBlurComputeShaderParams::ThreadGroup::Size, 1, 1)]
void HorzBlurCS(uint3 groupThreadID : SV_GroupThreadID, uint3 dispatchThreadID : SV_DispatchThreadID) {
	// unpack into float array.
	float blurWeights[12] = {
		gBlurWeights[0].x, gBlurWeights[0].y, gBlurWeights[0].z, gBlurWeights[0].w,
		gBlurWeights[1].x, gBlurWeights[1].y, gBlurWeights[1].z, gBlurWeights[1].w,
		gBlurWeights[2].x, gBlurWeights[2].y, gBlurWeights[2].z, gBlurWeights[2].w,
	};

	uint2 length;
	gi_Input.GetDimensions(length.x, length.y);

	// This thread group runs N threads.  To get the extra 2*BlurRadius pixels, 
	// have 2*BlurRadius threads sample an extra pixel.
	if (groupThreadID.x < gBlurRadius) {
		// Clamp out of bound samples that occur at image borders.
		int x = max(dispatchThreadID.x - gBlurRadius, 0);
		gCache[groupThreadID.x] = gi_Input[int2(x, dispatchThreadID.y)];
	}

	if (groupThreadID.x >= GaussianBlurComputeShaderParams::ThreadGroup::Size - gBlurRadius) {
		// Clamp out of bound samples that occur at image borders.
		int x = min(dispatchThreadID.x + gBlurRadius, length.x - 1);
		gCache[groupThreadID.x + 2 * gBlurRadius] = gi_Input[int2(x, dispatchThreadID.y)].r;
	}

	// Clamp out of bound samples that occur at image borders.
	gCache[groupThreadID.x + gBlurRadius] = gi_Input[min(dispatchThreadID.xy, length.xy - 1)].r;

	// Wait for all threads to finish.
	GroupMemoryBarrierWithGroupSync();

	//
	// Now blur each pixel.
	//
	float blurColor = gCache[groupThreadID.x + gBlurRadius] * blurWeights[gBlurRadius];
	float totalWeight = blurWeights[gBlurRadius];

	float2 centerTex = float2((dispatchThreadID.x + 0.5f) / (float)gDimension.x, (dispatchThreadID.y + 0.5f) / (float)gDimension.y);
	float3 centerNormal = gi_Normal.SampleLevel(gsamLinearClamp, centerTex, 0);
	float centerDepth = gi_Depth.SampleLevel(gsamLinearClamp, centerTex, 0);

	float dx = 1.0f / gDimension.x;

	for (int i = -gBlurRadius; i <= gBlurRadius; i++) {
		if (i == 0) continue;

		float2 neighborTex = float2(centerTex.x + dx * i, centerTex.y);
		float3 neighborNormal = gi_Normal.SampleLevel(gsamLinearClamp, neighborTex, 0);
		float neighborDepth = gi_Depth.SampleLevel(gsamLinearClamp, neighborTex, 0);

		if (dot(neighborNormal, centerNormal) >= 0.95f && abs(neighborDepth - centerDepth) <= 0.01f) {
			int k = groupThreadID.x + gBlurRadius + i;

			blurColor += gCache[k] * blurWeights[gBlurRadius + i];
			totalWeight += blurWeights[gBlurRadius + i];
		}
	}

	go_gOutput[dispatchThreadID.xy] = blurColor / totalWeight;
}

[numthreads(1, GaussianBlurComputeShaderParams::ThreadGroup::Size, 1)]
void VertBlurCS(uint3 groupThreadID : SV_GroupThreadID, uint3 dispatchThreadID : SV_DispatchThreadID) {
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

	uint2 length;
	gi_Input.GetDimensions(length.x, length.y);

	// This thread group runs N threads.  To get the extra 2*BlurRadius pixels, 
	// have 2*BlurRadius threads sample an extra pixel.
	if (groupThreadID.y < gBlurRadius) {
		// Clamp out of bound samples that occur at image borders.
		int y = max(dispatchThreadID.y - gBlurRadius, 0);
		gCache[groupThreadID.y] = gi_Input[int2(dispatchThreadID.x, y)].r;
	}

	if (groupThreadID.y >= GaussianBlurComputeShaderParams::ThreadGroup::Size - gBlurRadius) {
		// Clamp out of bound samples that occur at image borders.
		int y = min(dispatchThreadID.y + gBlurRadius, length.y - 1);
		gCache[groupThreadID.y + 2 * gBlurRadius] = gi_Input[int2(dispatchThreadID.x, y)].r;
	}

	// Clamp out of bound samples that occur at image borders.
	gCache[groupThreadID.y + gBlurRadius] = gi_Input[min(dispatchThreadID.xy, length.xy - 1)].r;

	// Wait for all threads to finish.
	GroupMemoryBarrierWithGroupSync();

	//
	// Now blur each pixel.
	//
	float blurColor = gCache[groupThreadID.y + gBlurRadius] * blurWeights[gBlurRadius];
	float totalWeight = blurWeights[gBlurRadius];

	float2 centerTex = float2((dispatchThreadID.x + 0.5f) / (float)gDimension.x, (dispatchThreadID.y + 0.5f) / (float)gDimension.y);
	float3 centerNormal = gi_Normal.SampleLevel(gsamLinearClamp, centerTex, 0);
	float centerDepth = gi_Depth.SampleLevel(gsamLinearClamp, centerTex, 0);

	float dy = 1.0f / gDimension.y;

	for (int i = -gBlurRadius; i <= gBlurRadius; i++) {
		if (i == 0) continue;

		float2 neighborTex = float2(centerTex.x, centerTex.y + dy * i);
		float3 neighborNormal = gi_Normal.SampleLevel(gsamLinearClamp, neighborTex, 0);
		float neighborDepth = gi_Depth.SampleLevel(gsamLinearClamp, neighborTex, 0);

		if (dot(neighborNormal, centerNormal) >= 0.95f && abs(neighborDepth - centerDepth) <= 0.01f) {
			int k = groupThreadID.y + gBlurRadius + i;

			blurColor += gCache[k] * blurWeights[gBlurRadius + i];
			totalWeight += blurWeights[gBlurRadius + i];
		}
	}

	go_gOutput[dispatchThreadID.xy] = blurColor / totalWeight;
}

#endif // __COMPUTEGAUSSIANBLUR_HLSL__