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
	float4 NonJitPosH	: NONJITPOSH;
	float4 PrevPosH		: PREVPOSH;
	float3 PrevPosW		: PREVPOSW;
	float3 NormalW		: NORMAL;
	float2 TexC			: TEXCOORD;
};

struct PixelOut {
	float4 Color	: SV_TARGET0;
	float4 Albedo	: SV_TARGET1;
	float4 Normal	: SV_TARGET2;
	float4 Specular	: SV_TARGET3;
	float2 Velocity	: SV_TARGET4;
};

VertexOut VS(VertexIn vin) {
	VertexOut vout = (VertexOut)0.0f;

	ObjectData objData = gObjects[gInstanceID];
	
	float4 posW = mul(float4(vin.PosL, 1.0f), objData.World);
	vout.PosW = posW.xyz;
	vout.PosH = mul(posW, gViewProj);
	vout.NonJitPosH = vout.PosH;

	float4 prevPosW = mul(float4(vin.PosL, 1.0f), objData.PrevWorld);
	vout.PrevPosW = prevPosW.xyz;
	vout.PrevPosH = mul(prevPosW, gPrevViewProj);

	vout.NormalW = mul(vin.NormalL, (float3x3)objData.World);

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

	pin.NonJitPosH /= pin.NonJitPosH.w;
	pin.PrevPosH /= pin.PrevPosH.w;
	float2 velocity = CalcVelocity(pin.NonJitPosH, pin.PrevPosH);

	PixelOut pout = (PixelOut)0.0f;
	pout.Color = matData.DiffuseAlbedo;
	pout.Albedo = matData.DiffuseAlbedo;
	pout.Normal = float4(pin.NormalW, 0.0f);
	pout.Specular = float4(matData.FresnelR0, matData.Roughness);
	pout.Velocity = velocity;
	return pout;
}

#endif // __GBUFFER_HLSL__