#pragma once

#include <d3dx12.h>
#include <array>
#include <unordered_map>

#include "Samplers.h"

class ShaderManager;

namespace GaussianFilter3x3CS {
	namespace RootSignatureLayout {
		enum {
			EC_Consts = 0,
			ESI_Input,
			EUO_Output,
			Count
		};
	}

	namespace RootConstantsLayout {
		enum {
			EDimensionX = 0,
			EDimensionY,
			EInvDimensionX,
			EInvDimensionY,
			Count
		};
	}

	enum FilterType {
		Filter3x3 = 0,
		Filter3x3RG,
		Count
	};

	class GaussianFilter3x3CSClass {
	public:
		GaussianFilter3x3CSClass() = default;
		virtual ~GaussianFilter3x3CSClass() = default;

	public:
		bool CompileShaders(ShaderManager*const manager, const std::wstring& filePath);
		bool BuildRootSignature(ID3D12Device*const device, const StaticSamplers& samplers);
		bool BuildPso(ID3D12Device*const device, ShaderManager*const manager);
		void Run(
			ID3D12GraphicsCommandList*const cmdList,
			D3D12_GPU_DESCRIPTOR_HANDLE si_input,
			D3D12_GPU_DESCRIPTOR_HANDLE uo_output,
			FilterType type,
			UINT width, UINT height);

	private:
		Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature;
		std::unordered_map<FilterType, Microsoft::WRL::ComPtr<ID3D12PipelineState>> mPSOs;
	};
}