#ifndef __GBUFFER_HLSL__
#define __GBUFFER_HLSL__

#ifndef HLSL
#define HLSL
#endif

#include "./../../include/HlslCompaction.h"
#include "ShadingHelpers.hlsli"
#include "LightingUtil.hlsli"
#include "Samplers.hlsli"

ConstantBuffer<PassConstants> cbPass : register(b0);

cbuffer cbRootConstants : register(b1) {
	uint gInstanceID;
	bool gIsRaytracing;
}

StructuredBuffer<ObjectData> gObjects		: register(t0, space1);
StructuredBuffer<MaterialData> gMaterials	: register(t0, space2);

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
	float3 PrevNormalW	: PREVNORMAL;
	float2 TexC			: TEXCOORD;
};

struct PixelOut {
	float4 Color					: SV_TARGET0;
	float4 Albedo					: SV_TARGET1;
	float4 NormalDepth				: SV_TARGET2;
	float4 Specular					: SV_TARGET3;
	float2 Velocity					: SV_TARGET4;
	float4 ReprojectedNormalDepth	: SV_TARGET5;
};

VertexOut VS(VertexIn vin) {
	VertexOut vout = (VertexOut)0.0f;

	ObjectData objData = gObjects[gInstanceID];
	
	float4 posW = mul(float4(vin.PosL, 1.0f), objData.World);
	vout.PosW = posW.xyz;
	vout.PosH = mul(posW, cbPass.ViewProj);
	vout.NonJitPosH = vout.PosH;

	float4 prevPosW = mul(float4(vin.PosL, 1.0f), objData.PrevWorld);
	vout.PrevPosW = prevPosW.xyz;
	vout.PrevPosH = mul(prevPosW, cbPass.PrevViewProj);

	vout.NormalW = mul(vin.NormalL, (float3x3)objData.World);
	vout.PrevNormalW = mul(vin.NormalL, (float3x3)objData.PrevWorld);

	MaterialData matData = gMaterials[objData.MaterialIndex];

	float4 texC = mul(float4(vin.TexC, 0.0f, 1.0f), objData.TexTransform);
	vout.TexC = mul(texC, matData.MatTransform).xy;

	return vout;
}

PixelOut PS(VertexOut pin) {
	ObjectData objData = gObjects[gInstanceID];
	MaterialData matData = gMaterials[objData.MaterialIndex];

	pin.NormalW = normalize(pin.NormalW);
	pin.PrevNormalW = normalize(pin.PrevNormalW);

	float3 toEyeW = normalize(cbPass.EyePosW - pin.PosW);

	pin.NonJitPosH /= pin.NonJitPosH.w;
	pin.PrevPosH /= pin.PrevPosH.w;
	float2 velocity = CalcVelocity(pin.NonJitPosH, pin.PrevPosH);

	PixelOut pout = (PixelOut)0.0f;
	pout.Color = matData.DiffuseAlbedo;
	pout.Albedo = matData.DiffuseAlbedo;
	pout.NormalDepth = float4(pin.NormalW, pin.NonJitPosH.z);
	pout.Specular = float4(matData.FresnelR0, matData.Roughness);
	pout.Velocity = velocity;
	pout.ReprojectedNormalDepth = float4(pin.PrevNormalW, pin.PrevPosH.z);
	return pout;
}

#endif // __GBUFFER_HLSL__