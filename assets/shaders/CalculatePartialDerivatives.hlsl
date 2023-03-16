#ifndef __CALCULATEPARTIALDERIVATIVES_HLSL__
#define __CALCULATEPARTIALDERIVATIVES_HLSL__

#ifndef HLSL
#define HLSL
#endif

#include "./../../include/HlslCompaction.h"
#include "ShadingHelpers.hlsli"
#include "Samplers.hlsli"

cbuffer cbRootConstants : register (b0) {
	uint2 gDimension;
}

Texture2D<float> giDepthMap						: register(t0);
RWTexture2D<float2> goLinearDepthDerivativesMap	: register(u0);

[numthreads(DefaultComputeShaderParams::ThreadGroup::Width, DefaultComputeShaderParams::ThreadGroup::Height, 1)]
void CS(uint2 dispatchThreadID : SV_DispatchThreadID) {
	float dx = 1.0f / gDimension.x;
	float dy = 1.0f / gDimension.y;

	float2 tex = float2((dispatchThreadID.x + 0.5f) / gDimension.x, (dispatchThreadID.y + 0.5f) / gDimension.y);

	float top	 = giDepthMap.SampleLevel(gsamPointClamp, tex + float2( 0, -dy), 0);
	float bottom = giDepthMap.SampleLevel(gsamPointClamp, tex + float2( 0,  dy), 0);
	float left	 = giDepthMap.SampleLevel(gsamPointClamp, tex + float2(-dx, 0 ), 0);
	float right	 = giDepthMap.SampleLevel(gsamPointClamp, tex + float2( dx, 0 ), 0);

	float center = giDepthMap.SampleLevel(gsamPointClamp, tex, 0);
	float2 backwardDiff = center - float2(left, top);
	float2 forwardDiff = float2(right, bottom) - center;

	// Calculates partial derivatives as the min of absolute backward and forward differences.

	// Find the absolute minimum of the backward and foward differences in each axis
    // while preserving the sign of the difference.
	float2 ddx = float2(backwardDiff.x, forwardDiff.x);
	float2 ddy = float2(backwardDiff.y, forwardDiff.y);

	uint2 minIndex = {
		GetIndexOfValueClosest(0, ddx),
		GetIndexOfValueClosest(0, ddy)
	};
	float2 ddxy = float2(ddx[minIndex.x], ddy[minIndex.y]);

	// Clamp ddxy to a reasonable value to avoid ddxy going over surface boundaries
	// on thin geometry and getting background/foreground blended together on blur.
	float maxDdxy = 1;
	float2 _sign = sign(ddxy);
	ddxy = _sign * min(abs(ddxy), maxDdxy);

	goLinearDepthDerivativesMap[dispatchThreadID] = ddxy;
}

#endif // __CALCULATEPARTIALDERIVATIVES_HLSL__