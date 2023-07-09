#include "Debug.h"
#include "Logger.h"
#include "ShaderManager.h"
#include "D3D12Util.h"

using namespace Debug;

bool DebugClass::Initialize(ID3D12Device* device, ShaderManager* const manager, UINT width, UINT height, DXGI_FORMAT backBufferFormat) {
	md3dDevice = device;
	mShaderManager = manager;

	mWidth = width;
	mHeight = height;

	mViewport = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
	mScissorRect = { 0, 0, static_cast<int>(width), static_cast<int>(height) };

	mBackBufferFormat = backBufferFormat;

	mNumEnabledMaps = 0;

	return true;
}

bool DebugClass::CompileShaders(const std::wstring& filePath) {
	const auto fullPath = filePath + L"Debug.hlsl";
	auto vsInfo = D3D12ShaderInfo(fullPath.c_str(), L"VS", L"vs_6_3");
	auto psInfo = D3D12ShaderInfo(fullPath.c_str(), L"PS", L"ps_6_3");
	CheckIsValid(mShaderManager->CompileShader(vsInfo, "debugVS"));
	CheckIsValid(mShaderManager->CompileShader(psInfo, "debugPS"));

	return true;
}

bool DebugClass::BuildRootSignature(const StaticSamplers& samplers) {
	CD3DX12_ROOT_PARAMETER slotRootParameter[RootSignatureLayout::Count];

	CD3DX12_DESCRIPTOR_RANGE texTables[5];
	texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
	texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0);
	texTables[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);
	texTables[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 0);
	texTables[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4, 0);

	slotRootParameter[RootSignatureLayout::ECB_Debug].InitAsConstantBufferView(0);
	slotRootParameter[RootSignatureLayout::EC_Consts].InitAsConstants(RootConstantsLayout::Count, 1);
	slotRootParameter[RootSignatureLayout::ESI_Debug0].InitAsDescriptorTable(1, &texTables[0]);
	slotRootParameter[RootSignatureLayout::ESI_Debug1].InitAsDescriptorTable(1, &texTables[1]);
	slotRootParameter[RootSignatureLayout::ESI_Debug2].InitAsDescriptorTable(1, &texTables[2]);
	slotRootParameter[RootSignatureLayout::ESI_Debug3].InitAsDescriptorTable(1, &texTables[3]);
	slotRootParameter[RootSignatureLayout::ESI_Debug4].InitAsDescriptorTable(1, &texTables[4]);

	CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
		_countof(slotRootParameter), slotRootParameter,
		static_cast<UINT>(samplers.size()), samplers.data(),
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
	);

	CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice, rootSigDesc, &mRootSignature));

	return true;
}
bool DebugClass::BuildPso() {
	D3D12_GRAPHICS_PIPELINE_STATE_DESC debugPsoDesc = D3D12Util::QuadPsoDesc();
	debugPsoDesc.pRootSignature = mRootSignature.Get();
	{
		auto vs = mShaderManager->GetDxcShader("debugVS");
		auto ps = mShaderManager->GetDxcShader("debugPS");
		debugPsoDesc.VS = { reinterpret_cast<BYTE*>(vs->GetBufferPointer()), vs->GetBufferSize() };
		debugPsoDesc.PS = { reinterpret_cast<BYTE*>(ps->GetBufferPointer()), ps->GetBufferSize() };
	}
	debugPsoDesc.RTVFormats[0] = mBackBufferFormat;
	CheckHResult(md3dDevice->CreateGraphicsPipelineState(&debugPsoDesc, IID_PPV_ARGS(&mPSO)));

	return true;
}
void DebugClass::Run(
		ID3D12GraphicsCommandList* const cmdList,
		D3D12_CPU_DESCRIPTOR_HANDLE ro_backBuffer,
		D3D12_CPU_DESCRIPTOR_HANDLE dio_dsv,
		D3D12_GPU_VIRTUAL_ADDRESS cbAddress) {
	cmdList->SetPipelineState(mPSO.Get());
	cmdList->SetGraphicsRootSignature(mRootSignature.Get());

	cmdList->RSSetViewports(1, &mViewport);
	cmdList->RSSetScissorRects(1, &mScissorRect);

	cmdList->OMSetRenderTargets(1, &ro_backBuffer, true, &dio_dsv);

	cmdList->SetGraphicsRootConstantBufferView(RootSignatureLayout::ECB_Debug, cbAddress);
	cmdList->SetGraphicsRoot32BitConstants(
		RootSignatureLayout::EC_Consts,
		static_cast<UINT>(mDebugMasks.size()),
		mDebugMasks.data(),
		RootConstantsLayout::ESampleMask0
	);

	for (int i = 0; i < mNumEnabledMaps; ++i) {
		cmdList->SetGraphicsRootDescriptorTable(static_cast<UINT>(RootSignatureLayout::ESI_Debug0 + i), mhDebugGpuSrvs[i]);
	}

	cmdList->IASetVertexBuffers(0, 0, nullptr);
	cmdList->IASetIndexBuffer(nullptr);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->DrawInstanced(6, static_cast<UINT>(mNumEnabledMaps), 0, 0);
}

bool DebugClass::OnResize(UINT width, UINT height) {
	if ((mWidth != width) || (mHeight != height)) {
		mWidth = width;
		mHeight = height;

		mViewport = { 0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f };
		mScissorRect = { 0, 0, static_cast<int>(width), static_cast<int>(height) };
	}

	return true;
}

bool DebugClass::AddDebugMap(D3D12_GPU_DESCRIPTOR_HANDLE hGpuSrv, DebugShaderParams::SampleMask::Type mask) {
	if (mNumEnabledMaps >= 5) return false;

	mhDebugGpuSrvs[mNumEnabledMaps] = hGpuSrv;
	mDebugMasks[mNumEnabledMaps] = mask;

	++mNumEnabledMaps;

	return true;
}

void DebugClass::RemoveDebugMap(D3D12_GPU_DESCRIPTOR_HANDLE hGpuSrv) {
	for (int i = 0; i < mNumEnabledMaps; ++i) {
		if (mhDebugGpuSrvs[i].ptr == hGpuSrv.ptr) {
			for (int curr = i, end = mNumEnabledMaps - 2; curr <= end; ++curr) {
				int next = curr + 1;
				mhDebugGpuSrvs[curr] = mhDebugGpuSrvs[next];
				mDebugMasks[curr] = mDebugMasks[next];
			}


			--mNumEnabledMaps;
			return;
		}
	}
}