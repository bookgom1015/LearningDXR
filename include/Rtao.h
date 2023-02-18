#pragma once

#include <d3dx12.h>

#include "MathHelper.h"

class Rtao {
public:
	Rtao();
	virtual ~Rtao();

public:
	bool Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, UINT width, UINT height);

	__forceinline constexpr UINT Width() const;
	__forceinline constexpr UINT Height() const;

	__forceinline ID3D12Resource* AmbientMap0Resource();
	__forceinline ID3D12Resource* AmbientMap1Resource();
	__forceinline ID3D12Resource* AccumulationMapResource();

	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE AmbientMap0Srv() const;
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE AmbientMap0Uav() const;
	
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE AmbientMap1Srv() const;
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE AmbientMap1Uav() const;

	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE AccumulationMapUav() const;

	void BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
		CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuUav,
		CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuUav,
		UINT descSize);

	bool OnResize(UINT width, UINT height, ID3D12GraphicsCommandList* cmdList);

private:
	void BuildDescriptors();
	bool BuildResource(ID3D12GraphicsCommandList* cmdList);
	
public:
	static const DXGI_FORMAT AmbientMapFormat = DXGI_FORMAT_R16_UNORM;
	static const DXGI_FORMAT AccumulationMapFormat = DXGI_FORMAT_R32_UINT;

	static const float AmbientMapClearValues[1];

private:
	ID3D12Device* md3dDevice;

	UINT mWidth;
	UINT mHeight;

	Microsoft::WRL::ComPtr<ID3D12Resource> mAmbientMap0;
	Microsoft::WRL::ComPtr<ID3D12Resource> mAmbientMap1;
	Microsoft::WRL::ComPtr<ID3D12Resource> mAmbientMap1UploadBuffer;
	Microsoft::WRL::ComPtr<ID3D12Resource> mAccumulationMap;
	
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhAmbientMap0CpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhAmbientMap0GpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhAmbientMap0CpuUav;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhAmbientMap0GpuUav;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhAmbientMap1CpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhAmbientMap1GpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhAmbientMap1CpuUav;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhAmbientMap1GpuUav;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhAccumulationMapCpuUav;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhAccumulationMapGpuUav;

	DirectX::XMFLOAT4 mOffsets[14];
};

constexpr UINT Rtao::Width() const {
	return mWidth;
}

constexpr UINT Rtao::Height() const {
	return mHeight;
}

ID3D12Resource* Rtao::AmbientMap0Resource() {
	return mAmbientMap0.Get();
}

ID3D12Resource* Rtao::AmbientMap1Resource() {
	return mAmbientMap1.Get();
}

ID3D12Resource* Rtao::AccumulationMapResource() {
	return mAccumulationMap.Get();
}

constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::AmbientMap0Srv() const {
	return mhAmbientMap0GpuSrv;
}

constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::AmbientMap0Uav() const {
	return mhAmbientMap0GpuUav;
}

constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::AmbientMap1Srv() const {
	return mhAmbientMap1GpuSrv;
}

constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::AmbientMap1Uav() const {
	return mhAmbientMap1GpuUav;
}

constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Rtao::AccumulationMapUav() const {
	return mhAccumulationMapGpuUav;
}