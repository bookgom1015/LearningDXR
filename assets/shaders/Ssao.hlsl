#ifndef __SSAO_HLSL__
#define __SSAO_HLSL__

#ifndef HLSL
#define HLSL
#endif

#include "./../../include/HlslCompaction.h"
#include "ShadingHelpers.hlsli"
#include "Samplers.hlsli"

ConstantBuffer<SsaoConstants> cbSsao : register(b0);

// Nonnumeric values cannot be added to a cbuffer.
Texture2D<float3> gNormalMap		: register(t0);
Texture2D<float> gDepthMap			: register(t1);
Texture2D<float3> gRandomVectorMap	: register(t2);

static const float2 gTexCoords[6] = {
	float2(0.0f, 1.0f),
	float2(0.0f, 0.0f),
	float2(1.0f, 0.0f),
	float2(0.0f, 1.0f),
	float2(1.0f, 0.0f),
	float2(1.0f, 1.0f)
};

struct VertexOut {
	float4 PosH : SV_POSITION;
	float3 PosV : POSITION;
	float2 TexC : TEXCOORD0;
};

VertexOut VS(uint vid : SV_VertexID) {
	VertexOut vout;

	vout.TexC = gTexCoords[vid];

	vout.PosH = float4(2.0f * vout.TexC.x - 1.0f, 1.0f - 2.0f * vout.TexC.y, 0.0f, 1.0f);

	// Transform quad corners to view space near plane.
	float4 ph = mul(vout.PosH, cbSsao.InvProj);
	vout.PosV = ph.xyz / ph.w;

	return vout;
}

float4 PS(VertexOut pin) : SV_Target {
	// p -- the point we are computing the ambient occlusion for.
	// n -- normal vector at p.
	// q -- a random offset from p.
	// r -- a potential occluder that might occlude p.

	// Get viewspace normal and z-coord of this pixel.  
	float3 n = mul(gNormalMap.Sample(gsamPointClamp, pin.TexC), (float3x3)cbSsao.View);

	float pz = gDepthMap.Sample(gsamDepthMap, pin.TexC);
	pz = NdcDepthToViewDepth(pz, cbSsao.Proj);

	//
	// Reconstruct full view space position (x,y,z).
	// Find t such that p = t*pin.PosV.
	// p.z = t*pin.PosV.z
	// t = p.z / pin.PosV.z
	//
	float3 p = (pz / pin.PosV.z) * pin.PosV;

	// Extract random vector and map from [0,1] --> [-1, +1].
	float3 randVec = 2.0f * gRandomVectorMap.Sample(gsamLinearWrap, 4.0f * pin.TexC) - 1.0f;

	float occlusionSum = 0.0f;

	// Sample neighboring points about p in the hemisphere oriented by n.
	for (int i = 0; i < ScreenSpaceAOShaderParams::SampleCount; ++i) {
		// Are offset vectors are fixed and uniformly distributed (so that our offset vectors
		// do not clump in the same direction).  If we reflect them about a random vector
		// then we get a random uniform distribution of offset vectors.
		float3 offset = reflect(cbSsao.OffsetVectors[i].xyz, randVec);

		// Flip offset vector if it is behind the plane defined by (p, n).
		float flip = sign(dot(offset, n));

		// Sample a point near p within the occlusion radius.
		float3 q = p + flip * cbSsao.OcclusionRadius * offset;

		// Project q and generate projective tex-coords.  
		float4 projQ = mul(float4(q, 1.0f), cbSsao.ProjTex);
		projQ /= projQ.w;

		// Find the nearest depth value along the ray from the eye to q (this is not
		// the depth of q, as q is just an arbitrary point near p and might
		// occupy empty space).  To find the nearest depth we look it up in the depthmap.
		float rz = gDepthMap.Sample(gsamDepthMap, projQ.xy);
		rz = NdcDepthToViewDepth(rz, cbSsao.Proj);

		// Reconstruct full view space position r = (rx,ry,rz).  We know r
		// lies on the ray of q, so there exists a t such that r = t*q.
		// r.z = t*q.z ==> t = r.z / q.z
		float3 r = (rz / q.z) * q;

		//
		// Test whether r occludes p.
		//   * The product dot(n, normalize(r - p)) measures how much in front
		//     of the plane(p,n) the occluder point r is.  The more in front it is, the
		//     more occlusion weight we give it.  This also prevents self shadowing where 
		//     a point r on an angled plane (p,n) could give a false occlusion since they
		//     have different depth values with respect to the eye.
		//   * The weight of the occlusion is scaled based on how far the occluder is from
		//     the point we are computing the occlusion of.  If the occluder r is far away
		//     from p, then it does not occlude it.
		// 
		float distZ = p.z - r.z;
		float dp = max(dot(n, normalize(r - p)), 0.0f);

		float occlusion = dp * OcclusionFunction(distZ, cbSsao.SurfaceEpsilon, cbSsao.OcclusionFadeStart, cbSsao.OcclusionFadeEnd);

		occlusionSum += occlusion;
	}

	occlusionSum /= ScreenSpaceAOShaderParams::SampleCount;

	float access = 1.0f - occlusionSum;

	// Sharpen the contrast of the SSAO map to make the SSAO affect more dramatic.
	return saturate(pow(access, 6.0f));
}

#endif // __SSAO_HLSL__