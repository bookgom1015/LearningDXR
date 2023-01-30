#ifndef __RAYGEN_HLSL__
#define __RAYGEN_HLSL__

#include "DxrCommon.hlsli"

[shader("raygeneration")]
void RayGen() {
	float3 rayDir;
	float3 origin;

	// Generate a ray for a camera pixel corresponding to an index from the dispatched 2D grid.
	GenerateCameraRay(DispatchRaysIndex().xy, origin, rayDir);

	// Trace the ray.
	// Set the ray's extents.
	RayDesc ray;
	ray.Origin = origin;
	ray.Direction = rayDir;
	// Set TMin to a non-zero small value to avoid aliasing issues due to floating - point errors.
	// TMin should be kept small to prevent missing geometry at close contact areas.
	ray.TMin = 0.1f;
	ray.TMax = 1000.0f;
	RayPayload payload = { (float4)0.0f };
	TraceRay(gBVH, RAY_FLAG_CULL_FRONT_FACING_TRIANGLES, ~0, 0, 1, 0, ray, payload);

	// Write the raytraced color to the output texture.
	gOutput[DispatchRaysIndex().xy] = payload.Color;
}

#endif // __RAYGEN_HLSL__