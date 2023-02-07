#ifndef __DXRCOMMON_HLSLI__
#define __DXRCOMMON_HLSLI__

#include "LightingUtil.hlsli"
#include "Samplers.hlsli"

typedef BuiltInTriangleIntersectionAttributes Attributes;

struct HitInfo {
	float4 Color;
};

struct ShadowHitInfo {
	bool IsHit;
};

struct Vertex {
	float3 PosW;
	float3 NormalW;
	float2 TexC;
	float3 TangentW;
};

struct ObjectData {
	float4x4	World;
	float4x4	TexTransform;
	uint		GeometryIndex;
	int			MaterialIndex;
};

struct MaterialData {
	float4		DiffuseAlbedo;
	float3		FresnelR0;
	float		Roughness;
	float4x4	MatTransform;
};

//
// Global root signatures
//
RWTexture2D<float4> gOutput					: register(u0);

RWTexture2D<float4> gShadowMap				: register(u0, space2);

RaytracingAccelerationStructure	gBVH		: register(t0);
StructuredBuffer<ObjectData> gObjects		: register(t1);
StructuredBuffer<MaterialData> gMaterials	: register(t2);

StructuredBuffer<Vertex> gVertices[64]		: register(t0, space1);

ByteAddressBuffer gIndices[64]				: register(t0, space2);

Texture2D gColorMap							: register(t0, space3);
Texture2D gAlbedoMap						: register(t1, space3);
Texture2D gNormalMap						: register(t2, space3);
Texture2D gDepthMap							: register(t3, space3);
Texture2D gSpecularMap						: register(t4, space3);


cbuffer cbPass	: register(b0) {
	float4x4	gView;
	float4x4	gInvView;
	float4x4	gProj;
	float4x4	gInvProj;
	float4x4	gViewProj;
	float4x4	gInvViewProj;
	float4x4	gUnitViewProj;
	float4x4	gViewProjTex;
	float4x4	gShadowTransform;
	float3		gEyePosW;
	float		gPassConstantsPad0;
	float4		gAmbientLight;
	Light		gLights[MaxLights];
};

//
// Local root signatures
//


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

float NdcDepthToViewDepth(float z_ndc) {
	// z_ndc = A + B/viewZ, where gProj[2,2]=A and gProj[3,2]=B.
	float viewZ = gProj[3][2] / (z_ndc - gProj[2][2]);
	return viewZ;
}

#endif // __DXRCOMMON_HLSLI__