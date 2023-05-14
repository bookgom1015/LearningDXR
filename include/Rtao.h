#pragma once

#include <d3dx12.h>
#include <array>
#include <unordered_map>

#include "MathHelper.h"
#include "Samplers.h"

class ShaderManager;

namespace Rtao {
	namespace CalcAmbientOcclusion {
		namespace RootSignatureLayout {
			enum {
				ESI_AccelerationStructure = 0,
				ECB_RtaoPass,
				EC_Consts,
				ESI_Normal,
				ESI_Depth,
				EUO_AOCoefficient,
				EUO_RayHitDistance,
				Count
			};
		}

		namespace RootConstantsLayout {
			enum {
				ETextureDim_X = 0,
				ETextureDim_Y,
				Count
			};
		}
	}

	namespace TemporalSupersamplingReverseReproject {
		namespace RootSignatureLayout {
			enum {
				ECB_CrossBilateralFilter = 0,
				EC_Consts,
				ESI_NormalDepth,
				ESI_DepthPartialDerivative,
				ESI_ReprojectedNormalDepth,
				ESI_CachedNormalDepth,
				ESI_Velocity,
				ESI_CachedAOCoefficient,
				ESI_CachedTspp,
				ESI_CachedAOCoefficientSquaredMean,
				ESI_CachedRayHitDistance,
				EUO_CachedTspp,
				EUO_TsppCoefficientSquaredMeanRayHitDistacne,
				Count
			};
		}

		namespace RootConstantsLayout {
			enum {
				ETextureDim_X = 0,
				ETextureDim_Y,
				EInvTextureDim_X,
				EInvTextureDim_Y,
				Count
			};
		}
	}

	namespace TemporalSupersamplingBlendWithCurrentFrame {
		namespace RootSignatureLayout {
			enum {
				ECB_TsspBlendWithCurrentFrame = 0,
				ESI_AOCoefficient,
				ESI_LocalMeanVaraince,
				ESI_RayHitDistance,
				ESI_TsppCoefficientSquaredMeanRayHitDistance,
				EUIO_TemporalAOCoefficient,
				EUIO_Tspp,
				EUIO_CoefficientSquaredMean,
				EUIO_RayHitDistance,
				EUO_VarianceMap,
				EUO_BlurStrength,
				Count
			};
		}
	}

	namespace CalcDepthPartialDerivative {
		namespace RootSignatureLayout {
			enum {
				EC_Consts = 0,
				ESI_Depth,
				EUO_DepthPartialDerivative,
				Count
			};
		}

		namespace RootConstantsLayout {
			enum {
				EInvTextureDim_X = 0,
				EInvTextureDim_Y,
				Count
			};
		}
	}

	namespace CalcLocalMeanVariance {
		namespace RootSignatureLayout {
			enum {
				ECB_LocalMeanVar = 0,
				ESI_AOCoefficient,
				EUO_LocalMeanVar,
				Count
			};
		}
	}

	namespace FillInCheckerboard {
		namespace RootSignatureLayout {
			enum {
				ECB_LocalMeanVar = 0,
				EUIO_LocalMeanVar,
				Count
			};
		}
	}

	namespace AtrousWaveletTransformFilter {
		namespace RootSignatureLayout {
			enum {
				ECB_AtrousFilter = 0,
				ESI_TemporalAOCoefficient,
				ESI_NormalDepth,
				ESI_Variance,
				ESI_HitDistance,
				ESI_DepthPartialDerivative,
				ESI_Tspp,
				EUO_TemporalAOCoefficient,
				Count
			};
		}
	}

	namespace DisocclusionBlur {
		namespace RootSignatureLayout {
			enum {
				EC_Consts = 0,
				ESI_Depth,
				ESI_BlurStrength,
				EUIO_AOCoefficient,
				Count
			};
		}

		namespace RootConstantsLayout {
			enum {
				ETextureDim_X = 0,
				ETextureDim_Y,
				EStep,
				Count
			};
		}
	}

	namespace AOResources {
		enum {
			EAmbientCoefficient = 0,
			ERayHitDistance,
			Count
		};

		namespace Descriptors {
			enum {
				ES_AmbientCoefficient = 0,
				EU_AmbientCoefficient,
				ES_RayHitDistance,
				EU_RayHitDistance,
				Count
			};
		}
	}

	namespace TemporalCaches {
		enum {
			ETspp = 0,
			ERayHitDistance,
			ECoefficientSquaredMean,
			Count
		};

		namespace Descriptors {
			enum {
				ES_Tspp = 0,
				EU_Tspp,
				ES_RayHitDistance,
				EU_RayHitDistance,
				ES_CoefficientSquaredMean,
				EU_CoefficientSquaredMean,
				Count
			};
		}
	}

	namespace LocalMeanVarianceResources {
		enum {
			ERaw = 0,
			ESmoothed,
			Count
		};

		namespace Descriptors {
			enum {
				ES_Raw = 0,
				EU_Raw,
				ES_Smoothed,
				EU_Smoothed,
				Count
			};
		}
	}

	namespace AOVarianceResources {
		enum {
			ERaw = 0,
			ESmoothed,
			Count
		};

		namespace Descriptors {
			enum {
				ES_Raw = 0,
				EU_Raw,
				ES_Smoothed,
				EU_Smoothed,
				Count
			};
		}
	}

	namespace TemporalAOCoefficients {
		namespace Descriptors {
			enum {
				Srv = 0,
				Uav,
				Count
			};
		}
	}
	
	using AOResourcesType = std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, AOResources::Count>;
	using AOResourcesCpuDescriptors = std::array<CD3DX12_CPU_DESCRIPTOR_HANDLE, AOResources::Descriptors::Count>;
	using AOResourcesGpuDescriptors = std::array<CD3DX12_GPU_DESCRIPTOR_HANDLE, AOResources::Descriptors::Count>;
	
	using TemporalCachesType = std::array<std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, TemporalCaches::Count>, 2>;
	using TemporalCachesCpuDescriptors = std::array<std::array<CD3DX12_CPU_DESCRIPTOR_HANDLE, TemporalCaches::Descriptors::Count>, 2>;
	using TemporalCachesGpuDescriptors = std::array<std::array<CD3DX12_GPU_DESCRIPTOR_HANDLE, TemporalCaches::Descriptors::Count>, 2>;

	using LocalMeanVarianceResourcesType = std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, LocalMeanVarianceResources::Count>;
	using LocalMeanVarianceResourcesCpuDescriptors = std::array<CD3DX12_CPU_DESCRIPTOR_HANDLE, LocalMeanVarianceResources::Descriptors::Count>;
	using LocalMeanVarianceResourcesGpuDescriptors = std::array<CD3DX12_GPU_DESCRIPTOR_HANDLE, LocalMeanVarianceResources::Descriptors::Count>;
	
	using AOVarianceResourcesType = std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, AOVarianceResources::Count>;
	using AOVarianceResourcesCpuDescriptors = std::array<CD3DX12_CPU_DESCRIPTOR_HANDLE, AOVarianceResources::Descriptors::Count>;
	using AOVarianceResourcesGpuDescriptors = std::array<CD3DX12_GPU_DESCRIPTOR_HANDLE, AOVarianceResources::Descriptors::Count>;

	using TemporalAOCoefficientsType = std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, 2>;
	using TemporalAOCoefficientsCpuDescriptors = std::array<std::array<CD3DX12_CPU_DESCRIPTOR_HANDLE, TemporalAOCoefficients::Descriptors::Count>, 2>;
	using TemporalAOCoefficientsGpuDescriptors = std::array<std::array<CD3DX12_GPU_DESCRIPTOR_HANDLE, TemporalAOCoefficients::Descriptors::Count>, 2>;

	const float AmbientMapClearValues[1] = { 1.0f };

	const DXGI_FORMAT AOCoefficientMapFormat							= DXGI_FORMAT_R16_FLOAT;
	const DXGI_FORMAT NormalDepthMapFormat								= DXGI_FORMAT_R8G8B8A8_SNORM;
	const DXGI_FORMAT DepthPartialDerivativeMapFormat					= DXGI_FORMAT_R16G16_FLOAT;
	const DXGI_FORMAT TsppCoefficientSquaredMeanRayHitDistanceFormat	= DXGI_FORMAT_R16G16B16A16_UINT;
	const DXGI_FORMAT DisocclusionBlurStrengthMapFormat					= DXGI_FORMAT_R8_UNORM;
	const DXGI_FORMAT TsppMapFormat										= DXGI_FORMAT_R8_UINT;
	const DXGI_FORMAT CoefficientSquaredMeanMapFormat					= DXGI_FORMAT_R16_FLOAT;
	const DXGI_FORMAT RayHitDistanceFormat								= DXGI_FORMAT_R16_FLOAT;
	const DXGI_FORMAT LocalMeanVarianceMapFormat						= DXGI_FORMAT_R16G16_FLOAT;
	const DXGI_FORMAT VarianceMapFormat									= DXGI_FORMAT_R16_FLOAT;

	class RtaoClass {
	public:
		RtaoClass() = default;
		virtual ~RtaoClass() = default;

	public:
		bool Initialize(ID3D12Device5*const device, ID3D12GraphicsCommandList*const cmdList, ShaderManager*const manager, UINT width, UINT height);
		bool CompileShaders(const std::wstring& filePath);
		bool BuildRootSignatures(const StaticSamplers& samplers);
		bool BuildPSO();
		bool BuildDXRPSO();
		bool BuildShaderTables();
		void RunCalculatingAmbientOcclusion(
			ID3D12GraphicsCommandList4*const cmdList,
			D3D12_GPU_VIRTUAL_ADDRESS accelStruct,
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress,
			D3D12_GPU_DESCRIPTOR_HANDLE si_normal,
			D3D12_GPU_DESCRIPTOR_HANDLE si_depth,
			D3D12_GPU_DESCRIPTOR_HANDLE uo_aoCoefficient,
			D3D12_GPU_DESCRIPTOR_HANDLE uo_rayHitDistance);
		void RunCalculatingDepthPartialDerivative(
			ID3D12GraphicsCommandList4*const cmdList,
			D3D12_GPU_DESCRIPTOR_HANDLE i_depth,
			D3D12_GPU_DESCRIPTOR_HANDLE o_depthPartialDerivative,
			UINT width, UINT height);
		void RunCalculatingLocalMeanVariance(
			ID3D12GraphicsCommandList4*const cmdList,
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress,
			D3D12_GPU_DESCRIPTOR_HANDLE si_aoCoefficient,
			D3D12_GPU_DESCRIPTOR_HANDLE uo_localMeanVariance,
			UINT width, UINT height,
			bool checkerboardSamplingEnabled);
		void FillInCheckerboard(
			ID3D12GraphicsCommandList4*const cmdList,
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress,
			D3D12_GPU_DESCRIPTOR_HANDLE uio_localMeanVariance);
		void ReverseReprojectPreviousFrame(
			ID3D12GraphicsCommandList4*const cmdList,
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress,
			D3D12_GPU_DESCRIPTOR_HANDLE si_normalDepth,
			D3D12_GPU_DESCRIPTOR_HANDLE si_depthPartialDerivative,
			D3D12_GPU_DESCRIPTOR_HANDLE si_reprojNormalDepth,
			D3D12_GPU_DESCRIPTOR_HANDLE si_cachedNormalDepth,
			D3D12_GPU_DESCRIPTOR_HANDLE si_velocity,
			D3D12_GPU_DESCRIPTOR_HANDLE si_cachedAOCoefficient,
			D3D12_GPU_DESCRIPTOR_HANDLE si_cachedTspp,
			D3D12_GPU_DESCRIPTOR_HANDLE si_cachedAOCoefficientSquaredMean,
			D3D12_GPU_DESCRIPTOR_HANDLE si_cachedRayHitDistance,
			D3D12_GPU_DESCRIPTOR_HANDLE uo_cachedTspp,
			D3D12_GPU_DESCRIPTOR_HANDLE uo_tsppCoefficientSquaredMeanRayHitDistance);
		void BlendWithCurrentFrame(
			ID3D12GraphicsCommandList4*const cmdList,
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress,
			D3D12_GPU_DESCRIPTOR_HANDLE si_aoCoefficient,
			D3D12_GPU_DESCRIPTOR_HANDLE si_localMeanVariance,
			D3D12_GPU_DESCRIPTOR_HANDLE si_rayHitDistance,
			D3D12_GPU_DESCRIPTOR_HANDLE si_tsppCoefficientSquaredMeanRayHitDistance,
			D3D12_GPU_DESCRIPTOR_HANDLE uio_temporalAOCoefficient,
			D3D12_GPU_DESCRIPTOR_HANDLE uio_tspp,
			D3D12_GPU_DESCRIPTOR_HANDLE uio_coefficientSquaredMean,
			D3D12_GPU_DESCRIPTOR_HANDLE uio_rayHitDistance,
			D3D12_GPU_DESCRIPTOR_HANDLE uo_variance,
			D3D12_GPU_DESCRIPTOR_HANDLE uo_blurStrength);
		void ApplyAtrousWaveletTransformFilter(
			ID3D12GraphicsCommandList4*const cmdList,
			D3D12_GPU_VIRTUAL_ADDRESS cbAddress,
			D3D12_GPU_DESCRIPTOR_HANDLE si_temporalAOCoefficient,
			D3D12_GPU_DESCRIPTOR_HANDLE si_normalDepth,
			D3D12_GPU_DESCRIPTOR_HANDLE si_variance,
			D3D12_GPU_DESCRIPTOR_HANDLE si_hitDistance,
			D3D12_GPU_DESCRIPTOR_HANDLE si_depthPartialDerivative,
			D3D12_GPU_DESCRIPTOR_HANDLE si_tspp,
			D3D12_GPU_DESCRIPTOR_HANDLE uo_temporalAOCoefficient);
		void BlurDisocclusion(
			ID3D12GraphicsCommandList4*const cmdList,
			ID3D12Resource* aoCoefficient,
			D3D12_GPU_DESCRIPTOR_HANDLE si_depth,
			D3D12_GPU_DESCRIPTOR_HANDLE si_blurStrength,
			D3D12_GPU_DESCRIPTOR_HANDLE uio_aoCoefficient,
			UINT width, UINT height,
			UINT lowTsppBlurPasses);

		__forceinline constexpr UINT Width() const;
		__forceinline constexpr UINT Height() const;

		__forceinline const AOResourcesType& AOResources() const;
		__forceinline const AOResourcesGpuDescriptors& AOResourcesGpuDescriptors() const;

		__forceinline const TemporalCachesType& TemporalCaches() const;
		__forceinline const TemporalCachesGpuDescriptors& TemporalCachesGpuDescriptors() const;

		__forceinline const LocalMeanVarianceResourcesType& LocalMeanVarianceResources() const;
		__forceinline const LocalMeanVarianceResourcesGpuDescriptors& LocalMeanVarianceResourcesGpuDescriptors() const;

		__forceinline const AOVarianceResourcesType& AOVarianceResources() const;
		__forceinline const AOVarianceResourcesGpuDescriptors& AOVarianceResourcesGpuDescriptors() const;

		__forceinline ID3D12Resource* PrevFrameNormalDepth();
		__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE PrevFrameNormalDepthSrv() const;

		__forceinline ID3D12Resource* TsppCoefficientSquaredMeanRayHitDistance();
		__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE TsppCoefficientSquaredMeanRayHitDistanceSrv() const;
		__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE TsppCoefficientSquaredMeanRayHitDistanceUav() const;

		__forceinline ID3D12Resource* DisocclusionBlurStrengthResource();
		__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE DisocclusionBlurStrengthSrv() const;
		__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE DisocclusionBlurStrengthUav() const;

		__forceinline const TemporalAOCoefficientsType& TemporalAOCoefficients();
		__forceinline const TemporalAOCoefficientsGpuDescriptors& TemporalAOCoefficientsGpuDescriptors() const;

		__forceinline ID3D12Resource* DepthPartialDerivativeMapResource();
		__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE DepthPartialDerivativeSrv() const;
		__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE DepthPartialDerivativeUav() const;

		__forceinline constexpr UINT TemporalCurrentFrameResourceIndex() const;
		UINT MoveToNextFrame();

		__forceinline constexpr UINT TemporalCurrentFrameTemporalAOCoefficientResourceIndex() const;
		UINT MoveToNextFrameTemporalAOCoefficient();

		void BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE& hCpu, CD3DX12_GPU_DESCRIPTOR_HANDLE& hGpu, UINT descSize);

		bool OnResize(ID3D12GraphicsCommandList* cmdList, UINT width, UINT height);

		void Transite(ID3D12GraphicsCommandList* cmdList, bool srvToUav);
		void Switch();

	private:
		void BuildDescriptors();
		bool BuildResource(ID3D12GraphicsCommandList* cmdList);

	public:

		static const float AmbientMapClearValues[1];

	private:
		ID3D12Device5* md3dDevice;
		ShaderManager* mShaderManager;

		std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12RootSignature>> mRootSignatures;
		std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12PipelineState>> mPSOs;
		Microsoft::WRL::ComPtr<ID3D12StateObject> mDXRPSO;
		Microsoft::WRL::ComPtr<ID3D12StateObjectProperties> mDXRPSOProp;

		std::unordered_map<std::string, Microsoft::WRL::ComPtr<ID3D12Resource>> mShaderTables;

		UINT mWidth;
		UINT mHeight;

		Rtao::AOResourcesType mAOResources;
		Rtao::AOResourcesCpuDescriptors mhAOResourcesCpus;
		Rtao::AOResourcesGpuDescriptors mhAOResourcesGpus;

		Rtao::TemporalCachesType mTemporalCaches;
		Rtao::TemporalCachesCpuDescriptors mhTemporalCachesCpus;
		Rtao::TemporalCachesGpuDescriptors mhTemporalCachesGpus;

		Rtao::LocalMeanVarianceResourcesType mLocalMeanVarianceResources;
		Rtao::LocalMeanVarianceResourcesCpuDescriptors mhLocalMeanVarianceResourcesCpus;
		Rtao::LocalMeanVarianceResourcesGpuDescriptors mhLocalMeanVarianceResourcesGpus;

		Rtao::AOVarianceResourcesType mAOVarianceResources;
		Rtao::AOVarianceResourcesCpuDescriptors mhAOVarianceResourcesCpus;
		Rtao::AOVarianceResourcesGpuDescriptors mhAOVarianceResourcesGpus;

		Microsoft::WRL::ComPtr<ID3D12Resource> mPrevFrameNormalDepth;
		Microsoft::WRL::ComPtr<ID3D12Resource> mPrevFrameNormalDepthUploadBuffer;
		CD3DX12_CPU_DESCRIPTOR_HANDLE mhPrevFrameNormalDepthCpuSrv;
		CD3DX12_GPU_DESCRIPTOR_HANDLE mhPrevFrameNormalDepthGpuSrv;

		Microsoft::WRL::ComPtr<ID3D12Resource> mTsppCoefficientSquaredMeanRayHitDistance;
		CD3DX12_CPU_DESCRIPTOR_HANDLE mhTsppCoefficientSquaredMeanRayHitDistanceCpuSrv;
		CD3DX12_GPU_DESCRIPTOR_HANDLE mhTsppCoefficientSquaredMeanRayHitDistanceGpuSrv;
		CD3DX12_CPU_DESCRIPTOR_HANDLE mhTsppCoefficientSquaredMeanRayHitDistanceCpuUav;
		CD3DX12_GPU_DESCRIPTOR_HANDLE mhTsppCoefficientSquaredMeanRayHitDistanceGpuUav;

		Microsoft::WRL::ComPtr<ID3D12Resource> mDisocclusionBlurStrength;
		CD3DX12_CPU_DESCRIPTOR_HANDLE mhDisocclusionBlurStrengthCpuSrv;
		CD3DX12_GPU_DESCRIPTOR_HANDLE mhDisocclusionBlurStrengthGpuSrv;
		CD3DX12_CPU_DESCRIPTOR_HANDLE mhDisocclusionBlurStrengthCpuUav;
		CD3DX12_GPU_DESCRIPTOR_HANDLE mhDisocclusionBlurStrengthGpuUav;

		Rtao::TemporalAOCoefficientsType mTemporalAOCoefficients;
		Rtao::TemporalAOCoefficientsCpuDescriptors mhTemporalAOCoefficientsCpus;
		Rtao::TemporalAOCoefficientsGpuDescriptors mhTemporalAOCoefficientsGpus;

		Microsoft::WRL::ComPtr<ID3D12Resource> mDepthPartialDerivative;
		CD3DX12_CPU_DESCRIPTOR_HANDLE mhDepthPartialDerivativeCpuSrv;
		CD3DX12_GPU_DESCRIPTOR_HANDLE mhDepthPartialDerivativeGpuSrv;
		CD3DX12_CPU_DESCRIPTOR_HANDLE mhDepthPartialDerivativeCpuUav;
		CD3DX12_GPU_DESCRIPTOR_HANDLE mhDepthPartialDerivativeGpuUav;

		bool bSwitch;
		bool bResourceState;

		UINT mTemporalCurrentFrameResourceIndex;
		UINT mTemporalCurrentFrameTemporalAOCeofficientResourceIndex;
	};
}	

constexpr UINT Rtao::RtaoClass::Width() const {
	return mWidth;
}

constexpr UINT Rtao::RtaoClass::Height() const {
	return mHeight;
}

const Rtao::AOResourcesType& Rtao::RtaoClass::AOResources() const {
	return mAOResources;
}
const Rtao::AOResourcesGpuDescriptors& Rtao::RtaoClass::AOResourcesGpuDescriptors() const {
	return mhAOResourcesGpus;
}

const Rtao::TemporalCachesType& Rtao::RtaoClass::TemporalCaches() const {
	return mTemporalCaches;
}
const Rtao::TemporalCachesGpuDescriptors& Rtao::RtaoClass::TemporalCachesGpuDescriptors() const {
	return mhTemporalCachesGpus;
}

const Rtao::LocalMeanVarianceResourcesType& Rtao::RtaoClass::LocalMeanVarianceResources() const {
	return mLocalMeanVarianceResources;
}
const Rtao::LocalMeanVarianceResourcesGpuDescriptors& Rtao::RtaoClass::LocalMeanVarianceResourcesGpuDescriptors() const {
	return mhLocalMeanVarianceResourcesGpus;
}

const Rtao::AOVarianceResourcesType& Rtao::RtaoClass::AOVarianceResources() const {
	return mAOVarianceResources;
}
const Rtao::AOVarianceResourcesGpuDescriptors& Rtao::RtaoClass::AOVarianceResourcesGpuDescriptors() const {
	return mhAOVarianceResourcesGpus;
}

ID3D12Resource* Rtao::RtaoClass::PrevFrameNormalDepth() {
	return mPrevFrameNormalDepth.Get();
}
constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::RtaoClass::PrevFrameNormalDepthSrv() const {
	return mhPrevFrameNormalDepthGpuSrv;
}

ID3D12Resource* Rtao::RtaoClass::TsppCoefficientSquaredMeanRayHitDistance() {
	return mTsppCoefficientSquaredMeanRayHitDistance.Get();
}
constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::RtaoClass::TsppCoefficientSquaredMeanRayHitDistanceSrv() const {
	return mhTsppCoefficientSquaredMeanRayHitDistanceGpuSrv;
}
constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::RtaoClass::TsppCoefficientSquaredMeanRayHitDistanceUav() const {
	return mhTsppCoefficientSquaredMeanRayHitDistanceGpuUav;
}

ID3D12Resource* Rtao::RtaoClass::DisocclusionBlurStrengthResource() {
	return mDisocclusionBlurStrength.Get();
}
constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::RtaoClass::DisocclusionBlurStrengthSrv() const {
	return mhDisocclusionBlurStrengthGpuSrv;
}
constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::RtaoClass::DisocclusionBlurStrengthUav() const {
	return mhDisocclusionBlurStrengthGpuUav;
}

const Rtao::TemporalAOCoefficientsType& Rtao::RtaoClass::TemporalAOCoefficients() {
	return mTemporalAOCoefficients;
}
const Rtao::TemporalAOCoefficientsGpuDescriptors& Rtao::RtaoClass::TemporalAOCoefficientsGpuDescriptors() const {
	return mhTemporalAOCoefficientsGpus;
}

ID3D12Resource* Rtao::RtaoClass::DepthPartialDerivativeMapResource() {
	return mDepthPartialDerivative.Get();
}
constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::RtaoClass::DepthPartialDerivativeSrv() const {
	return mhDepthPartialDerivativeGpuSrv;
}
constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::RtaoClass::DepthPartialDerivativeUav() const {
	return mhDepthPartialDerivativeGpuUav;
}

constexpr UINT Rtao::RtaoClass::TemporalCurrentFrameResourceIndex() const {
	return mTemporalCurrentFrameResourceIndex;
}

constexpr UINT Rtao::RtaoClass::TemporalCurrentFrameTemporalAOCoefficientResourceIndex() const {
	return mTemporalCurrentFrameTemporalAOCeofficientResourceIndex;
}