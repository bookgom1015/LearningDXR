#include "Ssao.h"
#include "Logger.h"
#include "ShaderManager.h"
#include "D3D12Util.h"

#include <DirectXColors.h>

using namespace DirectX;
using namespace DirectX::PackedVector;
using namespace Ssao;


bool SsaoClass::Initialize(ID3D12Device* device, ID3D12GraphicsCommandList*const cmdList, ShaderManager*const manager, UINT width, UINT height, UINT divider) {
	md3dDevice = device;
	mShaderManager = manager;

	mWidth = width / divider;
	mHeight = height / divider;

	mDivider = divider;

	mViewport = { 0.0f, 0.0f, static_cast<float>(mWidth), static_cast<float>(mHeight), 0.0f, 1.0f };
	mScissorRect = { 0, 0, static_cast<int>(mWidth), static_cast<int>(mHeight) };

	CheckIsValid(BuildResource());
	BuildOffsetVectors();
	CheckIsValid(BuildRandomVectorTexture(cmdList));

	return true;
}

bool SsaoClass::CompileShaders(const std::wstring& filePath) {
	const auto path = filePath + L"Ssao.hlsl";
	auto vsInfo = D3D12ShaderInfo(path.c_str(), L"VS", L"vs_6_3");
	auto psInfo = D3D12ShaderInfo(path.c_str(), L"PS", L"ps_6_3");
	CheckIsValid(mShaderManager->CompileShader(vsInfo, "ssaoVS"));
	CheckIsValid(mShaderManager->CompileShader(psInfo, "ssaoPS"));

	return true;
}

bool SsaoClass::BuildRootSignature(const StaticSamplers& samplers) {
	CD3DX12_ROOT_PARAMETER slotRootParameter[RootSignatureLayout::Count];

	CD3DX12_DESCRIPTOR_RANGE texTables[2];
	texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);
	texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);

	slotRootParameter[RootSignatureLayout::ECB_SsaoPass].InitAsConstantBufferView(0);
	slotRootParameter[RootSignatureLayout::ESI_NormalAndDepth].InitAsDescriptorTable(1, &texTables[0]);
	slotRootParameter[RootSignatureLayout::ESI_RandomVector].InitAsDescriptorTable(1, &texTables[1]);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
		_countof(slotRootParameter), slotRootParameter,
		static_cast<UINT>(samplers.size()), samplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	);
	CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice, rootSigDesc, &mRootSignature));

	return true;
}

bool SsaoClass::BuildPso() {
	D3D12_GRAPHICS_PIPELINE_STATE_DESC ssaoPsoDesc;
	ZeroMemory(&ssaoPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	ssaoPsoDesc.InputLayout = { nullptr, 0 };
	ssaoPsoDesc.pRootSignature = mRootSignature.Get();
	{
		auto vs = mShaderManager->GetDxcShader("ssaoVS");
		auto ps = mShaderManager->GetDxcShader("ssaoPS");
		ssaoPsoDesc.VS = {
			reinterpret_cast<BYTE*>(vs->GetBufferPointer()),
			vs->GetBufferSize()
		};
		ssaoPsoDesc.PS = {
			reinterpret_cast<BYTE*>(ps->GetBufferPointer()),
			ps->GetBufferSize()
		};
	}
	ssaoPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	ssaoPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	ssaoPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	ssaoPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	ssaoPsoDesc.NumRenderTargets = 1;
	ssaoPsoDesc.SampleMask = UINT_MAX;
	ssaoPsoDesc.SampleDesc.Count = 1;
	ssaoPsoDesc.SampleDesc.Quality = 0;
	ssaoPsoDesc.RTVFormats[0] = Ssao::AmbientCoefficientFormat;
	ssaoPsoDesc.DepthStencilState.DepthEnable = FALSE;
	ssaoPsoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	CheckHResult(md3dDevice->CreateGraphicsPipelineState(&ssaoPsoDesc, IID_PPV_ARGS(&mPSO)));

	return true;
}

void SsaoClass::Run(
		ID3D12GraphicsCommandList*const cmdList,
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress,
		D3D12_GPU_DESCRIPTOR_HANDLE normalAndDepthSrv) {
	cmdList->SetPipelineState(mPSO.Get());
	cmdList->SetGraphicsRootSignature(mRootSignature.Get());

	cmdList->RSSetViewports(1, &mViewport);
	cmdList->RSSetScissorRects(1, &mScissorRect);

	auto rawAmbientCoefficientRtv = mhResourcesCpuDescriptors[Resources::Descriptors::ER_AmbientCoefficient];
	cmdList->OMSetRenderTargets(1, &rawAmbientCoefficientRtv, true, nullptr);
	
	cmdList->SetGraphicsRootConstantBufferView(RootSignatureLayout::ECB_SsaoPass, cbAddress);
	cmdList->SetGraphicsRootDescriptorTable(RootSignatureLayout::ESI_NormalAndDepth, normalAndDepthSrv);
	cmdList->SetGraphicsRootDescriptorTable(RootSignatureLayout::ESI_RandomVector, mhResourcesGpuDescriptors[Ssao::Resources::Descriptors::ES_RandomVector]);

	cmdList->IASetVertexBuffers(0, 0, nullptr);
	cmdList->IASetIndexBuffer(nullptr);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->DrawInstanced(6, 1, 0, 0);
}


void SsaoClass::GetOffsetVectors(DirectX::XMFLOAT4 offsets[14]) {
	std::copy(&mOffsets[0], &mOffsets[14], &offsets[0]);
}

void SsaoClass::BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE& hCpuSrv,
		CD3DX12_GPU_DESCRIPTOR_HANDLE& hGpuSrv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE& hCpuRtv,
		UINT descSize, UINT rtvDescSize) {
	mhResourcesCpuDescriptors[Resources::Descriptors::ES_AmbientCoefficient] = hCpuSrv;
	mhResourcesGpuDescriptors[Resources::Descriptors::ES_AmbientCoefficient] = hGpuSrv;
	mhResourcesCpuDescriptors[Resources::Descriptors::ER_AmbientCoefficient] = hCpuRtv;
	  
	mhResourcesCpuDescriptors[Resources::Descriptors::ES_Temporary] = hCpuSrv.Offset(1, descSize);
	mhResourcesGpuDescriptors[Resources::Descriptors::ES_Temporary] = hGpuSrv.Offset(1, descSize);
	mhResourcesCpuDescriptors[Resources::Descriptors::ER_Temporary] = hCpuRtv.Offset(1, rtvDescSize);
	  
	mhResourcesCpuDescriptors[Resources::Descriptors::ES_RandomVector] = hCpuSrv.Offset(1, descSize);
	mhResourcesGpuDescriptors[Resources::Descriptors::ES_RandomVector] = hGpuSrv.Offset(1, descSize);

	BuildDescriptors();

	hCpuSrv.Offset(1, descSize);
	hGpuSrv.Offset(1, descSize);
	hCpuRtv.Offset(1, rtvDescSize);
}

bool SsaoClass::OnResize(UINT width, UINT height) {
	width /= mDivider;
	height /= mDivider;
	if ((mWidth != width) || (mHeight != height)) {
		mWidth = width;
		mHeight = height;

		mViewport = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
		mScissorRect = { 0, 0, static_cast<int>(width), static_cast<int>(height) };

		CheckIsValid(BuildResource());
		BuildDescriptors();
	}

	return true;
}

void SsaoClass::BuildDescriptors() {
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.MipLevels = 1;

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;
	rtvDesc.Texture2D.PlaneSlice = 0;

	{
		srvDesc.Format = Ssao::RandomVectorFormat;
		md3dDevice->CreateShaderResourceView(
			mResources[Resources::ERandomVector].Get(), &srvDesc,
			mhResourcesCpuDescriptors[Resources::Descriptors::ES_RandomVector]
		);
	}
	{
		srvDesc.Format = Ssao::AmbientCoefficientFormat;
		rtvDesc.Format = Ssao::AmbientCoefficientFormat;

		auto pRawResource = mResources[Resources::EAmbientCoefficient].Get();
		md3dDevice->CreateShaderResourceView(pRawResource, &srvDesc, mhResourcesCpuDescriptors[Resources::Descriptors::ES_AmbientCoefficient]);
		md3dDevice->CreateRenderTargetView(pRawResource, &rtvDesc, mhResourcesCpuDescriptors[Resources::Descriptors::ER_AmbientCoefficient]);

		auto pSmoothedResource = mResources[Resources::ETemporary].Get();
		md3dDevice->CreateShaderResourceView(pSmoothedResource, &srvDesc, mhResourcesCpuDescriptors[Resources::Descriptors::ES_Temporary]);
		md3dDevice->CreateRenderTargetView(pSmoothedResource, &rtvDesc, mhResourcesCpuDescriptors[Resources::Descriptors::ER_Temporary]);
	}
}

bool SsaoClass::BuildResource() {
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
	texDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
	
	texDesc.Format = Ssao::AmbientCoefficientFormat;
	CD3DX12_CLEAR_VALUE optClear(Ssao::AmbientCoefficientFormat, AmbientMapClearValues);

	CheckHResult(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&optClear,
		IID_PPV_ARGS(&mResources[Resources::EAmbientCoefficient])
	));
	mResources[Resources::EAmbientCoefficient]->SetName(L"AmbientCoefficientMap");

	CheckHResult(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		&optClear,
		IID_PPV_ARGS(&mResources[Resources::ETemporary])
	));
	mResources[Resources::ETemporary]->SetName(L"TemporaryAmbientCoefficientMap");

	return true;
}

void SsaoClass::BuildOffsetVectors() {
	// Start with 14 uniformly distributed vectors.  We choose the 8 corners of the cube
	// and the 6 center points along each cube face.  We always alternate the points on 
	// opposites sides of the cubes.  This way we still get the vectors spread out even
	// if we choose to use less than 14 samples.

	// 8 cube corners
	mOffsets[0] = XMFLOAT4(+1.0f, +1.0f, +1.0f, 0.0f);
	mOffsets[1] = XMFLOAT4(-1.0f, -1.0f, -1.0f, 0.0f);

	mOffsets[2] = XMFLOAT4(-1.0f, +1.0f, +1.0f, 0.0f);
	mOffsets[3] = XMFLOAT4(+1.0f, -1.0f, -1.0f, 0.0f);

	mOffsets[4] = XMFLOAT4(+1.0f, +1.0f, -1.0f, 0.0f);
	mOffsets[5] = XMFLOAT4(-1.0f, -1.0f, +1.0f, 0.0f);

	mOffsets[6] = XMFLOAT4(-1.0f, +1.0f, -1.0f, 0.0f);
	mOffsets[7] = XMFLOAT4(+1.0f, -1.0f, +1.0f, 0.0f);

	// 6 centers of cube faces
	mOffsets[8] = XMFLOAT4(-1.0f, 0.0f, 0.0f, 0.0f);
	mOffsets[9] = XMFLOAT4(+1.0f, 0.0f, 0.0f, 0.0f);

	mOffsets[10] = XMFLOAT4(0.0f, -1.0f, 0.0f, 0.0f);
	mOffsets[11] = XMFLOAT4(0.0f, +1.0f, 0.0f, 0.0f);

	mOffsets[12] = XMFLOAT4(0.0f, 0.0f, -1.0f, 0.0f);
	mOffsets[13] = XMFLOAT4(0.0f, 0.0f, +1.0f, 0.0f);

	for (int i = 0; i < 14; ++i) {
		// Create random lengths in [0.25, 1.0].
		float s = MathHelper::RandF(0.25f, 1.0f);

		XMVECTOR v = s * XMVector4Normalize(XMLoadFloat4(&mOffsets[i]));

		XMStoreFloat4(&mOffsets[i], v);
	}
}

bool SsaoClass::BuildRandomVectorTexture(ID3D12GraphicsCommandList* cmdList) {
	D3D12_RESOURCE_DESC texDesc;
	ZeroMemory(&texDesc, sizeof(D3D12_RESOURCE_DESC));
	texDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	texDesc.Alignment = 0;
	texDesc.Width = 256;
	texDesc.Height = 256;
	texDesc.DepthOrArraySize = 1;
	texDesc.MipLevels = 1;
	texDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	texDesc.SampleDesc.Count = 1;
	texDesc.SampleDesc.Quality = 0;
	texDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	texDesc.Flags = D3D12_RESOURCE_FLAG_NONE;

	CheckHResult(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&texDesc,
		D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
		nullptr,
		IID_PPV_ARGS(&mResources[Resources::ERandomVector])
	));
	auto pResource = mResources[Resources::ERandomVector].Get();
	CheckHResult(pResource->SetName(L"AORandomVectorMap"));

	//
	// In order to copy CPU memory data into our default buffer,
	//  we need to create an intermediate upload heap. 
	//

	const UINT num2DSubresources = texDesc.DepthOrArraySize * texDesc.MipLevels;
	const UINT64 uploadBufferSize = GetRequiredIntermediateSize(pResource, 0, num2DSubresources);

	CheckHResult(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_UPLOAD),
		D3D12_HEAP_FLAG_NONE,
		&CD3DX12_RESOURCE_DESC::Buffer(uploadBufferSize),
		D3D12_RESOURCE_STATE_COPY_SOURCE,
		nullptr,
		IID_PPV_ARGS(mRandomVectorMapUploadBuffer.GetAddressOf())
	));

	XMCOLOR initData[256 * 256];
	for (int i = 0; i < 256; ++i) {
		for (int j = 0; j < 256; ++j) {
			// Random vector in [0,1].  We will decompress in shader to [-1,1].
			XMFLOAT3 v(MathHelper::RandF(), MathHelper::RandF(), MathHelper::RandF());

			initData[i * 256 + j] = XMCOLOR(v.x, v.y, v.z, 0.0f);
		}
	}

	D3D12_SUBRESOURCE_DATA subResourceData = {};
	subResourceData.pData = initData;
	subResourceData.RowPitch = 256 * sizeof(XMCOLOR);
	subResourceData.SlicePitch = subResourceData.RowPitch * 256;

	//
	// Schedule to copy the data to the default resource, and change states.
	// Note that mCurrSol is put in the GENERIC_READ state so it can be 
	// read by a shader.
	//

	cmdList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			pResource,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_COPY_DEST
		)
	);
	UpdateSubresources(
		cmdList,
		pResource,
		mRandomVectorMapUploadBuffer.Get(),
		0,
		0,
		num2DSubresources,
		&subResourceData
	);
	cmdList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			pResource,
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		)
	);

	return true;
}