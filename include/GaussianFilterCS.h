#pragma once

#include <memory>
#include <string>
#include <array>
#include <unordered_map>
#include <d3dx12.h>

#include "Samplers.h"

struct IDxcBlob;

class ShaderManager;

namespace GaussianFilterCS {
	namespace RootSignatureLayout {
		enum {
			ECB_BlurPass = 0,
			EC_Consts,
			ESI_NormalAndDepth,
			ESI_Input,
			EUO_Output,
			Count
		};
	}

	namespace RootConstantsLayout {
		enum {
			EDimensionX = 0,
			EDimensionY,
			Count
		};
	}

	namespace Filter {
		enum Type {
			R8G8B8A8 = 0,
			R16,
			Count
		};
	}

	namespace Direction {
		enum Type {
			Horizontal = 0,
			Vertical,
			Count
		};
	}

	class GaussianFilterCSClass {
	public:
		GaussianFilterCSClass() = default;
		virtual ~GaussianFilterCSClass() = default;

	public:
		bool CompileShaders(ShaderManager*const manager, const std::wstring& filePath);
		bool BuildRootSignature(ID3D12Device*const device, const StaticSamplers& samplers);
		bool BuildPso(ID3D12Device*const device, ShaderManager*const manager);
		void Run(
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
			size_t blurCount = 3);

	private:
		void Blur(
			ID3D12GraphicsCommandList* cmdList,
			ID3D12Resource*const output,
			D3D12_CPU_DESCRIPTOR_HANDLE outputRtv,
			D3D12_GPU_DESCRIPTOR_HANDLE inputSrv,
			bool horzBlur);

	private:
		Microsoft::WRL::ComPtr<ID3D12RootSignature> mRootSignature;
		std::unordered_map<Filter::Type, std::unordered_map<Direction::Type, Microsoft::WRL::ComPtr<ID3D12PipelineState>>> mPSOs;
		std::unordered_map<Filter::Type, std::unordered_map<Direction::Type, IDxcBlob*>> mShaders;
	};
}