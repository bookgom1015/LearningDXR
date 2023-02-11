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

	__forceinline ID3D12Resource* ShadowMapResource();
	__forceinline ID3D12Resource* ShadowBlurMapResource();

	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE ShadowMapSrv() const;
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE ShadowMapUav() const;

	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE ShadowBlurMapUav() const;

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
	const static DXGI_FORMAT ShadowMapFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

private:
	ID3D12Device* md3dDevice;

	UINT mWidth;
	UINT mHeight;

	Microsoft::WRL::ComPtr<ID3D12Resource> mShadowMap = nullptr;
	Microsoft::WRL::ComPtr<ID3D12Resource> mShadowBlurMap = nullptr;
	
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuUav;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuUav;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhBlurCpuUav;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhBlurGpuUav;
};

constexpr UINT DxrShadowMap::Width() const {
	return mWidth;
}

constexpr UINT DxrShadowMap::Height() const {
	return mHeight;
}

ID3D12Resource* DxrShadowMap::ShadowMapResource() {
	return mShadowMap.Get();
}

ID3D12Resource* DxrShadowMap::ShadowBlurMapResource() {
	return mShadowBlurMap.Get();
}

constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE DxrShadowMap::ShadowMapSrv() const {
	return mhGpuSrv;
}

constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE DxrShadowMap::ShadowMapUav() const {
	return mhGpuUav;
}

constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE DxrShadowMap::ShadowBlurMapUav() const {
	return mhBlurGpuUav;
}