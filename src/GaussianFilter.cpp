#include "GaussianFilter.h"
#include "Logger.h"
#include "ShaderManager.h"
#include "D3D12Util.h"

using namespace GaussianFilter;

bool GaussianFilterClass::CompileShaders(ShaderManager*const manager, const std::wstring& filePath) {
	const auto path = filePath + L"GaussianBlur.hlsl";
	auto vsInfo = D3D12ShaderInfo(path.c_str(), L"VS", L"vs_6_3");
	auto psInfo = D3D12ShaderInfo(path.c_str(), L"PS", L"ps_6_3");
	CheckIsValid(manager->CompileShader(vsInfo, "gaussianBlurVS"));
	CheckIsValid(manager->CompileShader(psInfo, "gaussianBlurPS"));
	return true;
}

bool GaussianFilterClass::BuildRootSignature(ID3D12Device*const device, const StaticSamplers& samplers) {
	CD3DX12_ROOT_PARAMETER slotRootParameter[RootSignatureLayout::Count];

	CD3DX12_DESCRIPTOR_RANGE texTables[2];
	texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);
	texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);

	slotRootParameter[RootSignatureLayout::ECB_BlurPass].InitAsConstantBufferView(0);
	slotRootParameter[RootSignatureLayout::EC_Consts].InitAsConstants(RootConstantsLayout::Count, 1);
	slotRootParameter[RootSignatureLayout::ESI_NormalAndDepth].InitAsDescriptorTable(1, &texTables[0]);
	slotRootParameter[RootSignatureLayout::ESI_Input].InitAsDescriptorTable(1, &texTables[1]);
	
	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
		_countof(slotRootParameter), slotRootParameter,
		static_cast<UINT>(samplers.size()), samplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	);
	CheckIsValid(D3D12Util::CreateRootSignature(device, rootSigDesc, &mRootSignature));

	return true;
}

bool GaussianFilterClass::BuildPso(ID3D12Device*const device, ShaderManager*const manager) {
	D3D12_GRAPHICS_PIPELINE_STATE_DESC quadPsoDesc;
	ZeroMemory(&quadPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	quadPsoDesc.InputLayout = { nullptr, 0 };
	{
		auto vs = manager->GetDxcShader("gaussianBlurVS");
		auto ps = manager->GetDxcShader("gaussianBlurPS");
		quadPsoDesc.VS = {
			reinterpret_cast<BYTE*>(vs->GetBufferPointer()),
			vs->GetBufferSize()
		};
		quadPsoDesc.PS = {
			reinterpret_cast<BYTE*>(ps->GetBufferPointer()),
			ps->GetBufferSize()
		};
	}
	quadPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	quadPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	quadPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	quadPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	quadPsoDesc.NumRenderTargets = 1;
	quadPsoDesc.SampleMask = UINT_MAX;
	quadPsoDesc.SampleDesc.Count = 1;
	quadPsoDesc.SampleDesc.Quality = 0;
	quadPsoDesc.DepthStencilState.DepthEnable = FALSE;
	quadPsoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	quadPsoDesc.pRootSignature = mRootSignature.Get();

	for (UINT i = 0; i < FilterType::Count; ++i) {
		D3D12_GRAPHICS_PIPELINE_STATE_DESC psoDesc = quadPsoDesc;
		switch (i) {
		case FilterType::R8G8B8A8:
			psoDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM;
			break;
		case FilterType::R16:
			psoDesc.RTVFormats[0] = DXGI_FORMAT_R16_UNORM;
			break;
		default:
			ReturnFalse(L"Unknown FilterType");
		}
		CheckHResult(device->CreateGraphicsPipelineState(&psoDesc, IID_PPV_ARGS(&mPSOs[(FilterType)i])));
	}

	return true;
}

void GaussianFilterClass::Run(
		ID3D12GraphicsCommandList*const cmdList,
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress, 
		D3D12_GPU_DESCRIPTOR_HANDLE normalSrv,
		ID3D12Resource*const primary,
		ID3D12Resource*const secondary,
		D3D12_CPU_DESCRIPTOR_HANDLE primaryRtv,
		D3D12_GPU_DESCRIPTOR_HANDLE primarySrv,
		D3D12_CPU_DESCRIPTOR_HANDLE secondaryRtv,
		D3D12_GPU_DESCRIPTOR_HANDLE secondarySrv,
		float rootConstants[2],
		FilterType type,
		size_t blurCount) {
	cmdList->SetPipelineState(mPSOs[type].Get());
	cmdList->SetGraphicsRootSignature(mRootSignature.Get());

	cmdList->SetGraphicsRootConstantBufferView(RootSignatureLayout::ECB_BlurPass, cbAddress);
	cmdList->SetGraphicsRootDescriptorTable(RootSignatureLayout::ESI_NormalAndDepth, normalSrv);

	cmdList->SetGraphicsRoot32BitConstants(
		RootSignatureLayout::EC_Consts,
		2,
		rootConstants,
		RootConstantsLayout::EDotThreshold
	);

	ID3D12Resource* output = nullptr;
	CD3DX12_GPU_DESCRIPTOR_HANDLE inputSrv;
	CD3DX12_CPU_DESCRIPTOR_HANDLE outputRtv;
	for (int i = 0; i < blurCount; ++i) {
		// Ping-pong the two ambient map textures as we apply
		// horizontal and vertical blur passes.
		output = secondary;
		outputRtv = secondaryRtv;
		inputSrv = primarySrv;
		cmdList->SetGraphicsRoot32BitConstant(RootSignatureLayout::EC_Consts, 1, RootConstantsLayout::EHorizontal);
		Blur(cmdList, output, outputRtv, inputSrv, true);

		output = primary;
		outputRtv = primaryRtv;
		inputSrv = secondarySrv;
		cmdList->SetGraphicsRoot32BitConstant(RootSignatureLayout::EC_Consts, 0, RootConstantsLayout::EHorizontal);
		Blur(cmdList, output, outputRtv, inputSrv, false);
	}
}

void GaussianFilterClass::Blur(
		ID3D12GraphicsCommandList* cmdList, 
		ID3D12Resource*const output,
		D3D12_CPU_DESCRIPTOR_HANDLE outputRtv,
		D3D12_GPU_DESCRIPTOR_HANDLE inputSrv, 
		bool horzBlur) {
	cmdList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			output,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_RENDER_TARGET
		)
	);

	cmdList->OMSetRenderTargets(1, &outputRtv, true, nullptr);

	cmdList->SetGraphicsRootDescriptorTable(RootSignatureLayout::ESI_Input, inputSrv);

	cmdList->IASetVertexBuffers(0, 0, nullptr);
	cmdList->IASetIndexBuffer(nullptr);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->DrawInstanced(6, 1, 0, 0);

	cmdList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			output,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		)
	);
}