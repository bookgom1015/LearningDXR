#include "GBuffer.h"
#include "Logger.h"
#include "ShaderManager.h"
#include "D3D12Util.h"
#include "RenderItem.h"
#include "Mesh.h"

using namespace GBuffer;

bool GBufferClass::Initialize(ID3D12Device*const device, ShaderManager*const manager, UINT width, UINT height) {
	md3dDevice = device;
	mShaderManager = manager;

	mWidth = width;
	mHeight = height;

	CheckIsValid(BuildResource());

	return true;
}

bool GBufferClass::CompileShaders(const std::wstring& filePath) {
	const auto path = filePath + L"GBuffer.hlsl";
	auto vsInfo = D3D12ShaderInfo(path .c_str(), L"VS", L"vs_6_3");
	auto psInfo = D3D12ShaderInfo(path .c_str(), L"PS", L"ps_6_3");
	CheckIsValid(mShaderManager->CompileShader(vsInfo, "gbufferVS"));
	CheckIsValid(mShaderManager->CompileShader(psInfo, "gbufferPS"));

	return true;
}

bool GBufferClass::BuildRootSignature(const StaticSamplers& samplers) {
	CD3DX12_ROOT_PARAMETER slotRootParameter[RootSignatureLayout::Count];

	slotRootParameter[RootSignatureLayout::ECB_Pass].InitAsConstantBufferView(0);
	slotRootParameter[RootSignatureLayout::EC_Consts].InitAsConstants(RootConstantsLayout::Count, 1);
	slotRootParameter[RootSignatureLayout::ESB_Object].InitAsShaderResourceView(0, 1);
	slotRootParameter[RootSignatureLayout::ESB_Material].InitAsShaderResourceView(0, 2);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
		_countof(slotRootParameter), slotRootParameter,
		static_cast<UINT>(samplers.size()), samplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	);
	CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice, rootSigDesc, &mRootSignature));

	return true;
}

bool GBufferClass::BuildPso(D3D12_INPUT_LAYOUT_DESC inputLayout, DXGI_FORMAT depthFormat) {
	D3D12_GRAPHICS_PIPELINE_STATE_DESC gbufferPsoDesc;
	ZeroMemory(&gbufferPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	gbufferPsoDesc.InputLayout = inputLayout;
	gbufferPsoDesc.pRootSignature = mRootSignature.Get();
	{
		auto vs = mShaderManager->GetDxcShader("gbufferVS");
		auto ps = mShaderManager->GetDxcShader("gbufferPS");
		gbufferPsoDesc.VS = {
			reinterpret_cast<BYTE*>(vs->GetBufferPointer()),
			vs->GetBufferSize()
		};
		gbufferPsoDesc.PS = {
			reinterpret_cast<BYTE*>(ps->GetBufferPointer()),
			ps->GetBufferSize()
		};
	}
	gbufferPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	gbufferPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	gbufferPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	gbufferPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	gbufferPsoDesc.NumRenderTargets = Resources::Count;
	gbufferPsoDesc.RTVFormats[0] = GBuffer::ColorFormat;
	gbufferPsoDesc.RTVFormats[1] = GBuffer::AlbedoFormat;
	gbufferPsoDesc.RTVFormats[2] = GBuffer::NormalDepthFormat;
	gbufferPsoDesc.RTVFormats[3] = GBuffer::SpecularFormat;
	gbufferPsoDesc.RTVFormats[4] = GBuffer::VelocityFormat;
	gbufferPsoDesc.RTVFormats[5] = GBuffer::ReprojectedNormalDepthFormat;
	gbufferPsoDesc.SampleMask = UINT_MAX;
	gbufferPsoDesc.SampleDesc.Count = 1;
	gbufferPsoDesc.SampleDesc.Quality = 0;
	gbufferPsoDesc.DSVFormat = depthFormat;
	CheckHResult(md3dDevice->CreateGraphicsPipelineState(&gbufferPsoDesc, IID_PPV_ARGS(&mPSO)));

	return true;
}

void GBufferClass::Run(
		ID3D12GraphicsCommandList*const cmdList, 
		D3D12_CPU_DESCRIPTOR_HANDLE dsv,
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress,
		D3D12_GPU_VIRTUAL_ADDRESS objSBAddress,
		D3D12_GPU_VIRTUAL_ADDRESS matSBAddress,
		const std::vector<RenderItem*>& ritems) {
	cmdList->SetPipelineState(mPSO.Get());
	cmdList->SetGraphicsRootSignature(mRootSignature.Get());

	const auto colorRtv = mhResourcesCpuDescriptors[GBuffer::Resources::Descriptors::ER_Color];
	const auto albedoRtv = mhResourcesCpuDescriptors[GBuffer::Resources::Descriptors::ER_Albedo];
	const auto normalDepthRtv = mhResourcesCpuDescriptors[GBuffer::Resources::Descriptors::ER_NormalDepth];
	const auto specularRtv = mhResourcesCpuDescriptors[GBuffer::Resources::Descriptors::ER_Specular];
	const auto velocityRtv = mhResourcesCpuDescriptors[GBuffer::Resources::Descriptors::ER_Velocity];
	const auto reprojNormalDepthRtv = mhResourcesCpuDescriptors[GBuffer::Resources::Descriptors::ER_ReprojectedNormalDepth];

	cmdList->ClearRenderTargetView(colorRtv, GBuffer::ColorMapClearValues, 0, nullptr);
	cmdList->ClearRenderTargetView(albedoRtv, GBuffer::AlbedoMapClearValues, 0, nullptr);
	cmdList->ClearRenderTargetView(normalDepthRtv, GBuffer::NormalDepthMapClearValues, 0, nullptr);
	cmdList->ClearRenderTargetView(specularRtv, GBuffer::SpecularMapClearValues, 0, nullptr);
	cmdList->ClearRenderTargetView(velocityRtv, GBuffer::VelocityMapClearValues, 0, nullptr);
	cmdList->ClearRenderTargetView(reprojNormalDepthRtv, GBuffer::ReprojectedNormalDepthMapClearValues, 0, nullptr);
	std::array<D3D12_CPU_DESCRIPTOR_HANDLE, GBuffer::Resources::Count> renderTargets = {
		colorRtv, albedoRtv, normalDepthRtv, specularRtv, velocityRtv, reprojNormalDepthRtv
	};

	cmdList->OMSetRenderTargets(static_cast<UINT>(renderTargets.size()), renderTargets.data(), true, &dsv);
	cmdList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	cmdList->SetGraphicsRootConstantBufferView(RootSignatureLayout::ECB_Pass, cbAddress);
	cmdList->SetGraphicsRootShaderResourceView(RootSignatureLayout::ESB_Object, objSBAddress);
	cmdList->SetGraphicsRootShaderResourceView(RootSignatureLayout::ESB_Material, matSBAddress);

	DrawRenderItems(cmdList, ritems);
}

void GBufferClass::BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE& hCpuSrv,
		CD3DX12_GPU_DESCRIPTOR_HANDLE& hGpuSrv,
		CD3DX12_CPU_DESCRIPTOR_HANDLE& hCpuRtv,
		UINT descSize, UINT rtvDescSize,
		ID3D12Resource* depth) {
	mhResourcesCpuDescriptors[Resources::Descriptors::ES_Color] = hCpuSrv;
	mhResourcesGpuDescriptors[Resources::Descriptors::ES_Color] = hGpuSrv;
	mhResourcesCpuDescriptors[Resources::Descriptors::ER_Color] = hCpuRtv;

	mhResourcesCpuDescriptors[Resources::Descriptors::ES_Albedo] = hCpuSrv.Offset(1, descSize);
	mhResourcesGpuDescriptors[Resources::Descriptors::ES_Albedo] = hGpuSrv.Offset(1, descSize);
	mhResourcesCpuDescriptors[Resources::Descriptors::ER_Albedo] = hCpuRtv.Offset(1, rtvDescSize);

	mhResourcesCpuDescriptors[Resources::Descriptors::ES_NormalDepth] = hCpuSrv.Offset(1, descSize);
	mhResourcesGpuDescriptors[Resources::Descriptors::ES_NormalDepth] = hGpuSrv.Offset(1, descSize);
	mhResourcesCpuDescriptors[Resources::Descriptors::ER_NormalDepth] = hCpuRtv.Offset(1, rtvDescSize);

	mhResourcesCpuDescriptors[Resources::Descriptors::ES_Depth] = hCpuSrv.Offset(1, descSize);
	mhResourcesGpuDescriptors[Resources::Descriptors::ES_Depth] = hGpuSrv.Offset(1, descSize);

	mhResourcesCpuDescriptors[Resources::Descriptors::ES_Specular] = hCpuSrv.Offset(1, descSize);
	mhResourcesGpuDescriptors[Resources::Descriptors::ES_Specular] = hGpuSrv.Offset(1, descSize);
	mhResourcesCpuDescriptors[Resources::Descriptors::ER_Specular] = hCpuRtv.Offset(1, rtvDescSize);

	mhResourcesCpuDescriptors[Resources::Descriptors::ES_Velocity] = hCpuSrv.Offset(1, descSize);
	mhResourcesGpuDescriptors[Resources::Descriptors::ES_Velocity] = hGpuSrv.Offset(1, descSize);
	mhResourcesCpuDescriptors[Resources::Descriptors::ER_Velocity] = hCpuRtv.Offset(1, rtvDescSize);

	mhResourcesCpuDescriptors[Resources::Descriptors::ES_ReprojectedNormalDepth] = hCpuSrv.Offset(1, descSize);
	mhResourcesGpuDescriptors[Resources::Descriptors::ES_ReprojectedNormalDepth] = hGpuSrv.Offset(1, descSize);
	mhResourcesCpuDescriptors[Resources::Descriptors::ER_ReprojectedNormalDepth] = hCpuRtv.Offset(1, rtvDescSize);

	BuildDescriptors(depth);

	hCpuSrv.Offset(1, descSize);
	hGpuSrv.Offset(1, descSize);
	hCpuRtv.Offset(1, rtvDescSize);
}

bool GBufferClass::OnResize(UINT width, UINT height, ID3D12Resource* depth) {
	if ((mWidth != width) || (mHeight != height)) {
		mWidth = width;
		mHeight = height;

		CheckIsValid(BuildResource());
		BuildDescriptors(depth);
	}

	return true;
}

void GBufferClass::BuildDescriptors(ID3D12Resource* depth) {
	D3D12_SHADER_RESOURCE_VIEW_DESC srvDesc = {};
	srvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;
	srvDesc.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
	srvDesc.Texture2D.MostDetailedMip = 0;
	srvDesc.Texture2D.ResourceMinLODClamp = 0.0f;
	srvDesc.Texture2D.MipLevels = 1;

	D3D12_RENDER_TARGET_VIEW_DESC rtvDesc = {};
	rtvDesc.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
	rtvDesc.Texture2D.MipSlice = 0;
	rtvDesc.Texture2D.PlaneSlice = 0;

	{
		srvDesc.Format = ColorFormat;
		rtvDesc.Format = ColorFormat;

		auto pColor = mResources[Resources::EColor].Get();
		md3dDevice->CreateShaderResourceView(pColor, &srvDesc, mhResourcesCpuDescriptors[Resources::Descriptors::ES_Color]);
		md3dDevice->CreateRenderTargetView(pColor, &rtvDesc, mhResourcesCpuDescriptors[Resources::Descriptors::ER_Color]);

		auto pAlbedo = mResources[Resources::EAlbedo].Get();
		md3dDevice->CreateShaderResourceView(pAlbedo, &srvDesc, mhResourcesCpuDescriptors[Resources::Descriptors::ES_Albedo]);
		md3dDevice->CreateRenderTargetView(pAlbedo, &rtvDesc, mhResourcesCpuDescriptors[Resources::Descriptors::ER_Albedo]);

		auto pSpecular = mResources[Resources::ESpecular].Get();
		md3dDevice->CreateShaderResourceView(pSpecular, &srvDesc, mhResourcesCpuDescriptors[Resources::Descriptors::ES_Specular]);
		md3dDevice->CreateRenderTargetView(pSpecular, &rtvDesc, mhResourcesCpuDescriptors[Resources::Descriptors::ER_Specular]);
	}
	{
		srvDesc.Format = NormalDepthFormat;
		rtvDesc.Format = NormalDepthFormat;

		auto pNormalDepth = mResources[Resources::ENormalDepth].Get();
		md3dDevice->CreateShaderResourceView(pNormalDepth, &srvDesc, mhResourcesCpuDescriptors[Resources::Descriptors::ES_NormalDepth]);
		md3dDevice->CreateRenderTargetView(pNormalDepth, &rtvDesc, mhResourcesCpuDescriptors[Resources::Descriptors::ER_NormalDepth]);
	}
	{
		srvDesc.Format = DepthFormat;
		md3dDevice->CreateShaderResourceView(depth, &srvDesc, mhResourcesCpuDescriptors[Resources::Descriptors::ES_Depth]);
	}
	{
		srvDesc.Format = VelocityFormat;
		rtvDesc.Format = VelocityFormat;

		auto pVelocity = mResources[Resources::EVelocity].Get();
		md3dDevice->CreateShaderResourceView(pVelocity, &srvDesc, mhResourcesCpuDescriptors[Resources::Descriptors::ES_Velocity]);
		md3dDevice->CreateRenderTargetView(pVelocity, &rtvDesc, mhResourcesCpuDescriptors[Resources::Descriptors::ER_Velocity]);
	}
	{
		srvDesc.Format = ReprojectedNormalDepthFormat;
		rtvDesc.Format = ReprojectedNormalDepthFormat;

		auto pReproj = mResources[Resources::EReprojectedNormalDepth].Get();
		md3dDevice->CreateShaderResourceView(pReproj, &srvDesc, mhResourcesCpuDescriptors[Resources::Descriptors::ES_ReprojectedNormalDepth]);
		md3dDevice->CreateRenderTargetView(pReproj, &rtvDesc, mhResourcesCpuDescriptors[Resources::Descriptors::ER_ReprojectedNormalDepth]);
	}
}

bool GBufferClass::BuildResource() {
	D3D12_RESOURCE_DESC rscDesc = {};
	rscDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	rscDesc.Alignment = 0;
	rscDesc.Width = mWidth;
	rscDesc.Height = mHeight;
	rscDesc.DepthOrArraySize = 1;
	rscDesc.MipLevels = 1;
	rscDesc.SampleDesc.Count = 1;
	rscDesc.SampleDesc.Quality = 0;
	rscDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	rscDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;

	{
		rscDesc.Format = ColorFormat;

		CD3DX12_CLEAR_VALUE optClear(ColorFormat, ColorMapClearValues);

		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&rscDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&optClear,
			IID_PPV_ARGS(&mResources[Resources::EColor])
		));
		mResources[Resources::EColor]->SetName(L"ColorMap");
	}
	{
		rscDesc.Format = AlbedoFormat;

		CD3DX12_CLEAR_VALUE optClear(AlbedoFormat, AlbedoMapClearValues);

		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&rscDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&optClear,
			IID_PPV_ARGS(&mResources[Resources::EAlbedo])
		));
		mResources[Resources::EAlbedo]->SetName(L"AlbedoMap");
	}
	{
		rscDesc.Format = NormalDepthFormat;

		CD3DX12_CLEAR_VALUE optClear = CD3DX12_CLEAR_VALUE(NormalDepthFormat, NormalDepthMapClearValues);

		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&rscDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&optClear,
			IID_PPV_ARGS(&mResources[Resources::ENormalDepth])
		));
		mResources[Resources::ENormalDepth]->SetName(L"NormalDepthMap");
	}
	{
		rscDesc.Format = SpecularFormat;

		CD3DX12_CLEAR_VALUE optClear = CD3DX12_CLEAR_VALUE(SpecularFormat, SpecularMapClearValues);

		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&rscDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&optClear,
			IID_PPV_ARGS(&mResources[Resources::ESpecular])
		));
		mResources[Resources::ESpecular]->SetName(L"SpecularMap");
	}
	{
		rscDesc.Format = VelocityFormat;

		CD3DX12_CLEAR_VALUE optClear = CD3DX12_CLEAR_VALUE(VelocityFormat, VelocityMapClearValues);

		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&rscDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&optClear,
			IID_PPV_ARGS(&mResources[Resources::EVelocity])
		));
		mResources[Resources::EVelocity]->SetName(L"VelocityMap");
	}
	{
		rscDesc.Format = ReprojectedNormalDepthFormat;

		CD3DX12_CLEAR_VALUE optClear = CD3DX12_CLEAR_VALUE(ReprojectedNormalDepthFormat, ReprojectedNormalDepthMapClearValues);

		CheckHResult(md3dDevice->CreateCommittedResource(
			&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
			D3D12_HEAP_FLAG_NONE,
			&rscDesc,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			&optClear,
			IID_PPV_ARGS(&mResources[Resources::EReprojectedNormalDepth])
		));
		mResources[Resources::EReprojectedNormalDepth]->SetName(L"ReprojectedNormalDepthMap");
	}

	return true;
}

void GBufferClass::DrawRenderItems(ID3D12GraphicsCommandList* cmdList, const std::vector<RenderItem*>& ritems) {
	for (size_t i = 0, end = ritems.size(); i < end; ++i) {
		auto& ri = ritems[i];

		cmdList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		cmdList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		cmdList->IASetPrimitiveTopology(ri->PrimitiveType);

		cmdList->SetGraphicsRoot32BitConstant(RootSignatureLayout::EC_Consts, ri->ObjSBIndex, RootConstantsLayout::EInstanceID);

		cmdList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}
}