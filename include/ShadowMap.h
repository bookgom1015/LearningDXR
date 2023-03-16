#pragma once

#include <d3dx12.h>

class ShadowMap {
public:
	ShadowMap();
	virtual ~ShadowMap();

public:
	bool Initialize(ID3D12Device* device, UINT width, UINT height);

	__forceinline constexpr UINT Width() const;
	__forceinline constexpr UINT Height() const;

	__forceinline constexpr D3D12_VIEWPORT Viewport() const;
	__forceinline constexpr D3D12_RECT ScissorRect() const;

	ID3D12Resource* Resource();
	__forceinline constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE Srv() const;
	__forceinline constexpr CD3DX12_CPU_DESCRIPTOR_HANDLE Dsv() const;

	void BuildDescriptors(CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv,
		CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuDsv);

private:
	void BuildDescriptors();
	bool BuildResource();

public:
	static const UINT NumDepthStenciles = 1;

protected:
	const DXGI_FORMAT ShadowMapFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

private:
	ID3D12Device* md3dDevice;

	UINT mWidth;
	UINT mHeight;

	D3D12_VIEWPORT mViewport;
	D3D12_RECT mScissorRect;

	Microsoft::WRL::ComPtr<ID3D12Resource> mShadowMap = nullptr;

	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuSrv;
	CD3DX12_GPU_DESCRIPTOR_HANDLE mhGpuSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE mhCpuDsv;
};

constexpr UINT ShadowMap::Width() const {
	return mWidth;
}

constexpr UINT ShadowMap::Height() const {
	return mHeight;
}

constexpr D3D12_VIEWPORT ShadowMap::Viewport() const {
	return mViewport;
}

constexpr D3D12_RECT ShadowMap::ScissorRect() const {
	return mScissorRect;
}

constexpr CD3DX12_GPU_DESCRIPTOR_HANDLE ShadowMap::Srv() const {
	return mhGpuSrv;
}

constexpr CD3DX12_CPU_DESCRIPTOR_HANDLE ShadowMap::Dsv() const {
	return mhCpuDsv;
}