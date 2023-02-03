#ifndef __SHADOW_HLSL__
#define __SHADOW_HLSL__

#include "Common.hlsli"

struct VertexIn {
	float3 PosL		: POSITION;
	float3 NormalL	: NORMAL;
	float2 TexC		: TEXCOORD;
};

struct VertexOut {
	float4 PosH		: SV_POSITION;
	float2 TexC		: TEXCOORD;
};

VertexOut VS(VertexIn vin) {
	VertexOut vout = (VertexOut)0.0f;

	ObjectData objData = gObjects[gInstanceID];

	float4 posW = mul(float4(vin.PosL, 1.0f), objData.World);
	vout.PosH = mul(posW, gViewProj);

	MaterialData matData = gMaterials[objData.MaterialIndex];

	float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), objData.TexTransform);
	vout.TexC = mul(texC, matData.MatTransform).xy;

	return vout;
}

void PS(VertexOut pin) {
	ObjectData objData = gObjects[gInstanceID];
	MaterialData matData = gMaterials[objData.MaterialIndex];

	float4 diffuseAlbedo = matData.DiffuseAlbedo;
	//if (gDiffuseSrvIndex != -1) diffuseAlbedo *= gTextureMap[gDiffuseSrvIndex].Sample(gsamAnisotropicWrap, pin.TexC);

#ifdef ALPHA_TEST
	// Discard pixel if texture alpha < 0.1.  We do this test as soon 
	// as possible in the shader so that we can potentially exit the
	// shader early, thereby skipping the rest of the shader code.
	clip(diffuseAlbedo.a - 0.1f);
#endif
}

#endif // __SHADOW_HLSL__