#ifndef __HLSLCOMPACTION_H__
#define __HLSLCOMPACTION_H__

#ifdef HLSL
#include "HlslCompaction.hlsli"
#endif

#define MaxLights 16

struct Light {
	DirectX::XMFLOAT3 Strength;
	float FalloffStart;				// point/spot light only
	DirectX::XMFLOAT3 Direction;	// directional/spot light only
	float FalloffEnd;				// point/spot light only
	DirectX::XMFLOAT3 Position;		// point/spot light only
	float SpotPower;				// spot light only
};

struct PassConstants {
	DirectX::XMFLOAT4X4	View;
	DirectX::XMFLOAT4X4	InvView;
	DirectX::XMFLOAT4X4	Proj;
	DirectX::XMFLOAT4X4	InvProj;
	DirectX::XMFLOAT4X4	ViewProj;
	DirectX::XMFLOAT4X4	InvViewProj;
	DirectX::XMFLOAT4X4 UnitViewProj;
	DirectX::XMFLOAT4X4 PrevViewProj;
	DirectX::XMFLOAT4X4 ViewProjTex;
	DirectX::XMFLOAT4X4 ShadowTransform;
	DirectX::XMFLOAT3	EyePosW;
	float				PassConstantsPad0;
	DirectX::XMFLOAT4	AmbientLight;
	Light				Lights[MaxLights];
};

struct ObjectData {
	DirectX::XMFLOAT4X4 World;
	DirectX::XMFLOAT4X4 PrevWorld;
	DirectX::XMFLOAT4X4 TexTransform;
	UINT				GeometryIndex;
	int					MaterialIndex;
};

struct MaterialData {
	DirectX::XMFLOAT4	DiffuseAlbedo;
	DirectX::XMFLOAT3	FresnelR0;
	float				Roughness;
	DirectX::XMFLOAT4X4	MatTransform;
};

struct SsaoConstants {
	DirectX::XMFLOAT4X4	View;
	DirectX::XMFLOAT4X4	InvView;
	DirectX::XMFLOAT4X4	Proj;
	DirectX::XMFLOAT4X4	InvProj;
	DirectX::XMFLOAT4X4	ProjTex;
	DirectX::XMFLOAT4	OffsetVectors[14];
	float				OcclusionRadius;
	float				OcclusionFadeStart;
	float				OcclusionFadeEnd;
	float				SurfaceEpsilon;
};

struct BlurConstants {
	DirectX::XMFLOAT4X4	Proj;
	DirectX::XMFLOAT4	BlurWeights[3];
	float				BlurRadius;
	float				ConstantPad0;
	float				ConstantPad1;
	float				ConstantPad2;
};

struct RtaoConstants {
	DirectX::XMFLOAT4X4	View;
	DirectX::XMFLOAT4X4	InvView;
	DirectX::XMFLOAT4X4	Proj;
	DirectX::XMFLOAT4X4	InvProj;
	float				OcclusionRadius;
	float				OcclusionFadeStart;
	float				OcclusionFadeEnd;
	float				SurfaceEpsilon;
	UINT				FrameCount;
	UINT				SampleCount;
	float				ConstantPad1;
	float				ConstantPad2;
};

struct CrossBilateralFilterConstants {
	float	DepthSigma;
	UINT	DepthNumMantissaBits;
};

struct CalcLocalMeanVarianceConstants {
	DirectX::XMUINT2 TextureDimension;
	UINT	KernelWidth;
	UINT	KernelRadius;
	BOOL	CheckerboardSamplingEnabled;
	BOOL	EvenPixelActivated;
	UINT	PixelStepY;
	float	ConstantPad0;
};

struct TemporalSupersamplingBlendWithCurrentFrameConstants {
	float StdDevGamma;
	BOOL ClampCachedValues;
	float ClampingMinStdDevTolerance;
	float ConstnatPad0;

	float ClampDifferenceToTsppScale;
	BOOL ForceUseMinSmoothingFactor;
	float MinSmoothingFactor;
	UINT MinTsppToUseTemporalVariance;

	UINT BlurStrengthMaxTspp;
	float BlurDecayStrength;
	BOOL CheckerboardEnabled;
	BOOL CheckerboardEvenPixelActivated;
};

namespace ScreenSpaceAOShaderParams {
	static const int SampleCount = 14;
}

namespace GaussianBlurComputeShaderParams {
	static const int MaxBlurRadius = 5;

	namespace ThreadGroup {
		enum Enum {
			Size = 256
		};
	}
}

namespace DefaultComputeShaderParams {
	namespace ThreadGroup {
		enum Enum {
			Width	= 8,
			Height	= 8,
			Size	= Width * Height
		};
	}
}

#endif // __HLSLCOMPACTION_H__