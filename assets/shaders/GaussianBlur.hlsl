#ifndef __GAUSSIANBLUR_HLSL__
#define __GAUSSIANBLUR_HLSL__

#ifndef HLSL
#define HLSL
#endif

#include "./../../include/HlslCompaction.h"
#include "ShadingHelpers.hlsli"
#include "Samplers.hlsli"

ConstantBuffer<BlurConstants> cbBlur : register(b0);

cbuffer cbRootConstants : register(b1) {
	float	gDotThreshold;
	float	gDepthThresHold;
	bool	gHorizontalBlur;
};

Texture2D<float3> gNormalMap	: register(t0);
Texture2D<float> gDepthMap		: register(t1);
Texture2D<float4> gInputMap		: register(t2);

static const float2 gTexCoords[6] = {
	float2(0.0f, 1.0f),
	float2(0.0f, 0.0f),
	float2(1.0f, 0.0f),
	float2(0.0f, 1.0f),
	float2(1.0f, 0.0f),
	float2(1.0f, 1.0f)
};

struct VertexOut {
	float4 PosH  : SV_POSITION;
	float2 TexC  : TEXCOORD;
};

VertexOut VS(uint vid : SV_VertexID) {
	VertexOut vout;

	vout.TexC = gTexCoords[vid];

	// Quad covering screen in NDC space.
	vout.PosH = float4(2.0f * vout.TexC.x - 1.0f, 1.0f - 2.0f * vout.TexC.y, 0.0f, 1.0f);

	return vout;
}

float4 PS(VertexOut pin) : SV_Target {
	// unpack into float array.
	float blurWeights[12] = {
		cbBlur.BlurWeights[0].x, cbBlur.BlurWeights[0].y, cbBlur.BlurWeights[0].z, cbBlur.BlurWeights[0].w,
		cbBlur.BlurWeights[1].x, cbBlur.BlurWeights[1].y, cbBlur.BlurWeights[1].z, cbBlur.BlurWeights[1].w,
		cbBlur.BlurWeights[2].x, cbBlur.BlurWeights[2].y, cbBlur.BlurWeights[2].z, cbBlur.BlurWeights[2].w,
	};

	uint width, height;
	gInputMap.GetDimensions(width, height);

	float dx = 1.0f / width;
	float dy = 1.0f / height;

	float2 texOffset;
	if (gHorizontalBlur) texOffset = float2(dx, 0.0f);
	else texOffset = float2(0.0f, dy);

	// The center value always contributes to the sum.
	float4 color = blurWeights[cbBlur.BlurRadius] * gInputMap.Sample(gsamLinearClamp, pin.TexC);
	float totalWeight = blurWeights[cbBlur.BlurRadius];

#ifndef NON_BILATERAL
	float3 centerNormal = gNormalMap.Sample(gsamPointClamp, pin.TexC);
	float centerDepth = NdcDepthToViewDepth(gDepthMap.Sample(gsamDepthMap, pin.TexC), cbBlur.Proj);
#endif 

	for (int i = -cbBlur.BlurRadius; i <= cbBlur.BlurRadius; ++i) {
		// We already added in the center weight.
		if (i == 0) continue;

		float2 tex = pin.TexC + i * texOffset;

#ifndef NON_BILATERAL
		float3 neighborNormal = normalize(gNormalMap.Sample(gsamPointClamp, tex));
		float neighborDepth = NdcDepthToViewDepth(gDepthMap.Sample(gsamDepthMap, tex), cbBlur.Proj);

		//
		// If the center value and neighbor values differ too much (either in 
		// normal or depth), then we assume we are sampling across a discontinuity.
		// We discard such samples from the blur.
		//
		if (dot(neighborNormal, centerNormal) >= gDotThreshold && abs(neighborDepth - centerDepth) <= gDepthThresHold) {
			float weight = blurWeights[i + cbBlur.BlurRadius];

			// Add neighbor pixel to blur.
			color += weight * gInputMap.Sample(gsamLinearClamp, tex);

			totalWeight += weight;
		}
#else
		float weight = blurWeights[i + cbBlur.BlurRadius];

		color += weight * gInputMap.Sample(gsamLinearClamp, tex);

		totalWeight += weight;
#endif
	}

	// Compensate for discarded samples by making total weights sum to 1.
	return color / totalWeight;
}

#endif // __GAUSSIANBLUR_HLSL__