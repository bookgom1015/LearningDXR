#include "GaussianFilterCS.h"
#include "Logger.h"
#include "ShaderManager.h"
#include "D3D12Util.h"
#include "ShadingHelpers.h"

#include <DirectXMath.h>
#include "HlslCompaction.h"

using namespace GaussianFilterCS;

bool GaussianFilterCSClass::CompileShaders(ShaderManager*const manager, const std::wstring& filePath) {
	{
		const auto path = filePath + L"GaussianBlurCS.hlsl";
		{
			auto shaderInfo = D3D12ShaderInfo(path.c_str(), L"HorzBlurCS", L"cs_6_3");
			std::string name = "horzGaussianBlurCS";
			CheckIsValid(manager->CompileShader(shaderInfo, name));
			mShaders[Filter::Type::R8G8B8A8][Direction::Horizontal] = manager->GetDxcShader(name);
		}
		{
			std::string name = "vertGaussianBlurCS";
			auto shaderInfo = D3D12ShaderInfo(path.c_str(), L"VertBlurCS", L"cs_6_3");
			CheckIsValid(manager->CompileShader(shaderInfo, name));
			mShaders[Filter::Type::R8G8B8A8][Direction::Vertical] = manager->GetDxcShader(name);
		}
	}
	{
		const auto path = filePath + L"GaussianBlurCS.hlsl";
		{
			auto shaderInfo = D3D12ShaderInfo(path.c_str(), L"HorzBlurCS", L"cs_6_3");
			std::string name = "horzGaussianBlurR16CS";
			CheckIsValid(manager->CompileShader(shaderInfo, name));
			mShaders[Filter::Type::R16][Direction::Horizontal] = manager->GetDxcShader(name);
		}
		{
			auto shaderInfo = D3D12ShaderInfo(path.c_str(), L"VertBlurCS", L"cs_6_3");
			std::string name = "vertGaussianBlurR16CS";
			CheckIsValid(manager->CompileShader(shaderInfo, name));
			mShaders[Filter::Type::R16][Direction::Vertical] = manager->GetDxcShader(name);
		}
	}

	return true;
}

bool GaussianFilterCSClass::BuildRootSignature(ID3D12Device*const device, const StaticSamplers& samplers) {
	CD3DX12_ROOT_PARAMETER slotRootParameter[RootSignatureLayout::Count];

	CD3DX12_DESCRIPTOR_RANGE texTables[3];
	texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);
	texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);
	texTables[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);

	slotRootParameter[RootSignatureLayout::ECB_BlurPass].InitAsConstantBufferView(0);
	slotRootParameter[RootSignatureLayout::EC_Consts].InitAsConstants(RootConstantsLayout::Count, 1, 0);
	slotRootParameter[RootSignatureLayout::ESI_NormalAndDepth].InitAsDescriptorTable(1, &texTables[0]);
	slotRootParameter[RootSignatureLayout::ESI_Input].InitAsDescriptorTable(1, &texTables[1]);
	slotRootParameter[RootSignatureLayout::EUO_Output].InitAsDescriptorTable(1, &texTables[2]);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
		_countof(slotRootParameter), slotRootParameter,
		static_cast<UINT>(samplers.size()), samplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	);
	CheckIsValid(D3D12Util::CreateRootSignature(device, rootSigDesc, &mRootSignature));

	return true;
}

bool GaussianFilterCSClass::BuildPso(ID3D12Device*const device, ShaderManager*const manager) {
	D3D12_COMPUTE_PIPELINE_STATE_DESC psoDesc = {};
	psoDesc.pRootSignature = mRootSignature.Get();
	psoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;

	for (UINT i = 0; i < Filter::Type::Count; ++i) {
		for (UINT j = 0; j < Direction::Count; ++j) {
			auto cs = mShaders[(Filter::Type)i][(Direction::Type)j];
			psoDesc.CS = { reinterpret_cast<BYTE*>(cs->GetBufferPointer()), cs->GetBufferSize() };
			CheckHResult(device->CreateComputePipelineState(&psoDesc, IID_PPV_ARGS(&mPSOs[(Filter::Type)i][(Direction::Type)j])));
		}
	}

	return true;
}

void GaussianFilterCSClass::Run(
		ID3D12GraphicsCommandList*const cmdList,
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress,
		D3D12_GPU_DESCRIPTOR_HANDLE normalAndDepthSrv,
		ID3D12Resource*const primary,
		ID3D12Resource*const secondary,
		D3D12_GPU_DESCRIPTOR_HANDLE primarySrv,
		D3D12_GPU_DESCRIPTOR_HANDLE primaryUav,
		D3D12_GPU_DESCRIPTOR_HANDLE secondarySrv,
		D3D12_GPU_DESCRIPTOR_HANDLE secondaryUav,
		Filter::Type type,
		UINT width, UINT height,
		size_t blurCount) {
	cmdList->SetComputeRootSignature(mRootSignature.Get());

	cmdList->SetComputeRootConstantBufferView(RootSignatureLayout::ECB_BlurPass, cbAddress);

	UINT values[RootConstantsLayout::Count] = { width, height };
	cmdList->SetComputeRoot32BitConstants(RootSignatureLayout::EC_Consts, _countof(values), values, 0);

	cmdList->SetComputeRootDescriptorTable(RootSignatureLayout::ESI_NormalAndDepth, normalAndDepthSrv);

	for (int i = 0; i < blurCount; ++i) {
		cmdList->SetPipelineState(mPSOs[type][Direction::Horizontal].Get());

		cmdList->ResourceBarrier(
			1,
			&CD3DX12_RESOURCE_BARRIER::Transition(
				secondary,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS
			)
		);
		D3D12Util::UavBarrier(cmdList, secondary);

		cmdList->SetComputeRootDescriptorTable(RootSignatureLayout::ESI_Input, primarySrv);
		cmdList->SetComputeRootDescriptorTable(RootSignatureLayout::EUO_Output, secondaryUav);

		cmdList->Dispatch(CeilDivide(width, GaussianBlurComputeShaderParams::ThreadGroup::Size), height, 1);
		{
			D3D12_RESOURCE_BARRIER barriers[] = {
				CD3DX12_RESOURCE_BARRIER::Transition(
					secondary,
					D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
				),
				CD3DX12_RESOURCE_BARRIER::Transition(
					primary,
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
					D3D12_RESOURCE_STATE_UNORDERED_ACCESS
				),
			};
			cmdList->ResourceBarrier(
				_countof(barriers),
				barriers
			);
			std::vector<ID3D12Resource*> resources = { secondary, primary };
			D3D12Util::UavBarriers(cmdList, resources.data(), resources.size());
		}

		cmdList->SetPipelineState(mPSOs[type][Direction::Vertical].Get());

		cmdList->SetComputeRootDescriptorTable(RootSignatureLayout::ESI_Input, secondarySrv);
		cmdList->SetComputeRootDescriptorTable(RootSignatureLayout::EUO_Output, primaryUav);

		cmdList->Dispatch(width, CeilDivide(height, GaussianBlurComputeShaderParams::ThreadGroup::Size), 1);
		cmdList->ResourceBarrier(
			1,
			&CD3DX12_RESOURCE_BARRIER::Transition(
				primary,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
			)
		);
		D3D12Util::UavBarrier(cmdList, primary);
	}
}

void GaussianFilterCSClass::Blur(
		ID3D12GraphicsCommandList* cmdList,
		ID3D12Resource*const output,
		D3D12_CPU_DESCRIPTOR_HANDLE outputRtv,
		D3D12_GPU_DESCRIPTOR_HANDLE inputSrv,
		bool horzBlur) {

}