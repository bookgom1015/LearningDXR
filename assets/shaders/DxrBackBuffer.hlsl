#ifndef __DXRBUFFER_HLSL__
#define __DXRBUFFER_HLSL__

#ifndef NUM_DIR_LIGHTS
#define NUM_DIR_LIGHTS 1
#endif

#ifndef NUM_POINT_LIGHTS
#define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
#define NUM_SPOT_LIGHTS 0
#endif

#ifndef HLSL
#define HLSL
#endif

#include "./../../include/HlslCompaction.h"
#include "ShadingHelpers.hlsli"
#include "LightingUtil.hlsli"
#include "Samplers.hlsli"

ConstantBuffer<PassConstants> cbPass : register(b0);

Texture2D<float4> gColorMap				: register(t0);
Texture2D<float4> gAlbedoMap			: register(t1);
Texture2D<float3> gNormalMap			: register(t2);
Texture2D<float> gDepthMap				: register(t3);
Texture2D<float4> gSpecularMap			: register(t4);
Texture2D<float> gShadowMap				: register(t5);
Texture2D<float> gAmbientCoefficientMap	: register(t6);

static const float2 gTexCoords[6] = {
	float2(0.0f, 1.0f),
	float2(0.0f, 0.0f),
	float2(1.0f, 0.0f),
	float2(0.0f, 1.0f),
	float2(1.0f, 0.0f),
	float2(1.0f, 1.0f)
};

struct VertexOut {
	float4 PosH		: SV_POSITION;
	float3 PosV		: POSITION;
	float2 TexC		: TEXCOORD;
};

VertexOut VS(uint vid : SV_VertexID) {
	VertexOut vout = (VertexOut)0.0f;

	vout.TexC = gTexCoords[vid];

	// Quad covering screen in NDC space.
	vout.PosH = float4(2.0f * vout.TexC.x - 1.0f, 1.0f - 2.0f * vout.TexC.y, 0.0f, 1.0f);

	// Transform quad corners to view space near plane.
	float4 ph = mul(vout.PosH, cbPass.InvProj);
	vout.PosV = ph.xyz / ph.w;

	return vout;
}

float4 PS(VertexOut pin) : SV_Target {
	// Get viewspace normal and z-coord of this pixel.  
	float pz = gDepthMap.Sample(gsamDepthMap, pin.TexC).r;
	pz = NdcDepthToViewDepth(pz, cbPass.Proj);

	//
	// Reconstruct full view space position (x,y,z).
	// Find t such that p = t*pin.PosV.
	// p.z = t*pin.PosV.z
	// t = p.z / pin.PosV.z
	//
	float3 posV = (pz / pin.PosV.z) * pin.PosV;
	float4 posW = mul(float4(posV, 1.0f), cbPass.InvView);

	float3 normalW = normalize(gNormalMap.Sample(gsamPointClamp, pin.TexC).rgb);
	float3 toEyeW = normalize(cbPass.EyePosW - posW.xyz);

	float4 colorSample = gColorMap.Sample(gsamPointClamp, pin.TexC);
	float4 albedoSample = gAlbedoMap.Sample(gsamPointClamp, pin.TexC);
	float4 diffuseAlbedo = colorSample * albedoSample;
	
	float ambientAccess;
	{
		uint width, height;
		gAmbientCoefficientMap.GetDimensions(width, height);

		uint2 index = uint2(pin.TexC.x * width - 0.5f, pin.TexC.y * height - 0.5f);

		ambientAccess = gAmbientCoefficientMap[index];
	}

	float4 ambient = ambientAccess * cbPass.AmbientLight * diffuseAlbedo;

	float4 specular = gSpecularMap.Sample(gsamPointClamp, pin.TexC);
	const float shiness = 1.0f - specular.a;
	Material mat = { albedoSample, specular.rgb, shiness };

	float3 shadowFactor = (float3)1.0f;
	shadowFactor[0] = gShadowMap.Sample(gsamPointClamp, pin.TexC);

	float4 directLight = ComputeLighting(cbPass.Lights, mat, posW, normalW, toEyeW, shadowFactor);

	float4 litColor = ambient + directLight;
	litColor.a = diffuseAlbedo.a;

	return litColor;
}

#endif // __DXRBACKBUFFER_HLSL__