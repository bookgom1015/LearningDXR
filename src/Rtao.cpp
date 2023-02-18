#include "Rtao.h"
#include "Logger.h"

#include <DirectXColors.h>

using namespace DirectX;
using namespace DirectX::PackedVector;

const float Rtao::AmbientMapClearValues[1] = { 1.0f };

Rtao::Rtao() {}

Rtao::~Rtao() {}

bool Rtao::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList* cmdList, UINT width, UINT height) {
	md3dDevice = device;

	mWidth = width;
	mHeight = height;

	CheckIsValid(BuildResource(cmdList));

	return true;
}

void Rtao::BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuSrv, CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuSrv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE hCpuUav, CD3DX12_GPU_DESCRIPTOR_HANDLE hGpuUav, 
		UINT descSize) {
	mhAmbientMap0CpuSrv = hCpuSrv;
	mhAmbientMap0GpuSrv = hGpuSrv;
	mhAmbientMap0CpuUav = hCpuUav;
	mhAmbientMap0GpuUav = hGpuUav;

	mhAmbientMap1CpuSrv = hCpuSrv.Offset(1, descSize);
	mhAmbientMap1GpuSrv = hGpuSrv.Offset(1, descSize);
	mhAmbientMap1CpuUav = hCpuUav.Offset(1, descSize);
	mhAmbientMap1GpuUav = hGpuUav.Offset(1, descSize);

	mhAccumulationMapCpuUav = hCpuUav.Offset(1, descSize);
	mhAccumulationMapGpuUav = hGpuUav.Offset(1, descSize);

	BuildDescriptors();
}

bool Rtao::OnResize(UINT width, UINT height, ID3D12GraphicsCommandList* cmdList) {
	if ((mWidth != width) || (mHeight != height)) {
		mWidth = width;
		mHeight = height;

		CheckIsValid(BuildResource(cmdList));
		BuildDescriptors();
	}

	return true;
}

void Rtao::BuildDescriptors() {
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Format = AmbientMapFormat;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	srvDesc.Texture2D.MipLevels = 1;

	md3dDevice->CreateShaderResourceView(mAmbientMap0.Get(), &srvDesc, mhAmbientMap0CpuSrv);
	md3dDevice->CreateShaderResourceView(mAmbientMap1.Get(), &srvDesc, mhAmbientMap1CpuSrv);

	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Format = AmbientMapFormat;
	md3dDevice->CreateUnorderedAccessView(mAmbientMap0.Get(), nullptr, &uavDesc, mhAmbientMap0CpuUav);
	md3dDevice->CreateUnorderedAccessView(mAmbientMap1.Get(), nullptr, &uavDesc, mhAmbientMap1CpuUav);

	uavDesc.Format = AccumulationMapFormat;
	md3dDevice->CreateUnorderedAccessView(mAccumulationMap.Get(), nullptr, &uavDesc, mhAccumulationMapCpuUav);
}

bool Rtao::BuildResource(ID3D12GraphicsCommandList* cmdList) {
	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	// Ambient occlusion maps are at half resolution.
	texDesc.Width = mWidth;
	texDesc.Height = mHeight;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	
	{
		texDesc.Format = AmbientMapFormat;
		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&mAmbientMap0)
		));
		mAmbientMap0->SetName(L"DxrAmbientMap0");

		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&mAmbientMap1)
		));
		mAmbientMap1->SetName(L"DxrAmbientMap1");
	}
	{
		texDesc.Format = AccumulationMapFormat;
		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&texDesc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&mAccumulationMap)
		));
		mAccumulationMap->SetName(L"DxrAccumulationMap");
	}

	const UINT num2DSubresources = texDesc.DepthOrArraySize * texDesc.MipLevels;
	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(mAmbientMap1.Get(), 0, num2DSubresources);

	CheckHResult(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
		D3D12_RESOURCE_STATE_COPY_SOURCE,
		nullptr,
		IID_PPV_ARGS(mAmbientMap1UploadBuffer.GetAddressOf())
	));

	std::vector<XMCOLOR> initData(mWidth * mHeight, XMCOLOR(1.0f, 1.0f, 1.0f, 1.0f));

	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = initData.data();
	subResourceData.RowPitch = mWidth * sizeof(XMCOLOR);
	subResourceData.SlicePitch = subResourceData.RowPitch * mHeight;

	cmdList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			mAmbientMap1.Get(),
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_COPY_DEST
		)
	);
	UpdateSubresources(
		cmdList,
		mAmbientMap1.Get(),
		mAmbientMap1UploadBuffer.Get(),
		0,
		0,
		num2DSubresources,
		&subResourceData
	);
	cmdList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			mAmbientMap1.Get(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_COMMON
		)
	);

	return true;
}