#ifndef __DXRCOMMON_HLSLI__
#define __DXRCOMMON_HLSLI__

#include "LightingUtil.hlsl"

typedef BuiltInTriangleIntersectionAttributes Attributes;

struct RayPayload {
	float4 Color;
};

struct Vertex {
	float3 PosW;
	float3 NormalW;
	float2 TexC;
	float3 TangentW;
};

//
// Global root signatures
//
RWTexture2D<float4>				gOutput			: register(u0);
RaytracingAccelerationStructure	gBVH			: register(t0);
StructuredBuffer<Vertex>		gVertices[64]	: register(t0, space1);
ByteAddressBuffer				gIndices[64]	: register(t0, space2);

cbuffer cbPass	: register(b0) {
	float4x4	gView;
	float4x4	gInvView;
	float4x4	gProj;
	float4x4	gInvProj;
	float4x4	gViewProj;
	float4x4	gInvViewProj;
	float3		gEyePosW;
	float		gPassConstantsPad1;
	float4		gAmbientLight;
	Light		gLights[MaxLights];
};

//
// Local root signatures
//
cbuffer cbMat : register(b1) {
	float4		lDiffuseAlbedo;
	float3		lFresnelR0;
	float		lRoughness;
	float4x4	lMatTransform;
};

//
// Helper functions
//
// Load three 32 bit indices from a byte addressed buffer.
uint3 Load3x32BitIndices(uint offsetBytes, uint instID) {
	return gIndices[instID].Load3(offsetBytes);
}

// Retrieve hit world position.
float3 HitWorldPosition() {
	return WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
}

// Retrieve attribute at a hit position interpolated from vertex attributes using the hit's barycentrics.
float3 HitAttribute(float3 vertexAttribute[3], Attributes attr) {
	return vertexAttribute[0] +
		attr.barycentrics.x * (vertexAttribute[1] - vertexAttribute[0]) +
		attr.barycentrics.y * (vertexAttribute[2] - vertexAttribute[0]);
}

// Generate a ray in world space for a camera pixel corresponding to an index from the dispatched 2D grid.
inline void GenerateCameraRay(uint2 index, out float3 origin, out float3 direction) {
	float2 xy = index + 0.5f; // center in the middle of the pixel.
	float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0f - 1.0f;

	// Invert Y for DirectX-style coordinates.
	screenPos.y = -screenPos.y;

	// Unproject the pixel coordinate into a ray.
	float4 world = mul(float4(screenPos, 0.0f, 1.0f), gInvViewProj);

	world.xyz /= world.w;
	origin = gEyePosW;
	direction = normalize(world.xyz - origin);
}

#endif // __DXRCOMMON_HLSLI__