#pragma once

#include <d3dx12.h>
#include <array>

#include "MathHelper.h"

namespace RaytracedAO {
	namespace AOResources {
		enum {
			EAmbientCoefficient = 0,
			//ERayHitDistance,
			Count
		};

		namespace Descriptors {
			enum {
				ES_AmbientCoefficient = 0,
				EU_AmbientCoefficient,
				//ES_RayHitDistance,
				//EU_RayHitDistance,
				Count
			};
		}
	}

	namespace TemporalCaches {
		enum {
			ETemporalSupersampling = 0,
			Count
		};

		namespace Descriptors {
			enum {
				ES_TemporalSupersampling = 0,
				EU_TemporalSupersampling,
				Count
			};
		}
	}
	
	using AOResourcesType = std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, AOResources::Count>;
	using TemporalCachesType = std::array<std::array<Microsoft::WRL::ComPtr<ID3D12Resource>, TemporalCaches::Count>, 2>;

	using AOResourcesCpuDescriptors = std::array<CD3DX12_CPU_DESCRIPTOR_HANDLE, RaytracedAO::AOResources::Descriptors::Count>;
	using AOResourcesGpuDescriptors = std::array<CD3DX12_GPU_DESCRIPTOR_HANDLE, RaytracedAO::AOResources::Descriptors::Count>;
	using TemporalCachesCpuDescriptors = std::array<std::array<CD3DX12_CPU_DESCRIPTOR_HANDLE, RaytracedAO::TemporalCaches::Descriptors::Count>, 2>;
	using TemporalCachesGpuDescriptors = std::array<std::array<CD3DX12_GPU_DESCRIPTOR_HANDLE, RaytracedAO::TemporalCaches::Descriptors::Count>, 2>;
}

class Rtao {
public:
	Rtao();
	virtual ~Rtao();

public:
	bool Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, UINT width, UINT height);

	__forceinline constexpr UINT Width() const;
	__forceinline constexpr UINT Height() const;

	__forceinline const RaytracedAO::AOResourcesType& AOResources() const;
	__forceinline const RaytracedAO::AOResourcesGpuDescriptors& AOResourcesGpuDescriptors() const;

	__forceinline const RaytracedAO::TemporalCachesType& TemporalCaches() const;
	__forceinline const RaytracedAO::TemporalCachesGpuDescriptors& TemporalCachesGpuDescriptors() const;
	
	__forceinline ID3D12Resource* CachedNormalDepthMapResource();
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE CachedNormalDepthMapSrv() const;

	__forceinline ID3D12Resource* LinearDepthDerivativesMapResource();
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE LinearDepthDerivativesMapUav() const;

	__forceinline ID3D12Resource* TsppCoefficientSquaredMeanRayHitDistanceMapResource();
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE TsppCoefficientSquaredMeanRayHitDistanceMapUav() const;

	__forceinline ID3D12Resource* DisocclusionBlurStrengthMapResource();
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE DisocclusionBlurStrengthMapSrv() const;
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE DisocclusionBlurStrengthMapUav() const;

	__forceinline ID3D12Resource* TemporalAOCoefficientMapResource();
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE TemporalAOCoefficientMapUav() const;

	__forceinline ID3D12Resource* CachedTemporalAOCoefficientMapResource();
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE CachedTemporalAOCoefficientMapSrv() const;

	__forceinline ID3D12Resource* CoefficientSquaredMeanMapResource();
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE CoefficientSquaredMeanMapUav() const;

	__forceinline ID3D12Resource* CachedCoefficientSquaredMeanMapResource();
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE CachedCoefficientSquaredMeanMapSrv() const;

	__forceinline ID3D12Resource* RayHitDistanceMapResource();
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE RayHitDistanceMapUav() const;

	__forceinline ID3D12Resource* CachedRayHitDistanceMapResource();
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE CachedRayHitDistanceMapSrv() const;

	__forceinline ID3D12Resource* RawLocalMeanVarianceMapResource();
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE RawLocalMeanVarianceMapSrv() const;
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE RawLocalMeanVarianceMapUav() const;

	__forceinline ID3D12Resource* SmoothedLocalMeanVarianceMapResource();
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE SmoothedLocalMeanVarianceMapSrv() const;
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE SmoothedLocalMeanVarianceMapUav() const;

	__forceinline ID3D12Resource* RawVarianceMapResource();
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE RawVarianceMapSrv() const;
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE RawVarianceMapUav() const;

	__forceinline ID3D12Resource* SmoothedVarianceMapResource();
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE SmoothedVarianceMapSrv() const;
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE SmoothedVarianceMapUav() const;

	void BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE& hCpu, CD3DX12_GPU_DESCRIPTOR_HANDLE& hGpu, UINT descSize);

	bool OnResize(ID3D12GraphicsCommandList* cmdList, UINT width, UINT height);

	void Transite(ID3D12GraphicsCommandList* cmdList, bool srvToUav);
	void Switch();

private:
	void BuildDescriptors();
	bool BuildResource(ID3D12GraphicsCommandList* cmdList);
	
public:
	static const DXGI_FORMAT AmbientMapFormat = DXGI_FORMAT_R16_UNORM;
	static const DXGI_FORMAT NormalDepthMapFormat = DXGI_FORMAT_R8G8B8A8_SNORM;
	static const DXGI_FORMAT LinearDepthDerivativesMapFormat = DXGI_FORMAT_R16G16_FLOAT;
	static const DXGI_FORMAT TsppCoefficientSquaredMeanRayHitDistanceFormat = DXGI_FORMAT_R16G16B16A16_UINT;
	static const DXGI_FORMAT DisocclusionBlurStrengthMapFormat = DXGI_FORMAT_R8_UNORM;
	static const DXGI_FORMAT TemporalSuperSamplingMapFormat = DXGI_FORMAT_R8_UINT;
	static const DXGI_FORMAT TemporalAOCoefficientMapFormat = DXGI_FORMAT_R16_FLOAT;
	static const DXGI_FORMAT CoefficientSquaredMeanMapFormat = DXGI_FORMAT_R16_FLOAT;
	static const DXGI_FORMAT RayHitDistanceMapFormat = DXGI_FORMAT_R16_FLOAT;
	static const DXGI_FORMAT LocalMeanVarianceMapFormat = DXGI_FORMAT_R16G16_FLOAT;
	static const DXGI_FORMAT VarianceMapFormat = DXGI_FORMAT_R16_FLOAT;

	static const float AmbientMapClearValues[1];

private:
	ID3D12Device* md3dDevice;

	UINT mWidth;
	UINT mHeight;

	RaytracedAO::AOResourcesType mAOResources;
	RaytracedAO::AOResourcesCpuDescriptors mhAOResourcesCpus;
	RaytracedAO::AOResourcesGpuDescriptors mhAOResourcesGpus;

	RaytracedAO::TemporalCachesType mTemporalCaches;
	RaytracedAO::TemporalCachesCpuDescriptors mhTemporalCachesCpus;
	RaytracedAO::TemporalCachesGpuDescriptors mhTemporalCachesGpus;

	Microsoft::WRL::ComPtr<ID3D12Resource> mCachedNormalDepthMap;
	Microsoft::WRL::ComPtr<ID3D12Resource> mCachedNormalDepthMapUploadBuffer;

	Microsoft::WRL::ComPtr<ID3D12Resource> mLinearDepthDerivativesMap;

	Microsoft::WRL::ComPtr<ID3D12Resource> mTsppCoefficientSquaredMeanRayHitDistanceMap;

	Microsoft::WRL::ComPtr<ID3D12Resource> mDisocclusionBlurStrengthMap;

	Microsoft::WRL::ComPtr<ID3D12Resource> mTemporalAOCoefficientMaps[2];
	
	Microsoft::WRL::ComPtr<ID3D12Resource> mCoefficientSquaredMeanMaps[2];

	Microsoft::WRL::ComPtr<ID3D12Resource> mRayHitDistanceMaps[2];

	Microsoft::WRL::ComPtr<ID3D12Resource> mLocalMeanVarianceMaps[2];

	Microsoft::WRL::ComPtr<ID3D12Resource> mVarianceMaps[2];
	
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCachedNormalDepthMapCpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhCachedNormalDepthMapGpuSrv;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhLinearDepthDerivativesMapCpuUav;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhLinearDepthDerivativesMapGpuUav;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhTsppCoefficientSquaredMeanRayHitDistanceMapCpuUav;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhTsppCoefficientSquaredMeanRayHitDistanceMapGpuUav;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhDisocclusionBlurStrengthMapCpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhDisocclusionBlurStrengthMapGpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhDisocclusionBlurStrengthMapCpuUav;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhDisocclusionBlurStrengthMapGpuUav;
	
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhTemporalAOCoefficientMapCpuSrvs[2];
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhTemporalAOCoefficientMapGpuSrvs[2];
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhTemporalAOCoefficientMapCpuUavs[2];
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhTemporalAOCoefficientMapGpuUavs[2];

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCoefficientSquaredMeanMapCpuSrvs[2];
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhCoefficientSquaredMeanMapGpuSrvs[2];
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCoefficientSquaredMeanMapCpuUavs[2];
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhCoefficientSquaredMeanMapGpuUavs[2];

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhRayHitDistanceMapCpuSrvs[2];
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhRayHitDistanceMapGpuSrvs[2];
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhRayHitDistanceMapCpuUavs[2];
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhRayHitDistanceMapGpuUavs[2];

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhLocalMeanVarianceMapCpuSrvs[2];
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhLocalMeanVarianceMapGpuSrvs[2];
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhLocalMeanVarianceMapCpuUavs[2];
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhLocalMeanVarianceMapGpuUavs[2];

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhVarianceMapCpuSrvs[2];
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhVarianceMapGpuSrvs[2];
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhVarianceMapCpuUavs[2];
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhVarianceMapGpuUavs[2];

	bool bSwitch;
	bool bResourceState;
};

constexpr UINT Rtao::Width() const {
	return mWidth;
}

constexpr UINT Rtao::Height() const {
	return mHeight;
}

const RaytracedAO::AOResourcesType& Rtao::AOResources() const {
	return mAOResources;
}

const RaytracedAO::AOResourcesGpuDescriptors& Rtao::AOResourcesGpuDescriptors() const {
	return mhAOResourcesGpus;
}

const RaytracedAO::TemporalCachesType& Rtao::TemporalCaches() const {
	return mTemporalCaches;
}

const RaytracedAO::TemporalCachesGpuDescriptors& Rtao::TemporalCachesGpuDescriptors() const {
	return mhTemporalCachesGpus;
}

ID3D12Resource* Rtao::CachedNormalDepthMapResource() {
	return mCachedNormalDepthMap.Get();
}
constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::CachedNormalDepthMapSrv() const {
	return mhCachedNormalDepthMapGpuSrv;
}

ID3D12Resource* Rtao::LinearDepthDerivativesMapResource() {
	return mLinearDepthDerivativesMap.Get();
}
constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::LinearDepthDerivativesMapUav() const {
	return mhLinearDepthDerivativesMapGpuUav;
}

ID3D12Resource* Rtao::TsppCoefficientSquaredMeanRayHitDistanceMapResource() {
	return mTsppCoefficientSquaredMeanRayHitDistanceMap.Get();
}
constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::TsppCoefficientSquaredMeanRayHitDistanceMapUav() const {
	return mhTsppCoefficientSquaredMeanRayHitDistanceMapGpuUav;
}

ID3D12Resource* Rtao::DisocclusionBlurStrengthMapResource() {
	return mDisocclusionBlurStrengthMap.Get();
}
constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::DisocclusionBlurStrengthMapSrv() const {
	return mhDisocclusionBlurStrengthMapGpuSrv;
}
constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::DisocclusionBlurStrengthMapUav() const {
	return mhDisocclusionBlurStrengthMapGpuUav;
}

ID3D12Resource* Rtao::TemporalAOCoefficientMapResource() {
	return mTemporalAOCoefficientMaps[bSwitch].Get();
}
constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::TemporalAOCoefficientMapUav() const {
	return mhTemporalAOCoefficientMapGpuUavs[bSwitch];
}

ID3D12Resource* Rtao::CachedTemporalAOCoefficientMapResource() {
	return mTemporalAOCoefficientMaps[!bSwitch].Get();
}
constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::CachedTemporalAOCoefficientMapSrv() const {
	return mhTemporalAOCoefficientMapGpuSrvs[!bSwitch];
}

ID3D12Resource* Rtao::CoefficientSquaredMeanMapResource() {
	return mCoefficientSquaredMeanMaps[bSwitch].Get();
}
constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::CoefficientSquaredMeanMapUav() const {
	return mhCoefficientSquaredMeanMapGpuUavs[bSwitch];
}

ID3D12Resource* Rtao::CachedCoefficientSquaredMeanMapResource() {
	return mCoefficientSquaredMeanMaps[!bSwitch].Get();
}
constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::CachedCoefficientSquaredMeanMapSrv() const {
	return mhCoefficientSquaredMeanMapGpuSrvs[!bSwitch];
}

ID3D12Resource* Rtao::RayHitDistanceMapResource() {
	return mRayHitDistanceMaps[bSwitch].Get();
}
constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::RayHitDistanceMapUav() const {
	return mhRayHitDistanceMapGpuUavs[bSwitch];
}

ID3D12Resource* Rtao::CachedRayHitDistanceMapResource() {
	return mRayHitDistanceMaps[!bSwitch].Get();
}
constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::CachedRayHitDistanceMapSrv() const {
	return mhRayHitDistanceMapGpuUavs[!bSwitch];
}

ID3D12Resource* Rtao::RawLocalMeanVarianceMapResource() {
	return mLocalMeanVarianceMaps[0].Get();
}
constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::RawLocalMeanVarianceMapSrv() const {
	return mhLocalMeanVarianceMapGpuSrvs[0];
}

constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::RawLocalMeanVarianceMapUav() const {
	return mhLocalMeanVarianceMapGpuUavs[0];
}

ID3D12Resource* Rtao::SmoothedLocalMeanVarianceMapResource() {
	return mLocalMeanVarianceMaps[1].Get();
}
constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::SmoothedLocalMeanVarianceMapSrv() const {
	return mhLocalMeanVarianceMapGpuSrvs[1];
}
constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::SmoothedLocalMeanVarianceMapUav() const {
	return mhLocalMeanVarianceMapGpuUavs[1];
}

ID3D12Resource* Rtao::RawVarianceMapResource() {
	return mVarianceMaps[0].Get();
}
constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::RawVarianceMapSrv() const {
	return mhVarianceMapGpuSrvs[0];
}
constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::RawVarianceMapUav() const {
	return mhVarianceMapGpuUavs[0];
}

ID3D12Resource* Rtao::SmoothedVarianceMapResource() {
	return mVarianceMaps[1].Get();
}
constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::SmoothedVarianceMapSrv() const {
	return mhVarianceMapGpuSrvs[1];
}
constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::SmoothedVarianceMapUav() const {
	return mhVarianceMapGpuUavs[1];
}