#ifndef __RTAO_HLSL__
#define __RTAO_HLSL__

#include "Samplers.hlsli"

typedef BuiltInTriangleIntersectionAttributes Attributes;

struct RtaoHitInfo {
	float3	HitPosition;
	bool	IsOccluded;
};

cbuffer cbRtao : register(b0) {
	float4x4	gView;
	float4x4	gInvView;
	float4x4	gProj;
	float4x4	gInvProj;
	float		gOcclusionRadius;
	float		gOcclusionFadeStart;
	float		gOcclusionFadeEnd;
	float		gSurfaceEpsilon;
	uint		gFrameCount;
	uint		gSampleCount;
	float		gConstantPad1;
	float		gConstantPad2;
};

// Nonnumeric values cannot be added to a cbuffer.
RaytracingAccelerationStructure	gBVH	: register(t0);
Texture2D gNormalMap					: register(t1);
Texture2D gDepthMap						: register(t2);

RWTexture2D<float> gAmbientMap			: register(u0);

float NdcDepthToViewDepth(float z_ndc) {
	// z_ndc = A + B/viewZ, where gProj[2,2]=A and gProj[3,2]=B.
	float viewZ = gProj[3][2] / (z_ndc - gProj[2][2]);
	return viewZ;
}

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

// Takes our seed, updates it, and returns a pseudorandom float in [0..1]
float NextRand(inout uint s) {
	s = (1664525u * s + 1013904223u);
	return float(s & 0x00FFFFFF) / float(0x01000000);
}

// Utility function to get a vector perpendicular to an input vector 
//    (from "Efficient Construction of Perpendicular Vectors Without Branching")
float3 PerpendicularVector(float3 u) {
	float3 a = abs(u);
	uint xm = ((a.x - a.y) < 0 && (a.x - a.z) < 0) ? 1 : 0;
	uint ym = (a.y - a.z) < 0 ? (1 ^ xm) : 0;
	uint zm = 1 ^ (xm | ym);
	return cross(u, float3(xm, ym, zm));
}

// Get a cosine-weighted random vector centered around a specified normal direction.
float3 CosHemisphereSample(inout uint seed, float3 hitNorm) {
	// Get 2 random numbers to select our sample with
	float2 randVal = float2(NextRand(seed), NextRand(seed));

	// Cosine weighted hemisphere sample from RNG
	float3 bitangent = PerpendicularVector(hitNorm);
	float3 tangent = cross(bitangent, hitNorm);
	float r = sqrt(randVal.x);
	float phi = 2.0f * 3.14159265f * randVal.y;

	// Get our cosine-weighted hemisphere lobe sample direction
	return tangent * (r * cos(phi).x) + bitangent * (r * sin(phi)) + hitNorm.xyz * sqrt(1 - randVal.x);
}

// Determines how much the sample point q occludes the point p as a function of dist.
float OcclusionFunction(float dist) {
	//
	// If depth(q) is "behind" depth(p), then q cannot occlude p.  Moreover, if 
	// depth(q) and depth(p) are sufficiently close, then we also assume q cannot
	// occlude p because q needs to be in front of p by Epsilon to occlude p.
	//
	// We use the following function to determine the occlusion.  
	// 
	//
	//       1.0     -------------\
	//               |           |  \
	//               |           |    \
	//               |           |      \ 
	//               |           |        \
	//               |           |          \
	//               |           |            \
	//  ------|------|-----------|-------------|---------|--> zv
	//        0     Eps          z0            z1        
	//
	float occlusion = 0.0f;
	if (dist > gSurfaceEpsilon) {
		float fadeLength = gOcclusionFadeEnd - gOcclusionFadeStart;

		// Linearly decrease occlusion from 1 to 0 as dist goes from gOcclusionFadeStart to gOcclusionFadeEnd.	
		occlusion = saturate((gOcclusionFadeEnd - dist) / fadeLength);
	}

	return occlusion;
}

[shader("raygeneration")]
void RtaoRayGen() {
	float width, height;
	gDepthMap.GetDimensions(width, height);

	uint2 launchIndex = DispatchRaysIndex().xy;
	float2 tex = float2(launchIndex.x / width, launchIndex.y / height);
	float d = gDepthMap.SampleLevel(gsamDepthMap, tex, 0).r;

	if (d < 1.0f) {
		float4 posH = float4((launchIndex.x / width) * 2.0f - 1.0f, (1.0f - (launchIndex.y / height)) * 2.0f - 1.0f, 0.0f, 1.0f);
		float4 posV = mul(posH, gInvProj);
		posV /= posV.w;

		float dv = NdcDepthToViewDepth(d);
		posV = (dv / posV.z) * posV;

		float4 posW = mul(float4(posV.xyz, 1.0f), gInvView);
		float3 normalW = gNormalMap.SampleLevel(gsamPointClamp, tex, 0).xyz;

		uint seed = InitRand(launchIndex.x + launchIndex.y * width, gFrameCount);

		float3 direction = CosHemisphereSample(seed, normalW);
		float flip = sign(dot(direction, normalW));

		float occlusionSum = 0.0f;

		for (int i = 0; i < gSampleCount; ++i) {

			RayDesc ray;
			ray.Origin = posW.xyz;
			ray.Direction = flip * direction;
			ray.TMin = 0.001f;
			ray.TMax = gOcclusionRadius;

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

			float dist = distance(posW.xyz, payload.HitPosition);
			float occlusion = OcclusionFunction(dist);

			occlusionSum += payload.IsOccluded ? occlusion : 0.0f;
		}

		occlusionSum /= gSampleCount;

		gAmbientMap[launchIndex] = 1.0f - occlusionSum;

		return;
	}

	gAmbientMap[launchIndex] = 1.0f;
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