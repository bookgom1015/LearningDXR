#include "GaussianFilter3x3CS.h"
#include "Logger.h"
#include "ShaderManager.h"
#include "D3D12Util.h"
#include "ShadingHelpers.h"

#include <DirectXMath.h>
#include "HlslCompaction.h"

using namespace GaussianFilter3x3CS;

bool GaussianFilter3x3CSClass::CompileShaders(ShaderManager*const manager, const std::wstring& filePath) {
	{
		const auto path = filePath + L"GaussianFilter3x3CS.hlsl";
		auto shaderInfo = D3D12ShaderInfo(path.c_str(), L"CS", L"cs_6_3");
		CheckIsValid(manager->CompileShader(shaderInfo, "gaussianFilter3x3CS"));
	}
	{
		const auto path = filePath + L"GaussianFilterRG3x3CS.hlsl";
		auto shaderInfo = D3D12ShaderInfo(path.c_str(), L"CS", L"cs_6_3");
		CheckIsValid(manager->CompileShader(shaderInfo, "gaussianFilterRG3x3CS"));
	}

	return true;
}

bool GaussianFilter3x3CSClass::BuildRootSignature(ID3D12Device*const device, const StaticSamplers& samplers) {
	CD3DX12_DESCRIPTOR_RANGE texTables[2];
	texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
	texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);

	CD3DX12_ROOT_PARAMETER slotRootParameter[RootSignatureLayout::Count];
	slotRootParameter[RootSignatureLayout::EC_Consts].InitAsConstants(RootConstantsLayout::Count, 0, 0);
	slotRootParameter[RootSignatureLayout::ESI_Input].InitAsDescriptorTable(1, &texTables[0]);
	slotRootParameter[RootSignatureLayout::EUO_Output].InitAsDescriptorTable(1, &texTables[1]);

	CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(
		_countof(slotRootParameter), slotRootParameter,
		static_cast<UINT>(samplers.size()), samplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_NONE
	);
	CheckIsValid(D3D12Util::CreateRootSignature(device, globalRootSignatureDesc, mRootSignature.GetAddressOf()));

	return true;
}

bool GaussianFilter3x3CSClass::BuildPso(ID3D12Device*const device, ShaderManager*const manager) {
	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = mRootSignature.Get();
	psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

	for (UINT i = 0; i < FilterType::Count; ++i) {
		IDxcBlob* cs;
		switch (i) {
		case FilterType::Filter3x3:
			cs = manager->GetDxcShader("gaussianFilter3x3CS");
			break;
		case FilterType::Filter3x3RG:
			cs = manager->GetDxcShader("gaussianFilterRG3x3CS");
			break;
		default:
			ReturnFalse(L"Unknown FilterType");
		}
		psoDesc.CS = { reinterpret_cast<BYTE*>(cs->GetBufferPointer()), cs->GetBufferSize() };
		CheckHResult(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&mPSOs[(FilterType)i])));
	}

	return true;
}

void GaussianFilter3x3CSClass::Run(
		ID3D12GraphicsCommandList*const cmdList,
		D3D12_GPU_DESCRIPTOR_HANDLE si_input,
		D3D12_GPU_DESCRIPTOR_HANDLE uo_output,
		FilterType type,
		UINT width, UINT height) {
	cmdList->SetPipelineState(mPSOs[type].Get());
	cmdList->SetComputeRootSignature(mRootSignature.Get());

	{
		UINT values[2] = { width, height };
		cmdList->SetComputeRoot32BitConstants(RootSignatureLayout::EC_Consts, _countof(values), values, 0);
	}
	{
		float values[2] = { 1.0f / width, 1.0f / height };
		cmdList->SetComputeRoot32BitConstants(RootSignatureLayout::EC_Consts, _countof(values), values, RootConstantsLayout::EInvDimensionX);
	}

	cmdList->SetComputeRootDescriptorTable(RootSignatureLayout::ESI_Input, si_input);
	cmdList->SetComputeRootDescriptorTable(RootSignatureLayout::EUO_Output, uo_output);

	cmdList->Dispatch(CeilDivide(width, DefaultComputeShaderParams::ThreadGroup::Width), CeilDivide(height, DefaultComputeShaderParams::ThreadGroup::Height), 1);
}
