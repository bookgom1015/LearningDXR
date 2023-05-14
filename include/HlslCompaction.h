#ifndef __HLSLCOMPACTION_H__
#define __HLSLCOMPACTION_H__

#ifdef HLSL
#include "HlslCompaction.hlsli"
#endif

#define MaxLights 16

struct Ray {
	DirectX::XMFLOAT3 Origin;
	DirectX::XMFLOAT3 Direction;
};

struct Light {
	DirectX::XMFLOAT3 Strength;
	float FalloffStart;				// point/spot light only
	DirectX::XMFLOAT3 Direction;	// directional/spot light only
	float FalloffEnd;				// point/spot light only
	DirectX::XMFLOAT3 Position;		// point/spot light only
	float SpotPower;				// spot light only
};

struct Vertex {
	DirectX::XMFLOAT3 Pos;
	DirectX::XMFLOAT3 Normal;
	DirectX::XMFLOAT2 TexC;
	DirectX::XMFLOAT3 Tangent;
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

struct DebugConstants {
	float	RtaoOcclusionRadius;
	UINT	MaxTspp;
	float	ConstantPads[2];
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

	float OcclusionRadius;
	float OcclusionFadeStart;
	float OcclusionFadeEnd;
	float SurfaceEpsilon;

	UINT FrameCount;
	UINT SampleCount;
	float ConstantPad[2];
};

struct CrossBilateralFilterConstants {
	float	DepthSigma;
	UINT	DepthNumMantissaBits;
	float	ConstantPad0;
	float	ConstantPad1;
};

struct CalcLocalMeanVarianceConstants {
	DirectX::XMUINT2 TextureDim;
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

struct AtrousWaveletTransformFilterConstantBuffer {
	DirectX::XMUINT2 TextureDim;
	float DepthWeightCutoff;
	bool UsingBilateralDownsamplingBuffers;

	BOOL UseAdaptiveKernelSize;
	float KernelRadiusLerfCoef;
	UINT MinKernelWidth;
	UINT MaxKernelWidth;

	float RayHitDistanceToKernelWidthScale;
	float RayHitDistanceToKernelSizeScaleExponent;
	BOOL PerspectiveCorrectDepthInterpolation;
	float MinVarianceToDenoise;

	float ValueSigma;
	float DepthSigma;
	float NormalSigma;
	float FovY;
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

namespace DebugShadeParams {
	static const int MapCount = 5;

	namespace DisplayMark {
		enum {
			RGB		= 1 << 0,
			RG			= 1 << 1,
			RRR			= 1 << 2,
			GGG			= 1 << 3,
			BBB			= 1 << 4,
			AAA			= 1 << 5,
			RayHitDist	= 1 << 6
		};
	}
}

namespace AtrousWaveletTransformFilterShaderParams {
	namespace ThreadGroup {
		enum Enum {
			Width	= 16,
			Height	= 16,
			Size	= Width * Height
		};
	}
}

#endif // __HLSLCOMPACTION_H__