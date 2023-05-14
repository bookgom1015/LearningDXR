#ifndef __RTAO_HLSL__
#define __RTAO_HLSL__

#ifndef HLSL
#define HLSL
#endif

#include "./../../include/HlslCompaction.h"
#include "ShadingHelpers.hlsli"
#include "RandGenerator.hlsli"
#include "Samplers.hlsli"
#include "Rtao.hlsli"

typedef BuiltInTriangleIntersectionAttributes Attributes;

struct AORayPayload {
	float	tHit;
};

ConstantBuffer<RtaoConstants> cb : register(b0);

cbuffer cbRootConstants : register(b1) {
	uint2 gTextureDim;
};

// Nonnumeric values cannot be added to a cbuffer.
RaytracingAccelerationStructure	gBVH	: register(t0);
Texture2D<float3> gi_Normal				: register(t1);
Texture2D<float> gi_DepthMap			: register(t2);

RWTexture2D<float> go_AOCoefficient		: register(u0);
RWTexture2D<float> go_RayHitDistance	: register(u1);

// Generates a seed for a random number generator from 2 inputs plus a backoff
uint InitRand(uint val0, uint val1, uint backoff = 16) {
	uint v0 = val0;
	uint v1 = val1;
	uint s0 = 0;

	[unroll]
	for (uint n = 0; n < backoff; ++n) {
		s0 += 0x9e3779b9;
		v0 += ((v1 << 4) + 0xa341316c) ^ (v1 + s0) ^ ((v1 >> 5) + 0xc8013ea4);
		v1 += ((v0 << 4) + 0xad90777d) ^ (v0 + s0) ^ ((v0 >> 5) + 0x7e95761e);
	}

	return v0;
}

void CalculateHitPositionAndSurfaceNormal(float depth, uint2 launchIndex, out float3 hitPosition, out float3 surfaceNormal) {
	float2 tex = (launchIndex + 0.5) / gTextureDim;
	float4 posH = float4(tex.x * 2 - 1, (1 - tex.y) * 2 - 1, 0, 1);
	float4 posV = mul(posH, cb.InvProj);
	posV /= posV.w;

	float dv = NdcDepthToViewDepth(depth, cb.Proj);
	posV = (dv / posV.z) * posV;

	hitPosition = mul(float4(posV.xyz, 1), cb.InvView).xyz;
	surfaceNormal = gi_Normal[launchIndex];
}

bool TraceAORayAndReportIfHit(out float tHit, Ray aoRay, float TMax, float3 surfaceNormal) {
	RayDesc ray;
	// Nudge the origin along the surface normal a bit to avoid starting from
	//  behind the surface due to float calculations imprecision.
	ray.Origin = aoRay.Origin + 0.01 * surfaceNormal;
	ray.Direction = aoRay.Direction;
	ray.TMin = 0;
	ray.TMax = TMax;

	AORayPayload payload = { TMax };

	TraceRay(
		gBVH,
		RAY_FLAG_CULL_FRONT_FACING_TRIANGLES,
		0xFF,
		0,
		0,
		0,
		ray,
		payload
	);

	tHit = payload.tHit;

	return Rtao::HasAORayHitAnyGeometry(tHit);
}

float CalculateAO(out float tHit, uint2 launchIndex, Ray aoRay, float3 surfaceNormal) {
	float occlusion = 0;
	const float TMax = cb.OcclusionRadius;
	if (TraceAORayAndReportIfHit(tHit, aoRay, TMax, surfaceNormal)) {
		float3 hitPosition = aoRay.Origin + tHit * aoRay.Direction;
		float distZ = distance(aoRay.Origin, hitPosition);
		occlusion = OcclusionFunction(distZ, cb.SurfaceEpsilon, cb.OcclusionFadeStart, cb.OcclusionFadeEnd);
	}
	return occlusion;
}

[shader("raygeneration")]
void RtaoRayGen() {
	uint2 launchIndex = DispatchRaysIndex().xy;

	float depth = gi_DepthMap[launchIndex];

	float tHit = Rtao::RayHitDistanceOnMiss;
	float ambientCoef = Rtao::InvalidAOCoefficientValue;

	if (depth < 1) {
		float3 hitPosition;
		float3 surfaceNormal;
		CalculateHitPositionAndSurfaceNormal(depth, launchIndex, hitPosition, surfaceNormal);
		
		uint seed = InitRand(launchIndex.x + launchIndex.y * gTextureDim.x, cb.FrameCount);
		
		float3 direction = CosHemisphereSample(seed, surfaceNormal);
		float flip = sign(dot(direction, surfaceNormal));
		direction = flip * direction;
		
		float occlusionSum = 0;
		
		for (int i = 0; i < cb.SampleCount; ++i) {
			Ray aoRay = { hitPosition, direction };
			occlusionSum += CalculateAO(tHit, launchIndex, aoRay, surfaceNormal);
		}
		
		occlusionSum /= cb.SampleCount;
		ambientCoef = 1 - occlusionSum;
	}

	go_AOCoefficient[launchIndex] = ambientCoef;
	go_RayHitDistance[launchIndex] = Rtao::HasAORayHitAnyGeometry(tHit) ? tHit : cb.OcclusionRadius;
}

[shader("closesthit")]
void RtaoClosestHit(inout AORayPayload payload, Attributes attrib) {
	payload.tHit = RayTCurrent();
}

[shader("miss")]
void RtaoMiss(inout AORayPayload payload) {
	payload.tHit = Rtao::RayHitDistanceOnMiss;
}

#endif // __RTAO_HLSL__