#ifndef __SHADOWRAY_HLSL__
#define __SHADOWRAY_HLSL__

#include "DxrCommon.hlsli"

struct ShadowHitInfo {
	bool IsHit;
};

[shader("raygeneration")]
void ShadowRayGen() {
	float width, height;
	gDepthMap.GetDimensions(width, height);

	uint2 launchIndex = DispatchRaysIndex().xy;
	float2 tex = float2((launchIndex.x + 0.5f) / width, (launchIndex.y + 0.5f) / height);
	float d = gDepthMap.SampleLevel(gsamDepthMap, tex, 0).r;
	
	if (d < 1.0f) {
		float4 posH = float4(tex.x * 2.0f - 1.0f, (1.0f - tex.y) * 2.0f - 1.0f, 0.0f, 1.0f);
		float4 posV = mul(posH, gInvProj);
		posV /= posV.w;
	
		float dv = NdcDepthToViewDepth(d);
		posV = (dv / posV.z) * posV;
	
		float4 posW = mul(float4(posV.xyz, 1.0f), gInvView);
	
		RayDesc ray;
		ray.Origin = posW.xyz;
		ray.Direction = -gLights[0].Direction;
		ray.TMin = 0.001f;
		ray.TMax = 1000.0f;
	
		ShadowHitInfo payload;
		payload.IsHit = false;
	
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
	
		float shadowFactor = payload.IsHit ? 0.0f : 1.0f;
		float4 color = float4((float3)shadowFactor, 1.0f);

		gShadowMap0[launchIndex] = shadowFactor;
	
		return;
	}

	gShadowMap0[launchIndex] = 1.0f;
}

[shader("closesthit")]
void ShadowClosestHit(inout ShadowHitInfo payload, Attributes attrib) {
	payload.IsHit = true;
}

[shader("miss")]
void ShadowMiss(inout ShadowHitInfo payload) {
	payload.IsHit = false;
}

#endif // __SHADOWRAY_HLSL__