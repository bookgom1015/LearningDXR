#pragma once

#include <d3dx12.h>

class DxrShadowMap {
public:
	DxrShadowMap();
	virtual ~DxrShadowMap();

public:
	bool Initialize(ID3D12Device* device, UINT width, UINT height);

	__forceinline constexpr UINT Width() const;
	__forceinline constexpr UINT Height() const;

	__forceinline ID3D12Resource* ShadowMap0Resource();
	__forceinline ID3D12Resource* ShadowMap1Resource();

	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE ShadowMap0Srv() const;
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE ShadowMap0Uav() const;

	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE ShadowMap1Srv() const;
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE ShadowMap1Uav() const;

	void BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
		CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuUav, 
		CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuUav,
		UINT descSize
	);

	bool OnResize(UINT width, UINT height);

private:
	void BuildDescriptors();
	bool BuildResource();
	
public:
	const static DXGI_FORMAT ShadowMapFormat = DXGI_FORMAT_R16_UNORM;

private:
	ID3D12Device* md3dDevice;

	UINT mWidth;
	UINT mHeight;

	Microsoft::WRL::ComPtr<ID3D12Resource> mShadowMap0 = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> mShadowMap1 = nullptr;
	
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuSrv0;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuSrv0;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuUav0;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuUav0;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuSrv1;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuSrv1;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuUav1;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuUav1;
};

constexpr UINT DxrShadowMap::Width() const {
	return mWidth;
}

constexpr UINT DxrShadowMap::Height() const {
	return mHeight;
}

ID3D12Resource* DxrShadowMap::ShadowMap0Resource() {
	return mShadowMap0.Get();
}

ID3D12Resource* DxrShadowMap::ShadowMap1Resource() {
	return mShadowMap1.Get();
}

constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE DxrShadowMap::ShadowMap0Srv() const {
	return mhGpuSrv0;
}

constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE DxrShadowMap::ShadowMap0Uav() const {
	return mhGpuUav0;
}

constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE DxrShadowMap::ShadowMap1Srv() const {
	return mhGpuSrv1;
}

constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE DxrShadowMap::ShadowMap1Uav() const {
	return mhGpuUav1;
}