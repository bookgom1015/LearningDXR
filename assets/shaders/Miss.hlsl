#ifndef __MISS_HLSL__
#define __MISS_HLSL__

#include "DxrCommon.hlsli"

[shader("miss")]
void Miss(inout RayPayload payload) {
	uint2 launchIndex = DispatchRaysIndex().xy;
	float2 dims = DispatchRaysDimensions().xy;
	float ramp = (launchIndex.y / dims.y) * 0.4f;
	float4 background = float4(0.941176534f - ramp, 0.972549081f - ramp, 1.0f - ramp, 1.0f);
	payload.Color = background;
}

#endif // __MISS_HLSL__