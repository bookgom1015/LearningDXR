#ifndef __HLSLCOMPACTION_H__
#define __HLSLCOMPACTION_H__

#ifdef HLSL
#include "HlslCompaction.hlsli"
#else
#include <unordered_map>
#include <MathHelper.h>
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

#ifndef HLSL
	bool operator==(const Vertex& other) const {
		return MathHelper::IsEqual(Pos, other.Pos) &&
			MathHelper::IsEqual(Normal, other.Normal) &&
			MathHelper::IsEqual(TexC, other.TexC);
	}
#endif
};

#ifndef HLSL
namespace std {
	template<> struct hash<Vertex> {
		size_t operator()(Vertex const& vertex) const {
			size_t pos = static_cast<size_t>(vertex.Pos.x + vertex.Pos.y + vertex.Pos.z);
			size_t normal = static_cast<size_t>(vertex.Normal.x + vertex.Normal.y + vertex.Normal.z);
			size_t texc = static_cast<size_t>(vertex.TexC.x + vertex.TexC.y);
			return (pos ^ normal ^ texc);
		}
	};
}

struct Mesh {
	std::unordered_map<Vertex, std::uint32_t>	UniqueVertices;
	std::vector<Vertex>							Vertices;
	std::vector<std::uint32_t>					Indices;
};
#endif

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

namespace DebugShaderParams {
	static const int MapCount = 5;

	namespace SampleMask {
		enum Type {
			RGB			= 0,
			RG			= 1 << 0,
			RRR			= 1 << 1,
			GGG			= 1 << 2,
			BBB			= 1 << 3,
			AAA			= 1 << 4,
			RayHitDist	= 1 << 5
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