#ifndef __GBUFFER_HLSL__
#define __GBUFFER_HLSL__

#include "Common.hlsli"

struct VertexIn {
	float3 PosL		: POSITION;
	float3 NormalL	: NORMAL;
	float2 TexC		: TEXCOORD;
};

struct VertexOut {
	float4 PosH			: SV_POSITION;
	float3 PosW			: POSITION;
	float3 NormalW		: NORMAL;
	float2 TexC			: TEXCOORD;
};

struct PixelOut {
	float4 Color	: SV_TARGET0;
	float4 Albedo	: SV_TARGET1;
	float4 Normal	: SV_TARGET2;
	float4 Specular	: SV_TARGET3;
};

VertexOut VS(VertexIn vin) {
	VertexOut vout = (VertexOut)0.0f;

	ObjectData objData = gObjects[gInstanceID];
	
	float4 posW = mul(float4(vin.PosL, 1.0f), objData.World);
	vout.PosW = posW.xyz;

	vout.NormalW = mul(vin.NormalL, (float3x3)objData.World);

	vout.PosH = mul(posW, gViewProj);

	MaterialData matData = gMaterials[objData.MaterialIndex];

	float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), objData.TexTransform);
	vout.TexC = mul(texC, matData.MatTransform).xy;

	return vout;
}

PixelOut PS(VertexOut pin) {
	ObjectData objData = gObjects[gInstanceID];
	MaterialData matData = gMaterials[objData.MaterialIndex];

	pin.NormalW = normalize(pin.NormalW);

	float3 toEyeW = normalize(gEyePosW - pin.PosW);

	PixelOut pout = (PixelOut)0.0f;
	pout.Color = matData.DiffuseAlbedo;
	pout.Albedo = matData.DiffuseAlbedo;
	pout.Normal = float4(pin.NormalW, 0.0f);
	pout.Specular = float4(matData.FresnelR0, matData.Roughness);
	return pout;
}

#endif // __GBUFFER_HLSL__