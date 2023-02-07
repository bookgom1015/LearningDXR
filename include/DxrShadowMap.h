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

	__forceinline ID3D12Resource* Resource();
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Srv() const;
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Uav() const;

	void BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
		CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuUav, 
		CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuUav
	);

	bool OnResize(UINT width, UINT height);

private:
	void BuildDescriptors();
	bool BuildResource();
	
protected:
	const DXGI_FORMAT ShadowMapFormat = DXGI_FORMAT_R8G8B8A8_UNORM;

private:
	ID3D12Device* md3dDevice;

	UINT mWidth;
	UINT mHeight;

	Microsoft::WRL::ComPtr<ID3D12Resource> mShadowMap = nullptr;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuSrv;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuUav;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuUav;
};

constexpr UINT DxrShadowMap::Width() const {
	return mWidth;
}

constexpr UINT DxrShadowMap::Height() const {
	return mHeight;
}

ID3D12Resource* DxrShadowMap::Resource() {
	return mShadowMap.Get();
}

constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE DxrShadowMap::Srv() const {
	return mhGpuSrv;
}

constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE DxrShadowMap::Uav() const {
	return mhGpuUav;
}
