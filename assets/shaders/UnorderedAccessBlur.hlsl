#ifndef __UNORDEREDACCESSBLUR_HLSL__
#define __UNORDEREDACCESSBLUR_HLSL__

#include "Samplers.hlsli"

cbuffer cbBlur : register(b0) {
	float4x4	gProj;
	float4		gBlurWeights[3];
	float		gBlurRadius;
	float		gConstantPad0;
	float		gConstantPad1;
	float		gConstantPad2;
}

cbuffer cbRootConstants : register(b1) {
	bool gHorizontalBlur;
};

Texture2D gNormalMap				: register(t0);
Texture2D gDepthMap					: register(t1);

RWTexture2D<float4> gInputMap		: register(u0);
RWTexture2D<float4> gOutputMap		: register(u1);

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
	VertexOut vout = (VertexOut)0.0f;

	vout.TexC = gTexCoords[vid];

	// Quad covering screen in NDC space.
	vout.PosH = float4(2.0f * vout.TexC.x - 1.0f, 1.0f - 2.0f * vout.TexC.y, 0.0f, 1.0f);

	return vout;
}

float NdcDepthToViewDepth(float z_ndc) {
	// z_ndc = A + B/viewZ, where gProj[2,2]=A and gProj[3,2]=B.
	float viewZ = gProj[3][2] / (z_ndc - gProj[2][2]);
	return viewZ;
}

void PS(VertexOut pin) {
	// unpack into float array.
	float blurWeights[12] = {
		gBlurWeights[0].x, gBlurWeights[0].y, gBlurWeights[0].z, gBlurWeights[0].w,
		gBlurWeights[1].x, gBlurWeights[1].y, gBlurWeights[1].z, gBlurWeights[1].w,
		gBlurWeights[2].x, gBlurWeights[2].y, gBlurWeights[2].z, gBlurWeights[2].w,
	};

	uint width, height;
	gInputMap.GetDimensions(width, height);

	float dx = 1.0f / width;
	float dy = 1.0f / height;

	float2 texOffset;
	if (gHorizontalBlur) texOffset = float2(dx, 0.0f);
	else texOffset = float2(0.0f, dy);

	// The center value always contributes to the sum.
	float4 color = blurWeights[gBlurRadius] * gInputMap[uint2(pin.TexC.x * width, pin.TexC.y * height)];
	float totalWeight = blurWeights[gBlurRadius];

#ifndef NON_BILATERAL
	float3 centerNormal = gNormalMap.SampleLevel(gsamPointClamp, pin.TexC, 0.0f).xyz;
	float centerDepth = NdcDepthToViewDepth(gDepthMap.SampleLevel(gsamDepthMap, pin.TexC, 0.0f).r);
#endif 

	for (int i = -gBlurRadius; i <= gBlurRadius; ++i) {
		// We already added in the center weight.
		if (i == 0) continue;

		float2 tex = pin.TexC + i * texOffset;

#ifndef NON_BILATERAL
		float3 neighborNormal = gNormalMap.SampleLevel(gsamPointClamp, tex, 0.0f).xyz;
		float neighborDepth = NdcDepthToViewDepth(gDepthMap.SampleLevel(gsamDepthMap, tex, 0.0f).r);

		//
		// If the center value and neighbor values differ too much (either in 
		// normal or depth), then we assume we are sampling across a discontinuity.
		// We discard such samples from the blur.
		//
		if (dot(neighborNormal, centerNormal) >= 0.8f && abs(neighborDepth - centerDepth) <= 0.2f) {
			float weight = blurWeights[i + gBlurRadius];

			// Add neighbor pixel to blur.
			color += weight * gInputMap[uint2(tex.x * width, tex.y * height)];

			totalWeight += weight;
		}
#else
		float weight = blurWeights[i + gBlurRadius];

		color += weight * gInputMap.SampleLevel(gsamPointClamp, tex, 0.0);

		totalWeight += weight;
#endif
	}

	// Compensate for discarded samples by making total weights sum to 1.
	gOutputMap[uint2(pin.TexC.x * width, pin.TexC.y * height)] = color / totalWeight;
}

#endif // __UNORDEREDACCESSBLUR_HLSL__