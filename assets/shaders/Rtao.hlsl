#ifndef __RTAO_HLSL__
#define __RTAO_HLSL__

#ifndef HLSL
#define HLSL
#endif

#include "./../../include/HlslCompaction.h"
#include "ShadingHelpers.hlsli"
#include "RandGenerator.hlsli"
#include "Samplers.hlsli"

typedef BuiltInTriangleIntersectionAttributes Attributes;

struct RtaoHitInfo {
	float3	HitPosition;
	bool	IsOccluded;
};

ConstantBuffer<RtaoConstants> cbRtao		: register(b0);

cbuffer cbRootConstants : register(b1) {
	uint2 gDimension;
};

// Nonnumeric values cannot be added to a cbuffer.
RaytracingAccelerationStructure	gBVH		: register(t0);
Texture2D<float3> giNormalMap				: register(t1);
Texture2D<float> giDepthMap					: register(t2);

RWTexture2D<float> goAmbientCoefficientMap	: register(u0);

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

[shader("raygeneration")]
void RtaoRayGen() {
	uint2 launchIndex = DispatchRaysIndex().xy;

	float d = giDepthMap[launchIndex];

	if (d < 1.0f) {
		float2 tex = float2((launchIndex.x + 0.5f) / gDimension.x, (launchIndex.y + 0.5f) / gDimension.y);
		float4 posH = float4(tex.x * 2.0f - 1.0f, (1.0f - tex.y) * 2.0f - 1.0f, 0.0f, 1.0f);
		float4 posV = mul(posH, cbRtao.InvProj);
		posV /= posV.w;

		float dv = NdcDepthToViewDepth(d, cbRtao.Proj);
		posV = (dv / posV.z) * posV;

		float4 posW = mul(float4(posV.xyz, 1.0f), cbRtao.InvView);
		float3 normalW = giNormalMap[launchIndex];

		uint seed = InitRand(launchIndex.x + launchIndex.y * gDimension.x, cbRtao.FrameCount);

		float3 direction = CosHemisphereSample(seed, normalW);
		float flip = sign(dot(direction, normalW));

		float occlusionSum = 0.0f;

		for (int i = 0; i < cbRtao.SampleCount; ++i) {

			RayDesc ray;
			ray.Origin = posW.xyz;
			ray.Direction = flip * direction;
			ray.TMin = 0.001f;
			ray.TMax = cbRtao.OcclusionRadius;

			RtaoHitInfo payload;
			payload.IsOccluded = false;

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

			float distZ = distance(posW.xyz, payload.HitPosition);
			float occlusion = OcclusionFunction(distZ, cbRtao.SurfaceEpsilon, cbRtao.OcclusionFadeStart, cbRtao.OcclusionFadeEnd);

			occlusionSum += payload.IsOccluded ? occlusion : 0.0f;
		}

		occlusionSum /= cbRtao.SampleCount;

		goAmbientCoefficientMap[launchIndex] = 1.0f - occlusionSum;

		return;
	}

	goAmbientCoefficientMap[launchIndex] = 1.0f;
}

[shader("closesthit")]
void RtaoClosestHit(inout RtaoHitInfo payload, Attributes attrib) {
	payload.IsOccluded = true;
	payload.HitPosition = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
}

[shader("miss")]
void RtaoMiss(inout RtaoHitInfo payload) {
	payload.IsOccluded = false;
}

#endif // __RTAO_HLSL__