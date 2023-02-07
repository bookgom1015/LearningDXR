#ifndef __RAYGEN_HLSL__
#define __RAYGEN_HLSL__

#include "DxrCommon.hlsli"

[shader("raygeneration")]
void RayGen() {
	float3 rayDir;
	float3 origin;

	// Generate a ray for a camera pixel corresponding to an index from the dispatched 2D grid.
	uint2 launchIndex = DispatchRaysIndex().xy;
	GenerateCameraRay(launchIndex.xy, origin, rayDir);

	// Trace the ray.
	// Set the ray's extents.
	RayDesc ray;
	ray.Origin = origin;
	ray.Direction = rayDir;
	// Set TMin to a non-zero small value to avoid aliasing issues due to floating - point errors.
	// TMin should be kept small to prevent missing geometry at close contact areas.
	ray.TMin = 0.1f;
	ray.TMax = 1000.0f;
	HitInfo payload = { (float4)0.0f };
	TraceRay(
		// Parameter name: AccelerationStructure
		// Acceleration structure
		gBVH, 
		// Parameter name: RayFlags
		// Flags can be used to specify the behavior upon hitting a surface
		RAY_FLAG_CULL_FRONT_FACING_TRIANGLES, 
		// Parameter name: InstanceInclusionMask
		// Instance inclusion mask, which can be used to mask out some geometry to
		// this ray by and-ing the mask with a geometry mask. The 0xFF flag then
		// indicates no geometry will be masked
		0xFF, 
		// Parameter name: RayContributionToHitGroupIndex
		// Depending on the type of ray, a given object can have several hit
		// groups attached (ie. what to do when hitting to compute regular
		// shading, and what to do when hitting to compute shadows). Those hit
		// groups are specified sequentially in the SBT, so the value below
		// indicates which offset (on 4 bits) to apply to the hit groups for this
		// ray. In this sample we only have one hit group per object, hence an
		// offset of 0.
		0, 
		// Parameter name: MultiplierForGeometryContributionToHitGroupIndex
		// The offsets in the SBT can be computed from the object ID, its instance
		// ID, but also simply by the order the objects have been pushed in the
		// acceleration structure. This allows the application to group shaders in
		// the SBT in the same order as they are added in the AS, in which case
		// the value below represents the stride (4 bits representing the number
		// of hit groups) between two consecutive objects.
		0, 
		// Parameter name: MissShaderIndex
		// Index of the miss shader to use in case several consecutive miss
		// shaders are present in the SBT. This allows to change the behavior of
		// the program when no geometry have been hit, for example one to return a
		// sky color for regular rendering, and another returning a full
		// visibility value for shadow rays. This sample has only one miss shader,
		// hence an index 0
		0, 
		// Parameter name: Ray
		// Ray information to trace
		ray, 
		// Parameter name: Payload
		// Payload associated to the ray, which will be used to communicate
		// between the hit/miss shaders and the raygen
		payload
	);

	// Write the raytraced color to the output texture.
	gOutput[launchIndex.xy] = payload.Color;	
}

#endif // __RAYGEN_HLSL__