#include "DxrShadowMap.h"
#include "Logger.h"

DxrShadowMap::DxrShadowMap() {}

DxrShadowMap::~DxrShadowMap() {}

bool DxrShadowMap::Initialize(ID3D12Device* device, UINT width, UINT height) {
	md3dDevice = device;

	mWidth = width;
	mHeight = height;

	CheckIsValid(BuildResource());

	return true;
}

void DxrShadowMap::BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv, CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuUav, CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuUav,
		UINT descSize) {
	mhCpuSrv0 = hCpuSrv;
	mhGpuSrv0 = hGpuSrv;
	mhCpuUav0 = hCpuUav;
	mhGpuUav0 = hGpuUav;

	mhCpuSrv1 = hCpuSrv.Offset(1, descSize);
	mhGpuSrv1 = hGpuSrv.Offset(1, descSize);
	mhCpuUav1 = hCpuUav.Offset(1, descSize);
	mhGpuUav1 = hGpuUav.Offset(1, descSize);

	BuildDescriptors();
}

bool DxrShadowMap::OnResize(UINT width, UINT height) {
	if ((mWidth != width) || (mHeight != height)) {
		mWidth = width;
		mHeight = height;

		CheckIsValid(BuildResource());
	}

	return true;
}

void DxrShadowMap::BuildDescriptors() {
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = ShadowMapFormat;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	srvDesc.Texture2D.MipLevels = 1;

	md3dDevice->CreateShaderResourceView(mShadowMap0.Get(), &srvDesc, mhCpuSrv0);
	md3dDevice->CreateShaderResourceView(mShadowMap1.Get(), &srvDesc, mhCpuSrv1);

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Format = ShadowMapFormat;

	md3dDevice->CreateUnorderedAccessView(mShadowMap0.Get(), nullptr, &uavDesc, mhCpuUav0);
	md3dDevice->CreateUnorderedAccessView(mShadowMap1.Get(), nullptr, &uavDesc, mhCpuUav1);
}

bool DxrShadowMap::BuildResource() {
	D3D12_RESOURCE_DESC texDesc = {};
	texDesc.DepthOrArraySize = 1;
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Format = ShadowMapFormat;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS | D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	texDesc.Width = mWidth;
	texDesc.Height = mHeight;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.MipLevels = 1;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;

	CheckHResult(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&mShadowMap0)
	));
	CheckHResult(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_COMMON,
		nullptr,
		IID_PPV_ARGS(&mShadowMap1)
	));

	return true;
}