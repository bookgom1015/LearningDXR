#include "Renderer.h"
#include "Logger.h"
#include "RenderMacros.h"
#include "D3D12Util.h"
#include "RTXStructures.h"
#include "FrameResource.h"
#include "ShaderManager.h"
#include "Camera.h"
#include "Mesh.h"
#include "GeometryGenerator.h"
#include "HlslCompaction.h"
#include "ShadowMap.h"
#include "GBuffer.h"
#include "DxrShadowMap.h"
#include "Ssao.h"
#include "Rtao.h"

#include <array>
#include <d3dcompiler.h>

#include <imgui.h>
#include <backends/imgui_impl_win32.h>
#include <backends/imgui_impl_dx12.h>

#undef min
#undef max

using namespace DirectX;
using namespace DirectX::PackedVector;
using namespace Microsoft::WRL;

namespace {
	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 9> GetStaticSamplers() {
		const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
			0,									// shaderRegister
			D3D12_FILTER_MIN_MAG_MIP_POINT,		// filter
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,	// addressU
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,	// addressV
			D3D12_TEXTURE_ADDRESS_MODE_WRAP		// addressW
		);

		const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
			1,									// shaderRegister
			D3D12_FILTER_MIN_MAG_MIP_POINT,		// filter
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	// addressU
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	// addressV
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP	// addressW
		);

		const CD3DX12_STATIC_SAMPLER_DESC linearWrap(
			2, // shaderRegister
			D3D12_FILTER_MIN_MAG_MIP_LINEAR,	// filter
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,	// addressU
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,	// addressV
			D3D12_TEXTURE_ADDRESS_MODE_WRAP		// addressW
		);

		const CD3DX12_STATIC_SAMPLER_DESC linearClamp(
			3,									// shaderRegister
			D3D12_FILTER_MIN_MAG_MIP_LINEAR,	// filter
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	// addressU
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	// addressV
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP	// addressW
		);

		const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
			4,									// shaderRegister
			D3D12_FILTER_ANISOTROPIC,			// filter
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,	// addressU
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,	// addressV
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,	// addressW
			0.0f,								// mipLODBias
			8									// maxAnisotropy
		);

		const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
			5,									// shaderRegister
			D3D12_FILTER_ANISOTROPIC,			// filter
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	// addressU
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	// addressV
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	// addressW
			0.0f,								// mipLODBias
			8									// maxAnisotropy
		);

		const CD3DX12_STATIC_SAMPLER_DESC anisotropicBorder(
			6,									// shaderRegister
			D3D12_FILTER_ANISOTROPIC,			// filter
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,	// addressU
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,	// addressV
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,	// addressW
			0.0f,								// mipLODBias
			8,									// maxAnisotropy
			D3D12_COMPARISON_FUNC_ALWAYS,
			D3D12_STATIC_BORDER_COLOR_OPAQUE_BLACK
		);

		const CD3DX12_STATIC_SAMPLER_DESC depthMap(
			7,									// shaderRegister
			D3D12_FILTER_MIN_MAG_MIP_POINT,		// filter
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,	// addressU
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,	// addressV
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,	// addressW
			0.0f,								// mipLODBias
			0,									// maxAnisotropy
			D3D12_COMPARISON_FUNC_LESS_EQUAL,
			D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE
		);

		const CD3DX12_STATIC_SAMPLER_DESC shadow(
			8,													// shaderRegister
			D3D12_FILTER_COMPARISON_MIN_MAG_LINEAR_MIP_POINT,	// filter
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,					// addressU
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,					// addressV
			D3D12_TEXTURE_ADDRESS_MODE_BORDER,					// addressW
			0.0f,												// mipLODBias
			16,													// maxAnisotropy
			D3D12_COMPARISON_FUNC_LESS_EQUAL,
			D3D12_STATIC_BORDER_COLOR_OPAQUE_WHITE
		);

		return { pointWrap, pointClamp, linearWrap, linearClamp, anisotropicWrap, anisotropicClamp, anisotropicBorder, depthMap, shadow };
	}

	float* CalcGaussWeights(float sigma) {
		float twoSigma2 = 2.0f * sigma * sigma;

		// Estimate the blur radius based on sigma since sigma controls the "width" of the bell curve.
		int blurRadius = static_cast<int>(ceil(2.0f * sigma));

		if (blurRadius > 17) return nullptr;

		int size = 2 * blurRadius + 1;
		float* weights = new float[size];

		float weightSum = 0.0f;

		for (int i = -blurRadius; i <= blurRadius; ++i) {
			float x = static_cast<float>(i);

			weights[i + blurRadius] = expf(-x * x / twoSigma2);

			weightSum += weights[i + blurRadius];
		}

		// Divide by the sum so all the weights add up to 1.0.
		for (int i = 0; i < size; ++i)
			weights[i] /= weightSum;

		return weights;
	}

	const DXGI_FORMAT NormalMapFormat = DXGI_FORMAT_R8G8B8A8_SNORM;
	const DXGI_FORMAT SpecularMapFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
}

Renderer::Renderer() {
	bIsCleanedUp = false;
	bInitialized = false;
	bRaytracing = false;
	bDisplayImgGui = false;
	bDisplayMaps = true;

	mCurrFrameResourceIndex = 0;
	mGeometryBufferCount = 0;

	mSceneBounds.Center = XMFLOAT3(0.0f, 0.0f, 0.0f);
	float widthSquared = 32.0f * 32.0f;
	mSceneBounds.Radius = sqrtf(widthSquared + widthSquared);
	mLightDir = { 0.57735f, -0.57735f, 0.57735f };

	auto blurWeights = CalcGaussWeights(2.5f);
	mBlurWeights[0] = XMFLOAT4(&blurWeights[0]);
	mBlurWeights[1] = XMFLOAT4(&blurWeights[4]);
	mBlurWeights[2] = XMFLOAT4(&blurWeights[8]);

	mShaderManager = std::make_unique<ShaderManager>();
	mMainPassCB = std::make_unique<PassConstants>();
	mShadowPassCB = std::make_unique<PassConstants>();
	mTLAS = std::make_unique<AccelerationStructureBuffer>();

	mDXROutputs.resize(gNumFrameResources);

	mShadowMap = std::make_unique<ShadowMap>();

	mGBuffer = std::make_unique<GBuffer>();

	mDxrShadowMap = std::make_unique<DxrShadowMap>();
	mNumDxrShadowBlurs = 3;

	mRtao = std::make_unique<Rtao>();
	mRtaoRadius = 10.0f;
	mRtaoFadeStart = 1.0f;
	mRtaoFadeEnd = 100.0f;
	mRtaoEpsilon = 0.05f;
	mRtaoAccum = 0;
	mRtaoSampleCount = 2;

	mSsao = std::make_unique<Ssao>();
	mSsaoRadius = 0.5f;
	mSsaoFadeStart = 0.2f;
	mSsaoFadeEnd = 2.0f;
	mSsaoEpsilon = 0.05f;
	mNumSsaoBlurs = 3;

	mSsaoDotThreshold = 0.95f;
	mSsaoDepthThreshold = 0.5f;

	bCheckerboardSamplingEnabled = false;
	bCheckerboardGenerateRaysForEvenPixels = false;
}

Renderer::~Renderer() {
	if (!bIsCleanedUp)
		CleanUp();
}

bool Renderer::Initialize(HWND hMainWnd, UINT width, UINT height) {
	CheckIsValid(LowRenderer::Initialize(hMainWnd, width, height));

	BuildDebugViewport();
	
	CheckIsValid(mShaderManager->Initialize());

	CheckHResult(mDirectCmdListAlloc->Reset());
	CheckHResult(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	const auto device = md3dDevice.Get();
	const auto cmdList = mCommandList.Get();
	
	CheckIsValid(mShadowMap->Initialize(device, 2048, 2048));
	CheckIsValid(mGBuffer->Initialize(device, width, height, BackBufferFormat, DXGI_FORMAT_R24_UNORM_X8_TYPELESS));
	CheckIsValid(mDxrShadowMap->Initialize(device, cmdList, width, height));
	CheckIsValid(mSsao->Initialize(device, cmdList, width, height, 1));
	CheckIsValid(mRtao->Initialize(device, cmdList, width, height));

	// Shared
	CheckIsValid(CompileShaders());
	CheckIsValid(BuildFrameResources());
	CheckIsValid(BuildGeometries());
	CheckIsValid(BuildMaterials());
	CheckIsValid(BuildResources());
	CheckIsValid(BuildRootSignatures());
	CheckIsValid(BuildDescriptorHeaps());
	CheckIsValid(BuildDescriptors());

	// Raterization
	CheckIsValid(BuildPSOs());
	CheckIsValid(BuildRenderItems());

	// Ray-tracing
	CheckIsValid(BuildBLAS());
	CheckIsValid(BuildTLAS());
	CheckIsValid(BuildDXRPSOs());
	CheckIsValid(BuildShaderTables());

	CheckHResult(cmdList->Close());
	ID3D12CommandList* cmdsLists[] = { cmdList };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
	CheckIsValid(FlushCommandQueue());

	CheckIsValid(InitImGui());

	bInitialized = true;
	return true;
}

void Renderer::CleanUp() {
	CleanUpImGui();
	mShaderManager->CleanUp();

	LowRenderer::CleanUp();

	bIsCleanedUp = true;
}

bool Renderer::Update(const GameTimer& gt) {
	mCurrFrameResourceIndex = (mCurrFrameResourceIndex + 1) % gNumFrameResources;
	mCurrFrameResource = mFrameResources[mCurrFrameResourceIndex].get();

	// Has the GPU finished processing the commands of the current frame resource?
	// If not, wait until the GPU has completed commands up to this fence point.
	if (mCurrFrameResource->Fence != 0 && mFence->GetCompletedValue() < mCurrFrameResource->Fence) {
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);
		CheckHResult(mFence->SetEventOnCompletion(mCurrFrameResource->Fence, eventHandle));
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	CheckIsValid(UpdateObjectCB(gt));
	CheckIsValid(UpdatePassCB(gt));
	CheckIsValid(UpdateShadowPassCB(gt));
	CheckIsValid(UpdateMaterialCB(gt));
	CheckIsValid(UpdateBlurPassCB(gt));
	if (!bRaytracing) {
		CheckIsValid(UpdateSsaoPassCB(gt));
	}
	else {
		CheckIsValid(UpdateRtaoPassCB(gt));
		CheckIsValid(UpdateCrossBilateralFilterCB(gt));
		CheckIsValid(UpdateCalcMeanVarCB(gt));
	}

	return true;
}

bool Renderer::Draw() {
	CheckHResult(mCurrFrameResource->CmdListAlloc->Reset());

	if (bRaytracing) { CheckIsValid(Raytrace()); }
	else { CheckIsValid(Rasterize()); }

	CheckIsValid(DrawDebugLayer());

	if (bDisplayImgGui) CheckIsValid(DrawImGui());;

	DXGI_PRESENT_PARAMETERS presentParams = {};
	presentParams.DirtyRectsCount = 0;
	presentParams.pDirtyRects = NULL;
	presentParams.pScrollRect = NULL;
	presentParams.pScrollOffset = NULL;
	CheckHResult(mSwapChain->Present1(0, 0, &presentParams));
	NextBackBuffer();

	mCurrFrameResource->Fence = static_cast<UINT>(IncCurrentFence());
	mCommandQueue->Signal(mFence.Get(), GetCurrentFence());

	return true;
}

bool Renderer::OnResize(UINT width, UINT height) {
	CheckIsValid(LowRenderer::OnResize(width, height));

	BuildDebugViewport();

	CheckHResult(mDirectCmdListAlloc->Reset());
	CheckHResult(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	const auto pCmdList = mCommandList.Get();

	CheckIsValid(mGBuffer->OnResize(width, height, mDepthStencilBuffer.Get()));
	CheckIsValid(mDxrShadowMap->OnResize(pCmdList, width, height));
	CheckIsValid(mSsao->OnResize(width, height));
	CheckIsValid(mRtao->OnResize(pCmdList, width, height));

	CheckHResult(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { pCmdList };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);
	CheckIsValid(FlushCommandQueue());

	CheckIsValid(BuildResources());
	CheckIsValid(BuildDescriptors());

	return true;
}

void Renderer::SetRenderType(bool bRaytrace) {
	bRaytracing = bRaytrace;
}

void Renderer::SetCamera(Camera* pCam) {
	mCamera = pCam;
}

void Renderer::DisplayImGui(bool state) {
	bDisplayImgGui = state;
}

bool Renderer::CreateRtvAndDsvDescriptorHeaps() {
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount + GBuffer::NumRenderTargets + Ssao::NumRenderTargets;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	CheckHResult(md3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 1 + ShadowMap::NumDepthStenciles;
	dsvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
	dsvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	dsvHeapDesc.NodeMask = 0;
	CheckHResult(md3dDevice->CreateDescriptorHeap(&dsvHeapDesc, IID_PPV_ARGS(mDsvHeap.GetAddressOf())));

	return true;
}

bool Renderer::InitImGui() {
	// Setup dear ImGui context.
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGuiIO& io = ImGui::GetIO(); (void)io;

	// Setup Dear ImGui style.
	ImGui::StyleColorsDark();

	const auto pDescHeap = mCbvSrvUavHeap.Get();
	auto descSize = GetCbvSrvUavDescriptorSize();

	// Setup platform/renderer backends
	CheckIsValid(ImGui_ImplWin32_Init(mhMainWnd));
	CheckIsValid(ImGui_ImplDX12_Init(
		md3dDevice.Get(),
		SwapChainBufferCount,
		BackBufferFormat,
		pDescHeap,
		D3D12Util::GetCpuHandle(pDescHeap , static_cast<INT>(EDescriptors::ES_Font), descSize),
		D3D12Util::GetGpuHandle(pDescHeap , static_cast<INT>(EDescriptors::ES_Font), descSize)
	));

	return true;
}

void Renderer::CleanUpImGui() {
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
}

void Renderer::BuildDebugViewport() {
	UINT width = GetClientWidth();
	UINT height = GetClientHeight();

	float fwidth = static_cast<float>(width);
	float fheight = static_cast<float>(height);

	float quaterWidth = fwidth * 0.25f;
	float quaterHeight = fheight * 0.25f;

	float threeFourthWidth = fwidth * 0.75f;

	mDebugViewport.TopLeftX = threeFourthWidth;
	mDebugViewport.TopLeftY = 0;
	mDebugViewport.Width = quaterWidth;
	mDebugViewport.Height = quaterHeight;
	mDebugViewport.MinDepth = 0.0f;
	mDebugViewport.MaxDepth = 1.0f;

	mDebugScissorRect = { 0, 0, static_cast<LONG>(width), static_cast<LONG>(height) };
}

bool Renderer::CompileShaders() {
	//
	// d3dcompiler
	//
	{

	}
	//
	// dxcompiler
	//
	{
		const auto filePath = ShaderFilePathW + L"Debug.hlsl";
		auto vsInfo = D3D12ShaderInfo(filePath.c_str(), L"VS", L"vs_6_3");
		auto psInfo = D3D12ShaderInfo(filePath.c_str(), L"PS", L"ps_6_3");
		CheckIsValid(mShaderManager->CompileShader(vsInfo, "debugVS"));
		CheckIsValid(mShaderManager->CompileShader(psInfo, "debugPS"));
	}
	{
		const auto filePath = ShaderFilePathW + L"Gizmo.hlsl";
		auto vsInfo = D3D12ShaderInfo(filePath.c_str(), L"VS", L"vs_6_3");
		auto psInfo = D3D12ShaderInfo(filePath.c_str(), L"PS", L"ps_6_3");
		CheckIsValid(mShaderManager->CompileShader(vsInfo, "gizmoVS"));
		CheckIsValid(mShaderManager->CompileShader(psInfo, "gizmoPS"));
	}
	{
		const auto filePath = ShaderFilePathW + L"Shadow.hlsl";
		auto vsInfo = D3D12ShaderInfo(filePath.c_str(), L"VS", L"vs_6_3");
		auto psInfo = D3D12ShaderInfo(filePath.c_str(), L"PS", L"ps_6_3");
		CheckIsValid(mShaderManager->CompileShader(vsInfo, "shadowVS"));
		CheckIsValid(mShaderManager->CompileShader(psInfo, "shadowPS"));
	}
	{
		const auto filePath = ShaderFilePathW + L"GBuffer.hlsl";
		auto vsInfo = D3D12ShaderInfo(filePath.c_str(), L"VS", L"vs_6_3");
		auto psInfo = D3D12ShaderInfo(filePath.c_str(), L"PS", L"ps_6_3");
		CheckIsValid(mShaderManager->CompileShader(vsInfo, "gbufferVS"));
		CheckIsValid(mShaderManager->CompileShader(psInfo, "gbufferPS"));
	}
	{
		const auto filePath = ShaderFilePathW + L"BackBuffer.hlsl";
		auto vsInfo = D3D12ShaderInfo(filePath.c_str(), L"VS", L"vs_6_3");
		auto psInfo = D3D12ShaderInfo(filePath.c_str(), L"PS", L"ps_6_3");
		CheckIsValid(mShaderManager->CompileShader(vsInfo, "backBufferVS"));
		CheckIsValid(mShaderManager->CompileShader(psInfo, "backBufferPS"));
	}
	{
		const auto filePath = ShaderFilePathW + L"Ssao.hlsl";
		auto vsInfo = D3D12ShaderInfo(filePath.c_str(), L"VS", L"vs_6_3");
		auto psInfo = D3D12ShaderInfo(filePath.c_str(), L"PS", L"ps_6_3");
		CheckIsValid(mShaderManager->CompileShader(vsInfo, "ssaoVS"));
		CheckIsValid(mShaderManager->CompileShader(psInfo, "ssaoPS"));
	}
	{
		const auto filePath = ShaderFilePathW + L"GaussianBlur.hlsl";
		auto vsInfo = D3D12ShaderInfo(filePath.c_str(), L"VS", L"vs_6_3");
		auto psInfo = D3D12ShaderInfo(filePath.c_str(), L"PS", L"ps_6_3");
		CheckIsValid(mShaderManager->CompileShader(vsInfo, "gaussianBlurVS"));
		CheckIsValid(mShaderManager->CompileShader(psInfo, "gaussianBlurPS"));
	}
	{
		const auto filePath = ShaderFilePathW + L"DxrBackBuffer.hlsl";
		auto vsInfo = D3D12ShaderInfo(filePath.c_str(), L"VS", L"vs_6_3");
		auto psInfo = D3D12ShaderInfo(filePath.c_str(), L"PS", L"ps_6_3");
		CheckIsValid(mShaderManager->CompileShader(vsInfo, "dxrBackBufferVS"));
		CheckIsValid(mShaderManager->CompileShader(psInfo, "dxrBackBufferPS"));
	}
	{
		const auto filePath = ShaderFilePathW + L"ShadowRay.hlsl";
		auto shaderInfo = D3D12ShaderInfo(filePath.c_str(), L"", L"lib_6_3");
		CheckIsValid(mShaderManager->CompileShader(shaderInfo, "shadowRay"));
	}
	{
		const auto filePath = ShaderFilePathW + L"Rtao.hlsl";
		auto shaderInfo = D3D12ShaderInfo(filePath.c_str(), L"", L"lib_6_3");
		CheckIsValid(mShaderManager->CompileShader(shaderInfo, "rtao"));
	}
	{
		const auto filePath = ShaderFilePathW + L"ComputeGaussianBlur.hlsl";
		auto shaderInfo = D3D12ShaderInfo(filePath.c_str(), L"HorzBlurCS", L"cs_6_3");
		CheckIsValid(mShaderManager->CompileShader(shaderInfo, "horzGaussianBlurCS"));
	}
	{
		const auto filePath = ShaderFilePathW + L"ComputeGaussianBlur.hlsl";
		auto shaderInfo = D3D12ShaderInfo(filePath.c_str(), L"VertBlurCS", L"cs_6_3");
		CheckIsValid(mShaderManager->CompileShader(shaderInfo, "vertGaussianBlurCS"));
	}
	{
		const auto filePath = ShaderFilePathW + L"GroundTruthAO.hlsl";
		auto shaderInfo = D3D12ShaderInfo(filePath.c_str(), L"CS", L"cs_6_3");
		CheckIsValid(mShaderManager->CompileShader(shaderInfo, "gtAoCS"));
	}
	{
		const auto filePath = ShaderFilePathW + L"TemporalSupersamplingReverseReproject.hlsl";
		auto shaderInfo = D3D12ShaderInfo(filePath.c_str(), L"CS", L"cs_6_3");
		CheckIsValid(mShaderManager->CompileShader(shaderInfo, "tsppReprojCS"));
	}
	{
		const auto filePath = ShaderFilePathW + L"TemporalSupersamplingBlendWithCurrentFrame.hlsl";
		auto shaderInfo = D3D12ShaderInfo(filePath.c_str(), L"CS", L"cs_6_3");
		CheckIsValid(mShaderManager->CompileShader(shaderInfo, "tsppBlendCS"));
	}
	{
		const auto filePath = ShaderFilePathW + L"CalculatePartialDerivatives.hlsl";
		auto shaderInfo = D3D12ShaderInfo(filePath.c_str(), L"CS", L"cs_6_3");
		CheckIsValid(mShaderManager->CompileShader(shaderInfo, "partialDerivativesCS"));
	}
	{
		const auto filePath = ShaderFilePathW + L"CalculateLocalMeanVariance.hlsl";
		auto shaderInfo = D3D12ShaderInfo(filePath.c_str(), L"CS", L"cs_6_3");
		CheckIsValid(mShaderManager->CompileShader(shaderInfo, "calcLocalMeanVarianceCS"));
	}
	{
		const auto filePath = ShaderFilePathW + L"FillInCheckerboard.hlsl";
		auto shaderInfo = D3D12ShaderInfo(filePath.c_str(), L"CS", L"cs_6_3");
		CheckIsValid(mShaderManager->CompileShader(shaderInfo, "fillInCheckerboardCS"));
	}

	return true;
}

bool Renderer::BuildFrameResources() {
	for (int i = 0; i < gNumFrameResources; i++) {
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(), 2, gNumObjects, gNumMaterials));
		CheckIsValid(mFrameResources.back()->Initialize());
	}

	return true;
}

bool Renderer::BuildGeometries() {
	GeometryGenerator geoGen;
	//
	// Builds sphere geometry.
	//
	{
		GeometryGenerator::MeshData sphere = geoGen.CreateSphere(1.0f, 32, 32);

		SubmeshGeometry sphereSubmesh;
		sphereSubmesh.IndexCount = static_cast<UINT>(sphere.Indices32.size());
		sphereSubmesh.BaseVertexLocation = 0;
		sphereSubmesh.StartIndexLocation = 0;

		std::vector<Vertex> vertices(sphere.Vertices.size());
		for (size_t i = 0, end = vertices.size(); i < end; ++i) {
			vertices[i].Pos = sphere.Vertices[i].Position;
			vertices[i].Normal = sphere.Vertices[i].Normal;
			vertices[i].TexC = sphere.Vertices[i].TexC;
			vertices[i].Tangent = sphere.Vertices[i].TangentU;
		}

		std::vector<std::uint32_t> indices;
		indices.insert(indices.end(), std::begin(sphere.Indices32), std::end(sphere.Indices32));

		const UINT vbByteSize = static_cast<UINT>(vertices.size() * sizeof(Vertex));
		const UINT ibByteSize = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));

		auto geo = std::make_unique<MeshGeometry>();
		geo->Name = "sphere";

		CheckHResult(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
		CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

		CheckHResult(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
		CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

		CheckIsValid(D3D12Util::CreateDefaultBuffer(
			md3dDevice.Get(),
			mCommandList.Get(),
			vertices.data(),
			vbByteSize,
			geo->VertexBufferUploader,
			geo->VertexBufferGPU)
		);

		CheckIsValid(D3D12Util::CreateDefaultBuffer(
			md3dDevice.Get(),
			mCommandList.Get(),
			indices.data(),
			ibByteSize,
			geo->IndexBufferUploader,
			geo->IndexBufferGPU)
		);

		geo->VertexByteStride = static_cast<UINT>(sizeof(Vertex));
		geo->VertexBufferByteSize = vbByteSize;
		geo->IndexFormat = DXGI_FORMAT_R32_UINT;
		geo->IndexBufferByteSize = ibByteSize;
		geo->GeometryIndex = static_cast<UINT>(mGeometries.size());

		geo->DrawArgs["sphere"] = sphereSubmesh; 

		mGeometries[geo->Name] = std::move(geo);
	}
	//
	// Build grid geometry
	//
	{
		GeometryGenerator::MeshData grid = geoGen.CreateGrid(32.0f, 32.0f, 16, 16);

		SubmeshGeometry gridSubmesh;
		gridSubmesh.IndexCount = static_cast<UINT>(grid.Indices32.size());
		gridSubmesh.BaseVertexLocation = 0;
		gridSubmesh.StartIndexLocation = 0;

		std::vector<Vertex> vertices(grid.Vertices.size());
		for (size_t i = 0, end = vertices.size(); i < end; ++i) {
			vertices[i].Pos = grid.Vertices[i].Position;
			vertices[i].Normal = grid.Vertices[i].Normal;
			vertices[i].TexC = grid.Vertices[i].TexC;
			vertices[i].Tangent = grid.Vertices[i].TangentU;
		}

		std::vector<std::uint32_t> indices;
		indices.insert(indices.end(), std::begin(grid.Indices32), std::end(grid.Indices32));

		const UINT vbByteSize = static_cast<UINT>(vertices.size() * sizeof(Vertex));
		const UINT ibByteSize = static_cast<UINT>(indices.size() * sizeof(std::uint32_t));

		auto geo = std::make_unique<MeshGeometry>();
		geo->Name = "grid";

		CheckHResult(D3DCreateBlob(vbByteSize, &geo->VertexBufferCPU));
		CopyMemory(geo->VertexBufferCPU->GetBufferPointer(), vertices.data(), vbByteSize);

		CheckHResult(D3DCreateBlob(ibByteSize, &geo->IndexBufferCPU));
		CopyMemory(geo->IndexBufferCPU->GetBufferPointer(), indices.data(), ibByteSize);

		CheckIsValid(D3D12Util::CreateDefaultBuffer(
			md3dDevice.Get(),
			mCommandList.Get(),
			vertices.data(),
			vbByteSize,
			geo->VertexBufferUploader,
			geo->VertexBufferGPU)
		);

		CheckIsValid(D3D12Util::CreateDefaultBuffer(
			md3dDevice.Get(),
			mCommandList.Get(),
			indices.data(),
			ibByteSize,
			geo->IndexBufferUploader,
			geo->IndexBufferGPU)
		);

		geo->VertexByteStride = static_cast<UINT>(sizeof(Vertex));
		geo->VertexBufferByteSize = vbByteSize;
		geo->IndexFormat = DXGI_FORMAT_R32_UINT;
		geo->IndexBufferByteSize = ibByteSize;
		geo->GeometryIndex = static_cast<UINT>(mGeometries.size());

		geo->DrawArgs["grid"] = gridSubmesh;

		mGeometries[geo->Name] = std::move(geo);
	}

	return true;
}

bool Renderer::BuildMaterials() {
	int count = 0;

	auto whiteMat = std::make_unique<Material>();
	whiteMat->Name = "white";
	whiteMat->MatSBIndex = count++;
	whiteMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	whiteMat->FresnelR0 = XMFLOAT3(0.88725f, 0.88725f, 0.88725f);
	whiteMat->Roughness = 0.1f;
	mMaterials[whiteMat->Name] = std::move(whiteMat);

	auto redMat = std::make_unique<Material>();
	redMat->Name = "red";
	redMat->MatSBIndex = count++;
	redMat->DiffuseAlbedo = XMFLOAT4(1.0f, 0.0f, 0.0f, 1.0f);
	redMat->FresnelR0 = XMFLOAT3(0.88725f, 0.88725f, 0.88725f);
	redMat->Roughness = 0.1f;
	mMaterials[redMat->Name] = std::move(redMat);

	auto greenMat = std::make_unique<Material>();
	greenMat->Name = "green";
	greenMat->MatSBIndex = count++;
	greenMat->DiffuseAlbedo = XMFLOAT4(0.0f, 1.0f, 0.0f, 1.0f);
	greenMat->FresnelR0 = XMFLOAT3(0.88725f, 0.88725f, 0.88725f);
	greenMat->Roughness = 0.1f;
	mMaterials[greenMat->Name] = std::move(greenMat);

	auto blueMat = std::make_unique<Material>();
	blueMat->Name = "blue";
	blueMat->MatSBIndex = count++;
	blueMat->DiffuseAlbedo = XMFLOAT4(0.0f, 0.0f, 1.0f, 1.0f);
	blueMat->FresnelR0 = XMFLOAT3(0.88725f, 0.88725f, 0.88725f);
	blueMat->Roughness = 0.1f;
	mMaterials[blueMat->Name] = std::move(blueMat);

	return true;
}

bool Renderer::BuildResources() {
	// Describe the DXR output resource (texture)
	// Dimensions and format should match the swapchain
	// Initialize as a copy source, since we will copy this buffer's contents to the swapchain
	D3D12_RESOURCE_DESC desc = {};
	desc.DepthOrArraySize = 1;
	desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS;
	desc.Width = GetClientWidth();
	desc.Height = GetClientHeight();
	desc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	desc.MipLevels = 1;
	desc.SampleDesc.Count = 1;
	desc.SampleDesc.Quality = 0;

	// Create the buffer resource
	D3D12_HEAP_PROPERTIES heapProps = {
		D3D12_HEAP_TYPE_DEFAULT,
		D3D12_CPU_PAGE_PROPERTY_UNKNOWN,
		D3D12_MEMORY_POOL_UNKNOWN,
		0, 0
	};

	for (int i = 0; i < gNumFrameResources; ++i) {
		CheckHResult(md3dDevice->CreateCommittedResource(
			&heapProps,
			D3D12_HEAP_FLAG_NONE,
			&desc,
			D3D12_RESOURCE_STATE_COMMON,
			nullptr,
			IID_PPV_ARGS(&mDXROutputs[i])
		));
	}

	return true;
}

bool Renderer::BuildRootSignatures() {
	//
	// Rasterization
	//
	// Default
	{
		CD3DX12_ROOT_PARAMETER slotRootParameter[ERasterization::RootSignatureLayout::Count];

		CD3DX12_DESCRIPTOR_RANGE texTables[2];
		texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, (UINT)EDescriptors::Srv_End - (UINT)EDescriptors::Srv_Start + 1, 0, 0);
		texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, (UINT)EDescriptors::Uav_End - (UINT)EDescriptors::Uav_Start + 1, 0, 0);

		slotRootParameter[ERasterization::RootSignatureLayout::ECB_Pass].InitAsConstantBufferView(0);
		slotRootParameter[ERasterization::RootSignatureLayout::EC_Consts].InitAsConstants(ERasterization::RootConstantsLayout::Count, 1);
		slotRootParameter[ERasterization::RootSignatureLayout::ESB_Object].InitAsShaderResourceView(0, 1);
		slotRootParameter[ERasterization::RootSignatureLayout::ESB_Material].InitAsShaderResourceView(0, 2);
		slotRootParameter[ERasterization::RootSignatureLayout::ESI_SrvMaps].InitAsDescriptorTable(1, &texTables[0]);
		slotRootParameter[ERasterization::RootSignatureLayout::EUIO_UavMaps].InitAsDescriptorTable(1, &texTables[1]);

		auto samplers = GetStaticSamplers();

		CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
			_countof(slotRootParameter), slotRootParameter,
			static_cast<UINT>(samplers.size()), samplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
		);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice.Get(), rootSigDesc, mRootSignatures["raster"].GetAddressOf()));
	}
	// Screen-space ambient occlusion
	{
		CD3DX12_ROOT_PARAMETER slotRootParameter[EScreenSpaceAO::RootSignatureLayout::Count];

		CD3DX12_DESCRIPTOR_RANGE texTables[2];
		texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);
		texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);

		slotRootParameter[EScreenSpaceAO::RootSignatureLayout::ECB_SsaoPass].InitAsConstantBufferView(0);
		slotRootParameter[EScreenSpaceAO::RootSignatureLayout::ESI_NormalAndDepth].InitAsDescriptorTable(1, &texTables[0]);
		slotRootParameter[EScreenSpaceAO::RootSignatureLayout::ESI_RandomVector].InitAsDescriptorTable(1, &texTables[1]);

		auto samplers = GetStaticSamplers();

		CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
			_countof(slotRootParameter), slotRootParameter,
			static_cast<UINT>(samplers.size()), samplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
		);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice.Get(), rootSigDesc, mRootSignatures["ssao"].GetAddressOf()));
	}
	// Gaussian blur
	{
		CD3DX12_ROOT_PARAMETER slotRootParameter[EGaussianBlur::RootSignatureLayout::Count];

		CD3DX12_DESCRIPTOR_RANGE texTables[2];
		texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);
		texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);

		slotRootParameter[EGaussianBlur::RootSignatureLayout::ECB_BlurPass].InitAsConstantBufferView(0);
		slotRootParameter[EGaussianBlur::RootSignatureLayout::EC_Consts].InitAsConstants(EGaussianBlur::RootConstantsLayout::Count, 1);
		slotRootParameter[EGaussianBlur::RootSignatureLayout::ESI_NormalAndDepth].InitAsDescriptorTable(1, &texTables[0]);
		slotRootParameter[EGaussianBlur::RootSignatureLayout::ESI_Input].InitAsDescriptorTable(1, &texTables[1]);

		auto samplers = GetStaticSamplers();

		CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
			_countof(slotRootParameter), slotRootParameter,
			static_cast<UINT>(samplers.size()), samplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
		);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice.Get(), rootSigDesc, mRootSignatures["gaussianBlur"].GetAddressOf()));
	}
	//
	// Raytracing
	//
	// Default global root signature
	{
		CD3DX12_DESCRIPTOR_RANGE texTables[5];
		texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);		
		texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, gNumGeometryBuffers, 0, 1);
		texTables[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, gNumGeometryBuffers, 0, 2);
		texTables[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, (UINT)EDescriptors::ES_Velocity - (UINT)EDescriptors::Srv_Start + 1, 0, 3);
		texTables[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, (UINT)EDescriptors::Uav_End - (UINT)EDescriptors::Uav_Start + 1, 0, 0);

		CD3DX12_ROOT_PARAMETER slotRootParameter[EDefaultRaytracing::RootSignatureLayout::Count];
		slotRootParameter[EDefaultRaytracing::RootSignatureLayout::ESI_AccelerationStructure].InitAsShaderResourceView(0);
		slotRootParameter[EDefaultRaytracing::RootSignatureLayout::ECB_Pass].InitAsConstantBufferView(0);
		slotRootParameter[EDefaultRaytracing::RootSignatureLayout::ESB_Object].InitAsShaderResourceView(1);
		slotRootParameter[EDefaultRaytracing::RootSignatureLayout::ESB_Material].InitAsShaderResourceView(2);
		slotRootParameter[EDefaultRaytracing::RootSignatureLayout::ESB_Vertices].InitAsDescriptorTable(1, &texTables[1]);
		slotRootParameter[EDefaultRaytracing::RootSignatureLayout::ESB_Indices].InitAsDescriptorTable(1, &texTables[2]);
		slotRootParameter[EDefaultRaytracing::RootSignatureLayout::ESI_SrvMaps].InitAsDescriptorTable(1, &texTables[3]);
		slotRootParameter[EDefaultRaytracing::RootSignatureLayout::EUIO_UavMaps].InitAsDescriptorTable(1, &texTables[4]);

		auto samplers = GetStaticSamplers();

		CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(
			_countof(slotRootParameter), slotRootParameter,
			static_cast<UINT>(samplers.size()), samplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_NONE
		);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice.Get(), globalRootSignatureDesc, mRootSignatures["dxr_global"].GetAddressOf()));
	}
	// Default local root signature
	{
		CD3DX12_ROOT_SIGNATURE_DESC localRootSignatureDesc(0, nullptr);
		localRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice.Get(), localRootSignatureDesc, mRootSignatures["dxr_local"].GetAddressOf()));
	}
	// Compute gaussian blur
	{
		CD3DX12_ROOT_PARAMETER slotRootParameter[EComputeGaussianBlur::RootSignatureLayout::Count];

		CD3DX12_DESCRIPTOR_RANGE texTables[3];
		texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);
		texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);
		texTables[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, 0);

		slotRootParameter[EComputeGaussianBlur::RootSignatureLayout::ECB_BlurPass].InitAsConstantBufferView(0);
		slotRootParameter[EComputeGaussianBlur::RootSignatureLayout::EC_Consts].InitAsConstants(EComputeGaussianBlur::RootConstantsLayout::Count, 1, 0);
		slotRootParameter[EComputeGaussianBlur::RootSignatureLayout::ESI_NormalAndDepth].InitAsDescriptorTable(1, &texTables[0]);
		slotRootParameter[EComputeGaussianBlur::RootSignatureLayout::ESI_Input].InitAsDescriptorTable(1, &texTables[1]);
		slotRootParameter[EComputeGaussianBlur::RootSignatureLayout::EUO_Output].InitAsDescriptorTable(1, &texTables[2]);

		auto samplers = GetStaticSamplers();

		CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
			_countof(slotRootParameter), slotRootParameter,
			static_cast<UINT>(samplers.size()), samplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
		);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice.Get(), rootSigDesc, mRootSignatures["csBlur"].GetAddressOf()));
	}
	// Ray-traced ambient occlusion
	{
		CD3DX12_DESCRIPTOR_RANGE texTables[2];
		texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1, 0);
		texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);

		CD3DX12_ROOT_PARAMETER slotRootParameter[ERaytracedAO::RootSignatureLayout::Count];
		slotRootParameter[ERaytracedAO::RootSignatureLayout::ESI_AccelerationStructure].InitAsShaderResourceView(0);
		slotRootParameter[ERaytracedAO::RootSignatureLayout::ECB_RtaoPass].InitAsConstantBufferView(0);
		slotRootParameter[ERaytracedAO::RootSignatureLayout::EC_Consts].InitAsConstants(ERaytracedAO::RootConstantsLayout::Count, 1, 0);
		slotRootParameter[ERaytracedAO::RootSignatureLayout::ESI_NormalAndDepth].InitAsDescriptorTable(1, &texTables[0]);
		slotRootParameter[ERaytracedAO::RootSignatureLayout::EUO_AmbientCoefficient].InitAsDescriptorTable(1, &texTables[1]);

		auto samplers = GetStaticSamplers();

		CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(
			_countof(slotRootParameter), slotRootParameter,
			static_cast<UINT>(samplers.size()), samplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_NONE
		);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice.Get(), globalRootSignatureDesc, mRootSignatures["rtao_global"].GetAddressOf()));
	}
	// Denising using ground-truth
	{
		CD3DX12_DESCRIPTOR_RANGE texTables[1];
		texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 2, 0, 0);

		CD3DX12_ROOT_PARAMETER slotRootParameter[EGroundTruthAO::RootSingatureLayout::Count];
		slotRootParameter[EGroundTruthAO::RootSingatureLayout::ESI_AmbientCoefficentMaps].InitAsDescriptorTable(1, &texTables[0]);
		slotRootParameter[EGroundTruthAO::RootSingatureLayout::EConsts].InitAsConstants(EGroundTruthAO::RootConstantsLayout::Count, 0, 0);

		auto samplers = GetStaticSamplers();

		CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(
			_countof(slotRootParameter), slotRootParameter,
			static_cast<UINT>(samplers.size()), samplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_NONE
		);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice.Get(), globalRootSignatureDesc, mRootSignatures["gtRtao"].GetAddressOf()));
	}
	// Temporal-Supersampling
	{
		CD3DX12_DESCRIPTOR_RANGE texTables[11];
		texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 0, 0);
		texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);
		texTables[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 0);
		texTables[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4, 0);
		texTables[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5, 0);
		texTables[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 6, 0);
		texTables[6].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 7, 0);
		texTables[7].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 8, 0);
		texTables[8].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);
		texTables[9].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 1, 0);
		texTables[10].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 2, 0);

		CD3DX12_ROOT_PARAMETER slotRootParameter[ETemporalSupersamplingReverseReproject::RootSinatureLayout::Count];
		slotRootParameter[ETemporalSupersamplingReverseReproject::RootSinatureLayout::ECB_CrossBilateralFilter].InitAsConstantBufferView(0, 0);
		slotRootParameter[ETemporalSupersamplingReverseReproject::RootSinatureLayout::EC_Consts].InitAsConstants(ETemporalSupersamplingReverseReproject::RootConstantsLayout::Count, 1, 0);
		slotRootParameter[ETemporalSupersamplingReverseReproject::RootSinatureLayout::ESI_NormalDepth].InitAsDescriptorTable(1, &texTables[0]);
		slotRootParameter[ETemporalSupersamplingReverseReproject::RootSinatureLayout::ESI_Velocity].InitAsDescriptorTable(1, &texTables[1]);
		slotRootParameter[ETemporalSupersamplingReverseReproject::RootSinatureLayout::ESI_ReprojectedNormalDepth].InitAsDescriptorTable(1, &texTables[2]);
		slotRootParameter[ETemporalSupersamplingReverseReproject::RootSinatureLayout::ESI_CachedNormalDepth].InitAsDescriptorTable(1, &texTables[3]);
		slotRootParameter[ETemporalSupersamplingReverseReproject::RootSinatureLayout::ESI_CachedTemporalAOCoefficient].InitAsDescriptorTable(1, &texTables[4]);
		slotRootParameter[ETemporalSupersamplingReverseReproject::RootSinatureLayout::ESI_CachedTemporalSuperSampling].InitAsDescriptorTable(1, &texTables[5]);
		slotRootParameter[ETemporalSupersamplingReverseReproject::RootSinatureLayout::ESI_CachedCoefficientSquaredMean].InitAsDescriptorTable(1, &texTables[6]);
		slotRootParameter[ETemporalSupersamplingReverseReproject::RootSinatureLayout::ESI_CachedRayHitDistance].InitAsDescriptorTable(1, &texTables[7]);
		slotRootParameter[ETemporalSupersamplingReverseReproject::RootSinatureLayout::EUO_TemporalSuperSampling].InitAsDescriptorTable(1, &texTables[8]);
		slotRootParameter[ETemporalSupersamplingReverseReproject::RootSinatureLayout::EUI_LinearDepthDerivatives].InitAsDescriptorTable(1, &texTables[9]);
		slotRootParameter[ETemporalSupersamplingReverseReproject::RootSinatureLayout::EUO_TsppCoefficientSquaredMeanRayHitDistacne].InitAsDescriptorTable(1, &texTables[10]);

		auto samplers = GetStaticSamplers(); 

		CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(
			_countof(slotRootParameter), slotRootParameter,
			static_cast<UINT>(samplers.size()), samplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_NONE
		);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice.Get(), globalRootSignatureDesc, mRootSignatures["tspp_reproj"].GetAddressOf()));
	}
	// CalculatePartialDerivatives 
	{
		CD3DX12_DESCRIPTOR_RANGE texTables[2];
		texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
		texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);

		CD3DX12_ROOT_PARAMETER slotRootParameter[ECalcPartialDerivatives::RootSignatureLayout::Count];
		slotRootParameter[ECalcPartialDerivatives::RootSignatureLayout::EC_Consts].InitAsConstants(ECalcPartialDerivatives::RootConstantsLayout::Count, 0, 0);
		slotRootParameter[ECalcPartialDerivatives::RootSignatureLayout::ESI_Depth].InitAsDescriptorTable(1, &texTables[0]);
		slotRootParameter[ECalcPartialDerivatives::RootSignatureLayout::EUO_LinearDepthDerivatives].InitAsDescriptorTable(1, &texTables[1]);

		auto samplers = GetStaticSamplers();

		CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(
			_countof(slotRootParameter), slotRootParameter,
			static_cast<UINT>(samplers.size()), samplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_NONE
		);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice.Get(), globalRootSignatureDesc, mRootSignatures["partialDerivatives"].GetAddressOf()));
	}
	// CalculateMeanVariance
	{
		CD3DX12_DESCRIPTOR_RANGE texTables[2];
		texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
		texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);

		CD3DX12_ROOT_PARAMETER slotRootParameter[ECalcLocalMeanVariance::RootSignatureLayout::Count];
		slotRootParameter[ECalcLocalMeanVariance::RootSignatureLayout::ECB_LocalMeanVar].InitAsConstantBufferView(0, 0);
		slotRootParameter[ECalcLocalMeanVariance::RootSignatureLayout::ESI_AmbientCoefficient].InitAsDescriptorTable(1, &texTables[0]);
		slotRootParameter[ECalcLocalMeanVariance::RootSignatureLayout::EUO_LocalMeanVar].InitAsDescriptorTable(1, &texTables[1]);

		auto samplers = GetStaticSamplers();

		CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(
			_countof(slotRootParameter), slotRootParameter,
			static_cast<UINT>(samplers.size()), samplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_NONE
		);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice.Get(), globalRootSignatureDesc, mRootSignatures["localMeanVariance"].GetAddressOf()));
	}
	// FillInCheckerboard
	{
		CD3DX12_DESCRIPTOR_RANGE texTables[1];
		texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0, 0);

		CD3DX12_ROOT_PARAMETER slotRootParameter[EFillInCheckerboard::RootSignatureLayout::Count];
		slotRootParameter[EFillInCheckerboard::RootSignatureLayout::ECB_LocalMeanVar].InitAsConstantBufferView(0, 0);
		slotRootParameter[EFillInCheckerboard::RootSignatureLayout::EUIO_LocalMeanVar].InitAsDescriptorTable(1, &texTables[0]);

		auto samplers = GetStaticSamplers();

		CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(
			_countof(slotRootParameter), slotRootParameter,
			static_cast<UINT>(samplers.size()), samplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_NONE
		);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice.Get(), globalRootSignatureDesc, mRootSignatures["fillInCheckerboard"].GetAddressOf()));
	}

	return true;
}

bool Renderer::BuildDescriptorHeaps() {
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = 256;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	md3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mCbvSrvUavHeap));

	return true;
}

bool Renderer::BuildDescriptors() {
	const auto pDescHeap = mCbvSrvUavHeap.Get();
	auto descSize = GetCbvSrvUavDescriptorSize();
	auto rtvDescSize = GetRtvDescriptorSize();
	auto dsvDescSize = GetDsvDescriptorSize();

	D3D12_SHADER_RESOURCE_VIEW_DESC vertexSrvDesc = {};
	vertexSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	vertexSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
	vertexSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	vertexSrvDesc.Buffer.StructureByteStride = sizeof(Vertex);
	vertexSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	D3D12_SHADER_RESOURCE_VIEW_DESC indexSrvDesc = {};
	indexSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	indexSrvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	indexSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
	indexSrvDesc.Buffer.StructureByteStride = 0;
	indexSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	for (const auto& g : mGeometries) {
		auto geo = g.second.get();

		vertexSrvDesc.Buffer.FirstElement = 0;
		vertexSrvDesc.Buffer.NumElements = static_cast<UINT>(geo->VertexBufferCPU->GetBufferSize() / sizeof(Vertex));

		md3dDevice->CreateShaderResourceView(
			geo->VertexBufferGPU.Get(),
			&vertexSrvDesc,
			D3D12Util::GetCpuHandle(pDescHeap, EDescriptors::ES_Vertices + mGeometryBufferCount, descSize)
		);

		indexSrvDesc.Buffer.FirstElement = 0;
		indexSrvDesc.Buffer.NumElements = static_cast<UINT>(geo->IndexBufferCPU->GetBufferSize() / sizeof(std::uint32_t));

		md3dDevice->CreateShaderResourceView(
			geo->IndexBufferGPU.Get(),
			&indexSrvDesc,
			D3D12Util::GetCpuHandle(pDescHeap, EDescriptors::ES_Indices + mGeometryBufferCount, descSize)
		);

		++mGeometryBufferCount;
	}

	auto cpuDesc = CD3DX12_CPU_DESCRIPTOR_HANDLE(pDescHeap->GetCPUDescriptorHandleForHeapStart());
	auto gpuDesc = CD3DX12_GPU_DESCRIPTOR_HANDLE(pDescHeap->GetGPUDescriptorHandleForHeapStart());
	auto rtvCpuDesc = CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
	auto dsvCpuDesc = CD3DX12_CPU_DESCRIPTOR_HANDLE(mDsvHeap->GetCPUDescriptorHandleForHeapStart());

	mShadowMap->BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuStart, EDescriptors::ES_Shadow, descSize),
		CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuStart, EDescriptors::ES_Shadow, descSize),
		CD3DX12_CPU_DESCRIPTOR_HANDLE(dsvCpuStart, EDsvHeapLayout::EShadow, dsvDescSize)
	);

	mGBuffer->BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuStart, EDescriptors::ES_Color, descSize),
		CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuStart, EDescriptors::ES_Color, descSize),
		CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvCpuStart, ERtvHeapLayout::EColor, rtvDescSize),
		descSize, rtvDescSize,
		mDepthStencilBuffer.Get()
	);

	mDxrShadowMap->BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuStart, EDescriptors::ES_DxrShadow0, descSize),
		CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuStart, EDescriptors::ES_DxrShadow0, descSize),
		CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuStart, EDescriptors::EU_DxrShadow0, descSize),
		CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuStart, EDescriptors::EU_DxrShadow0, descSize),
		descSize
	);

	mSsao->BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuStart, EDescriptors::ES_Ambient0, descSize),
		CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuStart, EDescriptors::ES_Ambient0, descSize),
		CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvCpuStart, ERtvHeapLayout::EAmbient0, rtvDescSize),
		descSize, rtvDescSize
	);

	mRtao->BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuStart, EDescriptors::ES_DxrAmbient0, descSize),
		CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuStart, EDescriptors::ES_DxrAmbient0, descSize),
		CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuStart, EDescriptors::EU_DxrAmbient0, descSize),
		CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuStart, EDescriptors::EU_DxrAmbient0, descSize),
		descSize
	);

	return true;
}

bool Renderer::BuildPSOs() {
	std::vector<D3D12_INPUT_ELEMENT_DESC> inputLayout = {
		{ "POSITION",	0, DXGI_FORMAT_R32G32B32_FLOAT,			0, 0,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "NORMAL",		0, DXGI_FORMAT_R32G32B32_FLOAT,			0, 12,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TEXCOORD",	0, DXGI_FORMAT_R32G32_FLOAT,			0, 24,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 },
		{ "TANGENT",	0, DXGI_FORMAT_R32G32B32_FLOAT,			0, 32,	D3D12_INPUT_CLASSIFICATION_PER_VERTEX_DATA, 0 }
	};

	D3D12_GRAPHICS_PIPELINE_STATE_DESC defaultPsoDesc;
	ZeroMemory(&defaultPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	defaultPsoDesc.InputLayout = { inputLayout.data(), static_cast<UINT>(inputLayout.size()) };
	defaultPsoDesc.pRootSignature = mRootSignatures["raster"].Get();
	defaultPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	defaultPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	defaultPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	defaultPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	defaultPsoDesc.SampleMask = UINT_MAX;
	defaultPsoDesc.SampleDesc.Count = 1;
	defaultPsoDesc.SampleDesc.Quality = 0;
	defaultPsoDesc.DSVFormat = DepthStencilFormat;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC quadPsoDesc = defaultPsoDesc;
	quadPsoDesc.InputLayout = { nullptr, 0 };
	quadPsoDesc.NumRenderTargets = 1;
	quadPsoDesc.DepthStencilState.DepthEnable = FALSE;
	quadPsoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;

	D3D12_GRAPHICS_PIPELINE_STATE_DESC backBufferPsoDesc = quadPsoDesc;
	{
		auto vs = mShaderManager->GetDxcShader("backBufferVS");
		auto ps = mShaderManager->GetDxcShader("backBufferPS");
		backBufferPsoDesc.VS = {
			reinterpret_cast<BYTE*>(vs->GetBufferPointer()),
			vs->GetBufferSize()
		};
		backBufferPsoDesc.PS = {
			reinterpret_cast<BYTE*>(ps->GetBufferPointer()),
			ps->GetBufferSize()
		};
	}	
	backBufferPsoDesc.RTVFormats[0] = BackBufferFormat;
	CheckHResult(md3dDevice->CreateGraphicsPipelineState(&backBufferPsoDesc, IID_PPV_ARGS(&mPSOs["backBuffer"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC gizmoPsoDesc = quadPsoDesc;
	{
		auto vs = mShaderManager->GetDxcShader("gizmoVS");
		auto ps = mShaderManager->GetDxcShader("gizmoPS");
		gizmoPsoDesc.VS = {
			reinterpret_cast<BYTE*>(vs->GetBufferPointer()),
			vs->GetBufferSize()
		};
		gizmoPsoDesc.PS = {
			reinterpret_cast<BYTE*>(ps->GetBufferPointer()),
			ps->GetBufferSize()
		};
	}
	gizmoPsoDesc.DepthStencilState.DepthEnable = FALSE;
	gizmoPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
	gizmoPsoDesc.NumRenderTargets = 1;
	gizmoPsoDesc.RTVFormats[0] = BackBufferFormat;
	gizmoPsoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	CheckHResult(md3dDevice->CreateGraphicsPipelineState(&gizmoPsoDesc, IID_PPV_ARGS(&mPSOs["gizmo"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC shadowPsoDesc = defaultPsoDesc;
	{
		auto vs = mShaderManager->GetDxcShader("shadowVS");
		auto ps = mShaderManager->GetDxcShader("shadowPS");
		shadowPsoDesc.VS = {
			reinterpret_cast<BYTE*>(vs->GetBufferPointer()),
			vs->GetBufferSize()
		};
		shadowPsoDesc.PS = {
			reinterpret_cast<BYTE*>(ps->GetBufferPointer()),
			ps->GetBufferSize()
		};
	}
	shadowPsoDesc.NumRenderTargets = 0;
	shadowPsoDesc.RasterizerState.DepthBias = 100000;
	shadowPsoDesc.RasterizerState.SlopeScaledDepthBias = 1.0f;
	shadowPsoDesc.RasterizerState.DepthBiasClamp = 0.1f;
	CheckHResult(md3dDevice->CreateGraphicsPipelineState(&shadowPsoDesc, IID_PPV_ARGS(&mPSOs["shadow"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC gbufferPsoDesc = defaultPsoDesc;
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
	gbufferPsoDesc.NumRenderTargets = GBuffer::NumRenderTargets;
	gbufferPsoDesc.RTVFormats[0] = BackBufferFormat;
	gbufferPsoDesc.RTVFormats[1] = BackBufferFormat;
	gbufferPsoDesc.RTVFormats[2] = GBuffer::NormalDepthMapFormat;
	gbufferPsoDesc.RTVFormats[3] = GBuffer::SpecularMapFormat;
	gbufferPsoDesc.RTVFormats[4] = GBuffer::VelocityMapFormat;
	gbufferPsoDesc.RTVFormats[5] = GBuffer::ReprojectedNormalDepthMapFormat;
	CheckHResult(md3dDevice->CreateGraphicsPipelineState(&gbufferPsoDesc, IID_PPV_ARGS(&mPSOs["gbuffer"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC debugPsoDesc = quadPsoDesc;
	{
		auto vs = mShaderManager->GetDxcShader("debugVS");
		auto ps = mShaderManager->GetDxcShader("debugPS");
		debugPsoDesc.VS = {
			reinterpret_cast<BYTE*>(vs->GetBufferPointer()),
			vs->GetBufferSize()
		};
		debugPsoDesc.PS = {
			reinterpret_cast<BYTE*>(ps->GetBufferPointer()),
			ps->GetBufferSize()
		};
	}
	debugPsoDesc.RTVFormats[0] = BackBufferFormat;
	CheckHResult(md3dDevice->CreateGraphicsPipelineState(&debugPsoDesc, IID_PPV_ARGS(&mPSOs["debug"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC dxrBackBufferPsoDesc = quadPsoDesc;
	{
		auto vs = mShaderManager->GetDxcShader("dxrBackBufferVS");
		auto ps = mShaderManager->GetDxcShader("dxrBackBufferPS");
		dxrBackBufferPsoDesc.VS = {
			reinterpret_cast<BYTE*>(vs->GetBufferPointer()),
			vs->GetBufferSize()
		};
		dxrBackBufferPsoDesc.PS = {
			reinterpret_cast<BYTE*>(ps->GetBufferPointer()),
			ps->GetBufferSize()
		};
	}
	dxrBackBufferPsoDesc.RTVFormats[0] = BackBufferFormat;
	CheckHResult(md3dDevice->CreateGraphicsPipelineState(&dxrBackBufferPsoDesc, IID_PPV_ARGS(&mPSOs["dxrBackBuffer"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC ssaoPsoDesc = quadPsoDesc;
	ssaoPsoDesc.pRootSignature = mRootSignatures["ssao"].Get();
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
	ssaoPsoDesc.RTVFormats[0] = Ssao::AmbientMapFormat;
	CheckHResult(md3dDevice->CreateGraphicsPipelineState(&ssaoPsoDesc, IID_PPV_ARGS(&mPSOs["ssao"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC ssaoBlurPsoDesc = quadPsoDesc;
	ssaoBlurPsoDesc.pRootSignature = mRootSignatures["gaussianBlur"].Get();
	{
		auto vs = mShaderManager->GetDxcShader("gaussianBlurVS");
		auto ps = mShaderManager->GetDxcShader("gaussianBlurPS");
		ssaoBlurPsoDesc.VS = {
			reinterpret_cast<BYTE*>(vs->GetBufferPointer()),
			vs->GetBufferSize()
		};
		ssaoBlurPsoDesc.PS = {
			reinterpret_cast<BYTE*>(ps->GetBufferPointer()),
			ps->GetBufferSize()
		};
	}
	ssaoBlurPsoDesc.RTVFormats[0] = Ssao::AmbientMapFormat;
	CheckHResult(md3dDevice->CreateGraphicsPipelineState(&ssaoBlurPsoDesc, IID_PPV_ARGS(&mPSOs["ssaoBlur"])));

	D3D12_COMPUTE_PIPELINE_STATE_DESC horzBlurPsoDesc = {};
	horzBlurPsoDesc.pRootSignature = mRootSignatures["csBlur"].Get();
	{
		auto cs = mShaderManager->GetDxcShader("horzGaussianBlurCS");
		horzBlurPsoDesc.CS = {
			reinterpret_cast<BYTE*>(cs->GetBufferPointer()),
			cs->GetBufferSize()
		};
	}
	horzBlurPsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	CheckHResult(md3dDevice->CreateComputePipelineState(&horzBlurPsoDesc, IID_PPV_ARGS(&mPSOs["horzBlur"])));

	D3D12_COMPUTE_PIPELINE_STATE_DESC vertBlurPsoDesc = {};
	vertBlurPsoDesc.pRootSignature = mRootSignatures["csBlur"].Get();
	{
		auto cs = mShaderManager->GetDxcShader("vertGaussianBlurCS");
		vertBlurPsoDesc.CS = {
			reinterpret_cast<BYTE*>(cs->GetBufferPointer()),
			cs->GetBufferSize()
		};
	}
	vertBlurPsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	CheckHResult(md3dDevice->CreateComputePipelineState(&vertBlurPsoDesc, IID_PPV_ARGS(&mPSOs["vertBlur"])));

	D3D12_COMPUTE_PIPELINE_STATE_DESC gtRtaoPsoDesc = {};
	gtRtaoPsoDesc.pRootSignature = mRootSignatures["gtRtao"].Get();
	{
		auto cs = mShaderManager->GetDxcShader("gtAoCS");
		gtRtaoPsoDesc.CS = {
			reinterpret_cast<BYTE*>(cs->GetBufferPointer()),
			cs->GetBufferSize()
		};
	}
	gtRtaoPsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	CheckHResult(md3dDevice->CreateComputePipelineState(&gtRtaoPsoDesc, IID_PPV_ARGS(&mPSOs["gtRtao"])));

	D3D12_COMPUTE_PIPELINE_STATE_DESC tsppRprojPsoDesc = {};
	tsppRprojPsoDesc.pRootSignature = mRootSignatures["tspp_reproj"].Get();
	{
		auto cs = mShaderManager->GetDxcShader("tsppReprojCS");
		tsppRprojPsoDesc.CS = {
			reinterpret_cast<BYTE*>(cs->GetBufferPointer()),
			cs->GetBufferSize()
		};
	}
	tsppRprojPsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	CheckHResult(md3dDevice->CreateComputePipelineState(&tsppRprojPsoDesc, IID_PPV_ARGS(&mPSOs["tspp_reproj"])));

	D3D12_COMPUTE_PIPELINE_STATE_DESC calcPartialDerivativesPsoDesc = {};
	calcPartialDerivativesPsoDesc.pRootSignature = mRootSignatures["partialDerivatives"].Get();
	{
		auto cs = mShaderManager->GetDxcShader("partialDerivativesCS");
		calcPartialDerivativesPsoDesc.CS = {
			reinterpret_cast<BYTE*>(cs->GetBufferPointer()),
			cs->GetBufferSize()
		};
	}
	calcPartialDerivativesPsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	CheckHResult(md3dDevice->CreateComputePipelineState(&calcPartialDerivativesPsoDesc, IID_PPV_ARGS(&mPSOs["partialDerivatives"])));

	D3D12_COMPUTE_PIPELINE_STATE_DESC calcLocalMeanVariancePsoDesc = {};
	calcLocalMeanVariancePsoDesc.pRootSignature = mRootSignatures["localMeanVariance"].Get();
	{
		auto cs = mShaderManager->GetDxcShader("calcLocalMeanVarianceCS");
		calcLocalMeanVariancePsoDesc.CS = {
			reinterpret_cast<BYTE*>(cs->GetBufferPointer()),
			cs->GetBufferSize()
		};
	}
	calcLocalMeanVariancePsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	CheckHResult(md3dDevice->CreateComputePipelineState(&calcLocalMeanVariancePsoDesc, IID_PPV_ARGS(&mPSOs["localMeanVariance"])));

	D3D12_COMPUTE_PIPELINE_STATE_DESC fillInCheckerboardPsoDesc = {};
	fillInCheckerboardPsoDesc.pRootSignature = mRootSignatures["fillInCheckerboard"].Get();
	{
		auto cs = mShaderManager->GetDxcShader("fillInCheckerboardCS");
		fillInCheckerboardPsoDesc.CS = {
			reinterpret_cast<BYTE*>(cs->GetBufferPointer()),
			cs->GetBufferSize()
		};
	}
	fillInCheckerboardPsoDesc.Flags = D3D12_PIPELINE_STATE_FLAG_NONE;
	CheckHResult(md3dDevice->CreateComputePipelineState(&fillInCheckerboardPsoDesc, IID_PPV_ARGS(&mPSOs["fillInCheckerboard"])));

	return true;
}

bool Renderer::BuildRenderItems() {
	UINT count = 0;

	{
		auto sphereRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&sphereRitem->World, XMMatrixTranslation(0.0f, 1.75f, 0.0f));
		sphereRitem->ObjSBIndex = count++;
		sphereRitem->Geo = mGeometries["sphere"].get();
		sphereRitem->Mat = mMaterials["red"].get();
		sphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		sphereRitem->IndexCount = sphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		sphereRitem->StartIndexLocation = sphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		sphereRitem->BaseVertexLocation = sphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
		mRitems[ERenderTypes::EOpaque].push_back(sphereRitem.get());
		mAllRitems.push_back(std::move(sphereRitem));
	}
	{
		auto sphereRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&sphereRitem->World, XMMatrixTranslation(1.75f, 0.0f, 0.0f));
		sphereRitem->ObjSBIndex = count++;
		sphereRitem->Geo = mGeometries["sphere"].get();
		sphereRitem->Mat = mMaterials["green"].get();
		sphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		sphereRitem->IndexCount = sphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		sphereRitem->StartIndexLocation = sphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		sphereRitem->BaseVertexLocation = sphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
		mRitems[ERenderTypes::EOpaque].push_back(sphereRitem.get());
		mAllRitems.push_back(std::move(sphereRitem));
	}
	{
		auto sphereRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&sphereRitem->World, XMMatrixTranslation(-1.75f, 0.0f, 0.0f));
		sphereRitem->ObjSBIndex = count++;
		sphereRitem->Geo = mGeometries["sphere"].get();
		sphereRitem->Mat = mMaterials["blue"].get();
		sphereRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		sphereRitem->IndexCount = sphereRitem->Geo->DrawArgs["sphere"].IndexCount;
		sphereRitem->StartIndexLocation = sphereRitem->Geo->DrawArgs["sphere"].StartIndexLocation;
		sphereRitem->BaseVertexLocation = sphereRitem->Geo->DrawArgs["sphere"].BaseVertexLocation;
		mRitems[ERenderTypes::EOpaque].push_back(sphereRitem.get());
		mAllRitems.push_back(std::move(sphereRitem));
	}
	{
		auto gridRitem = std::make_unique<RenderItem>();
		XMStoreFloat4x4(&gridRitem->World, XMMatrixTranslation(0.0f, -1.25f, 0.0f));
		gridRitem->ObjSBIndex = count++;
		gridRitem->Geo = mGeometries["grid"].get();
		gridRitem->Mat = mMaterials["white"].get();
		gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
		gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
		gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
		mRitems[ERenderTypes::EOpaque].push_back(gridRitem.get());
		mAllRitems.push_back(std::move(gridRitem));
	}

	return true;
}

bool Renderer::BuildBLAS() {
	for (const auto& pair : mGeometries) {
		auto geo = pair.second.get();

		D3D12_RAYTRACING_GEOMETRY_DESC geometryDesc = {};
		geometryDesc.Type = D3D12_RAYTRACING_GEOMETRY_TYPE_TRIANGLES;
		geometryDesc.Triangles.VertexFormat = DXGI_FORMAT_R32G32B32_FLOAT;
		geometryDesc.Triangles.VertexCount = static_cast<UINT>(geo->VertexBufferCPU->GetBufferSize() / sizeof(Vertex));
		geometryDesc.Triangles.VertexBuffer.StartAddress = geo->VertexBufferGPU->GetGPUVirtualAddress();
		geometryDesc.Triangles.VertexBuffer.StrideInBytes = sizeof(Vertex);
		geometryDesc.Triangles.IndexFormat = DXGI_FORMAT_R32_UINT;
		geometryDesc.Triangles.IndexCount = static_cast<UINT>(geo->IndexBufferCPU->GetBufferSize() / sizeof(std::uint32_t));
		geometryDesc.Triangles.IndexBuffer = geo->IndexBufferGPU->GetGPUVirtualAddress();
		geometryDesc.Triangles.Transform3x4 = 0;
		// Mark the geometry as opaque. 
		// PERFORMANCE TIP: mark geometry as opaque whenever applicable as it can enable important ray processing optimizations.
		// Note: When rays encounter opaque geometry an any hit shader will not be executed whether it is present or not.
		geometryDesc.Flags = D3D12_RAYTRACING_GEOMETRY_FLAG_OPAQUE;

		// Get the size requirements for the BLAS buffers
		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
		inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_BOTTOM_LEVEL;
		inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
		inputs.pGeometryDescs = &geometryDesc;
		inputs.NumDescs = 1;
		inputs.Flags = buildFlags;

		D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
		md3dDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);

		prebuildInfo.ScratchDataSizeInBytes = Align(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, prebuildInfo.ScratchDataSizeInBytes);
		prebuildInfo.ResultDataMaxSizeInBytes = Align(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, prebuildInfo.ResultDataMaxSizeInBytes);

		std::unique_ptr<AccelerationStructureBuffer> blas = std::make_unique<AccelerationStructureBuffer>();

		// Create the BLAS scratch buffer
		D3D12BufferCreateInfo bufferInfo(prebuildInfo.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
		bufferInfo.Alignment = std::max(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
		CheckIsValid(D3D12Util::CreateBuffer(md3dDevice.Get(), bufferInfo, blas->Scratch.GetAddressOf(), mInfoQueue.Get()));

		// Create the BLAS buffer
		bufferInfo.Size = prebuildInfo.ResultDataMaxSizeInBytes;
		bufferInfo.State = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
		CheckIsValid(D3D12Util::CreateBuffer(md3dDevice.Get(), bufferInfo, blas->Result.GetAddressOf(), mInfoQueue.Get()));

		// Describe and build the bottom level acceleration structure
		D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
		buildDesc.Inputs = inputs;
		buildDesc.ScratchAccelerationStructureData = blas->Scratch->GetGPUVirtualAddress();
		buildDesc.DestAccelerationStructureData = blas->Result->GetGPUVirtualAddress();

		mCommandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

		mBLASs[geo->Name] = std::move(blas);
	}

	// Wait for the BLAS build to complete
	std::vector<ID3D12Resource*> resources;
	for (const auto& pair : mBLASs) {
		resources.push_back(mBLASs[pair.first]->Result.Get());
	}
	D3D12Util::UavBarriers(mCommandList.Get(), resources.data(), resources.size());

	return true;
}

bool Renderer::BuildTLAS() {
	std::vector<D3D12_RAYTRACING_INSTANCE_DESC> instanceDescs;

	// Describe the TLAS geometry instance(s)
	{
		D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
		instanceDesc.InstanceID = instanceDescs.size();
		instanceDesc.InstanceContributionToHitGroupIndex = 0;
		instanceDesc.InstanceMask = 0xFF;
		instanceDesc.Transform[0][0] = instanceDesc.Transform[1][1] = instanceDesc.Transform[2][2] = 1.0f;
		instanceDesc.Transform[1][3] = 1.75f;
		instanceDesc.AccelerationStructure = mBLASs["sphere"]->Result->GetGPUVirtualAddress();
		instanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE;
		instanceDescs.push_back(instanceDesc);
	}
	{
		D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
		instanceDesc.InstanceID = instanceDescs.size();
		instanceDesc.InstanceContributionToHitGroupIndex = 0;
		instanceDesc.InstanceMask = 0xFF;
		instanceDesc.Transform[0][0] = instanceDesc.Transform[1][1] = instanceDesc.Transform[2][2] = 1.0f;
		instanceDesc.Transform[0][3] = 1.75f;
		instanceDesc.AccelerationStructure = mBLASs["sphere"]->Result->GetGPUVirtualAddress();
		instanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE;
		instanceDescs.push_back(instanceDesc);
	}
	{
		D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
		instanceDesc.InstanceID = instanceDescs.size();
		instanceDesc.InstanceContributionToHitGroupIndex = 0;
		instanceDesc.InstanceMask = 0xFF;
		instanceDesc.Transform[0][0] = instanceDesc.Transform[1][1] = instanceDesc.Transform[2][2] = 1.0f;
		instanceDesc.Transform[0][3] = -1.75f;
		instanceDesc.AccelerationStructure = mBLASs["sphere"]->Result->GetGPUVirtualAddress();
		instanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE;
		instanceDescs.push_back(instanceDesc);
	}
	{
		D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
		instanceDesc.InstanceID = instanceDescs.size();
		instanceDesc.InstanceContributionToHitGroupIndex = 0;
		instanceDesc.InstanceMask = 0xFF;
		instanceDesc.Transform[0][0] = instanceDesc.Transform[1][1] = instanceDesc.Transform[2][2] = 1.0f;
		instanceDesc.Transform[1][3] = -1.25f;
		instanceDesc.AccelerationStructure = mBLASs["grid"]->Result->GetGPUVirtualAddress();
		instanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE;
		instanceDescs.push_back(instanceDesc);
	}

	// Create the TLAS instance buffer
	D3D12BufferCreateInfo instanceBufferInfo;
	instanceBufferInfo.Size = instanceDescs.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC);
	instanceBufferInfo.HeapType = D3D12_HEAP_TYPE_UPLOAD;
	instanceBufferInfo.Flags = D3D12_RESOURCE_FLAG_NONE;
	instanceBufferInfo.State = D3D12_RESOURCE_STATE_GENERIC_READ;
	CheckIsValid(D3D12Util::CreateBuffer(md3dDevice.Get(), instanceBufferInfo, mTLAS->InstanceDesc.GetAddressOf(), mInfoQueue.Get()));

	// Copy the instance data to the buffer
	void* pData;
	CheckHResult(mTLAS->InstanceDesc->Map(0, nullptr, &pData));
	std::memcpy(pData, instanceDescs.data(), instanceDescs.size() * sizeof(D3D12_RAYTRACING_INSTANCE_DESC));
	mTLAS->InstanceDesc->Unmap(0, nullptr);

	// Get the size requirements for the TLAS buffers
	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAGS buildFlags = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BUILD_FLAG_PREFER_FAST_TRACE;
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_INPUTS inputs = {};
	inputs.Type = D3D12_RAYTRACING_ACCELERATION_STRUCTURE_TYPE_TOP_LEVEL;
	inputs.DescsLayout = D3D12_ELEMENTS_LAYOUT_ARRAY;
	inputs.InstanceDescs = mTLAS->InstanceDesc->GetGPUVirtualAddress();
	inputs.NumDescs = static_cast<UINT>(instanceDescs.size());
	inputs.Flags = buildFlags;

	D3D12_RAYTRACING_ACCELERATION_STRUCTURE_PREBUILD_INFO prebuildInfo = {};
	md3dDevice->GetRaytracingAccelerationStructurePrebuildInfo(&inputs, &prebuildInfo);

	prebuildInfo.ResultDataMaxSizeInBytes = Align(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, prebuildInfo.ResultDataMaxSizeInBytes);
	prebuildInfo.ScratchDataSizeInBytes = Align(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, prebuildInfo.ScratchDataSizeInBytes);

	// Set TLAS size
	mTLAS->ResultDataMaxSizeInBytes = prebuildInfo.ResultDataMaxSizeInBytes;

	// Create TLAS sratch buffer
	D3D12BufferCreateInfo bufferInfo(prebuildInfo.ScratchDataSizeInBytes, D3D12_RESOURCE_FLAG_ALLOW_UNORDERED_ACCESS, D3D12_RESOURCE_STATE_COMMON);
	bufferInfo.Alignment = std::max(D3D12_RAYTRACING_ACCELERATION_STRUCTURE_BYTE_ALIGNMENT, D3D12_DEFAULT_RESOURCE_PLACEMENT_ALIGNMENT);
	CheckIsValid(D3D12Util::CreateBuffer(md3dDevice.Get(), bufferInfo, mTLAS->Scratch.GetAddressOf(), mInfoQueue.Get()));

	// Create the TLAS buffer
	bufferInfo.Size = prebuildInfo.ResultDataMaxSizeInBytes;
	bufferInfo.State = D3D12_RESOURCE_STATE_RAYTRACING_ACCELERATION_STRUCTURE;
	CheckIsValid(D3D12Util::CreateBuffer(md3dDevice.Get(), bufferInfo, mTLAS->Result.GetAddressOf(), mInfoQueue.Get()));

	// Describe and build the TLAS
	D3D12_BUILD_RAYTRACING_ACCELERATION_STRUCTURE_DESC buildDesc = {};
	buildDesc.Inputs = inputs;
	buildDesc.ScratchAccelerationStructureData = mTLAS->Scratch->GetGPUVirtualAddress();
	buildDesc.DestAccelerationStructureData = mTLAS->Result->GetGPUVirtualAddress();

	mCommandList->BuildRaytracingAccelerationStructure(&buildDesc, 0, nullptr);

	// Wait for the TLAS build to complete
	D3D12Util::UavBarrier(mCommandList.Get(), mTLAS->Result.Get());

	return true;
}

bool Renderer::BuildDXRPSOs() {
	// Default
	{
		// Subobjects need to be associated with DXIL exports (i.e. shaders) either by way of default or explicit associations.
		// Default association applies to every exported shader entrypoint that doesn't have any of the same type of subobject associated with it.
		// This simple sample utilizes default shader association except for local root signature subobject
		// which has an explicit association specified purely for demonstration purposes.
		CD3DX12_STATE_OBJECT_DESC defaultDxrPso = { D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };

		auto shadowRayLib = defaultDxrPso.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
		auto shadowRayShader = mShaderManager->GetDxcShader("shadowRay");
		D3D12_SHADER_BYTECODE shadowRayLibDxil = CD3DX12_SHADER_BYTECODE(shadowRayShader->GetBufferPointer(), shadowRayShader->GetBufferSize());
		shadowRayLib->SetDXILLibrary(&shadowRayLibDxil);
		LPCWSTR shadowRayExports[] = { L"ShadowRayGen", L"ShadowClosestHit", L"ShadowMiss" };
		shadowRayLib->DefineExports(shadowRayExports);

		auto shadowHitGroup = defaultDxrPso.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
		shadowHitGroup->SetClosestHitShaderImport(L"ShadowClosestHit");
		shadowHitGroup->SetHitGroupExport(L"ShadowHitGroup");
		shadowHitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

		auto shaderConfig = defaultDxrPso.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
		UINT payloadSize = sizeof(XMFLOAT4);	// for pixel color
		UINT attribSize = sizeof(XMFLOAT2);		// for barycentrics
		shaderConfig->Config(payloadSize, attribSize);

		//auto localRootSig = dxrPso.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
		//localRootSig->SetRootSignature(mRootSignatures["dxr_local"].Get());
		//{
		//	auto rootSigAssociation = dxrPso.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
		//	rootSigAssociation->SetSubobjectToAssociate(*localRootSig);
		//	rootSigAssociation->AddExport(L"HitGroup");
		//}

		auto glbalRootSig = defaultDxrPso.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
		glbalRootSig->SetRootSignature(mRootSignatures["dxr_global"].Get());

		auto pipelineConfig = defaultDxrPso.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
		UINT maxRecursionDepth = 1;
		pipelineConfig->Config(maxRecursionDepth);

		CheckHResult(md3dDevice->CreateStateObject(defaultDxrPso, IID_PPV_ARGS(&mDXRPSOs["default"])));
		CheckHResult(mDXRPSOs["default"]->QueryInterface(IID_PPV_ARGS(&mDXRPSOProps["defaultProps"])));
	}
	// RTAO
	{
		CD3DX12_STATE_OBJECT_DESC rtaoDxrPso = { D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };

		auto rtaoLib = rtaoDxrPso.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
		auto rtaoShader = mShaderManager->GetDxcShader("rtao");
		D3D12_SHADER_BYTECODE rtaoLibDxil = CD3DX12_SHADER_BYTECODE(rtaoShader->GetBufferPointer(), rtaoShader->GetBufferSize());
		rtaoLib->SetDXILLibrary(&rtaoLibDxil);
		LPCWSTR rtaoExports[] = { L"RtaoRayGen", L"RtaoClosestHit", L"RtaoMiss" };
		rtaoLib->DefineExports(rtaoExports);

		auto rtaoHitGroup = rtaoDxrPso.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
		rtaoHitGroup->SetClosestHitShaderImport(L"RtaoClosestHit");
		rtaoHitGroup->SetHitGroupExport(L"RtaoHitGroup");
		rtaoHitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

		auto shaderConfig = rtaoDxrPso.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
		UINT payloadSize = sizeof(XMFLOAT3) + 4;
		UINT attribSize = sizeof(XMFLOAT2);
		shaderConfig->Config(payloadSize, attribSize);

		auto glbalRootSig = rtaoDxrPso.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
		glbalRootSig->SetRootSignature(mRootSignatures["rtao_global"].Get());

		auto pipelineConfig = rtaoDxrPso.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
		UINT maxRecursionDepth = 1;
		pipelineConfig->Config(maxRecursionDepth);

		CheckHResult(md3dDevice->CreateStateObject(rtaoDxrPso, IID_PPV_ARGS(&mDXRPSOs["rtao"])));
		CheckHResult(mDXRPSOs["rtao"]->QueryInterface(IID_PPV_ARGS(&mDXRPSOProps["rtaoProps"])));
	}

	return true;
}

bool Renderer::BuildShaderTables() {
	UINT shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

	{
		const auto props = mDXRPSOProps["defaultProps"];
		void* shadowRayGenShaderIdentifier = props->GetShaderIdentifier(L"ShadowRayGen");
		void* shadowMissShaderIdentifier = props->GetShaderIdentifier(L"ShadowMiss");
		void* shadowHitGroupShaderIdentifier = props->GetShaderIdentifier(L"ShadowHitGroup");

		ShaderTable shadowRayGenShaderTable(md3dDevice.Get(), 1, shaderIdentifierSize);
		CheckIsValid(shadowRayGenShaderTable.Initialze());
		shadowRayGenShaderTable.push_back(ShaderRecord(shadowRayGenShaderIdentifier, shaderIdentifierSize));
		mShaderTables["shadowRayGen"] = shadowRayGenShaderTable.GetResource();

		ShaderTable shadowMissShaderTable(md3dDevice.Get(), 1, shaderIdentifierSize);
		CheckIsValid(shadowMissShaderTable.Initialze());
		shadowMissShaderTable.push_back(ShaderRecord(shadowMissShaderIdentifier, shaderIdentifierSize));
		mShaderTables["shadowMiss"] = shadowMissShaderTable.GetResource();

		ShaderTable shadowHitGroupTable(md3dDevice.Get(), 1, shaderIdentifierSize);
		CheckIsValid(shadowHitGroupTable.Initialze());
		shadowHitGroupTable.push_back(ShaderRecord(shadowHitGroupShaderIdentifier, shaderIdentifierSize));
		mShaderTables["shadowHitGroup"] = shadowHitGroupTable.GetResource();
	}
	{
		const auto props = mDXRPSOProps["rtaoProps"].Get();
		void* rtaoRayGenShaderIdentifier = props->GetShaderIdentifier(L"RtaoRayGen");
		void* rtaoMissShaderIdentifier = props->GetShaderIdentifier(L"RtaoMiss");
		void* rtaoHitGroupShaderIdentifier = props->GetShaderIdentifier(L"RtaoHitGroup");

		ShaderTable rtaoRayGenShaderTable(md3dDevice.Get(), 1, shaderIdentifierSize);
		CheckIsValid(rtaoRayGenShaderTable.Initialze());
		rtaoRayGenShaderTable.push_back(ShaderRecord(rtaoRayGenShaderIdentifier, shaderIdentifierSize));
		mShaderTables["rtaoRayGen"] = rtaoRayGenShaderTable.GetResource();

		ShaderTable rtaoMissShaderTable(md3dDevice.Get(), 1, shaderIdentifierSize);
		CheckIsValid(rtaoMissShaderTable.Initialze());
		rtaoMissShaderTable.push_back(ShaderRecord(rtaoMissShaderIdentifier, shaderIdentifierSize));
		mShaderTables["rtaoMiss"] = rtaoMissShaderTable.GetResource();

		ShaderTable rtaoHitGroupTable(md3dDevice.Get(), 1, shaderIdentifierSize);
		CheckIsValid(rtaoHitGroupTable.Initialze());
		rtaoHitGroupTable.push_back(ShaderRecord(rtaoHitGroupShaderIdentifier, shaderIdentifierSize));
		mShaderTables["rtaoHitGroup"] = rtaoHitGroupTable.GetResource();
	}

	return true;
}

bool Renderer::UpdateObjectCB(const GameTimer& gt) {
	auto& currObjectSB = mCurrFrameResource->ObjectSB;

	for (auto& e : mAllRitems) {
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if (e->NumFramesDirty > 0) {
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectData objData;
			objData.PrevWorld = e->PrevWolrd;
			XMStoreFloat4x4(&objData.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objData.TexTransform, XMMatrixTranspose(texTransform));
			objData.GeometryIndex = e->Geo->GeometryIndex;
			objData.MaterialIndex = e->Mat->MatSBIndex;

			e->PrevWolrd = objData.World;

			currObjectSB.CopyData(e->ObjSBIndex, objData);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}

	return true;
}

bool Renderer::UpdatePassCB(const GameTimer& gt) {
	XMMATRIX view = mCamera->GetViewMatrix();
	XMMATRIX unitView = mCamera->GetViewMatrix(true);
	XMMATRIX proj = mCamera->GetProjectionMatrix();
	XMMATRIX viewProj = XMMatrixMultiply(view, proj);
	XMMATRIX unitViewProj = XMMatrixMultiply(unitView, proj);

	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f
	);
	XMMATRIX viewProjTex = XMMatrixMultiply(viewProj, T);

	mMainPassCB->PrevViewProj = mMainPassCB->ViewProj;
	XMStoreFloat4x4(&mMainPassCB->View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB->InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB->Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB->InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB->ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB->InvViewProj, XMMatrixTranspose(invViewProj));
	XMStoreFloat4x4(&mMainPassCB->UnitViewProj, XMMatrixTranspose(unitViewProj));
	XMStoreFloat4x4(&mMainPassCB->ViewProjTex, XMMatrixTranspose(viewProjTex));
	mMainPassCB->EyePosW = mCamera->GetCameraPosition();
	mMainPassCB->AmbientLight = { 0.3f, 0.3f, 0.42f, 1.0f };
	mMainPassCB->Lights[0].Strength = { 0.4f, 0.4f, 0.4f };
	mMainPassCB->Lights[0].Direction = mLightDir;
	mMainPassCB->Lights[0].FalloffStart = 1.0f;
	mMainPassCB->Lights[0].FalloffEnd = 10.0f;
	mMainPassCB->Lights[0].SpotPower = 64.0f;

	auto& currPassCB = mCurrFrameResource->PassCB;
	currPassCB.CopyData(0, *mMainPassCB);

	return true;
}

bool Renderer::UpdateShadowPassCB(const GameTimer& gt) {
	XMVECTOR lightDir = XMLoadFloat3(&mLightDir);
	XMVECTOR lightPos = -2.0f * mSceneBounds.Radius * lightDir;
	XMVECTOR targetPos = XMLoadFloat3(&mSceneBounds.Center);
	XMVECTOR lightUp = XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);
	XMMATRIX lightView = XMMatrixLookAtLH(lightPos, targetPos, lightUp);

	// Transform bounding sphere to light space.
	XMFLOAT3 sphereCenterLS;
	XMStoreFloat3(&sphereCenterLS, XMVector3TransformCoord(targetPos, lightView));

	// Ortho frustum in light space encloses scene.
	float l = sphereCenterLS.x - mSceneBounds.Radius;
	float b = sphereCenterLS.y - mSceneBounds.Radius;
	float n = sphereCenterLS.z - mSceneBounds.Radius;
	float r = sphereCenterLS.x + mSceneBounds.Radius;
	float t = sphereCenterLS.y + mSceneBounds.Radius;
	float f = sphereCenterLS.z + mSceneBounds.Radius;

	XMMATRIX lightProj = XMMatrixOrthographicOffCenterLH(l, r, b, t, n, f);

	// Transform NDC space [-1 , +1]^2 to texture space [0, 1]^2
	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f
	);

	XMMATRIX S = lightView * lightProj * T;
	XMStoreFloat4x4(&mMainPassCB->ShadowTransform, XMMatrixTranspose(S));

	XMMATRIX viewProj = XMMatrixMultiply(lightView, lightProj);
	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(lightView), lightView);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(lightProj), lightProj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMStoreFloat4x4(&mShadowPassCB->View, XMMatrixTranspose(lightView));
	XMStoreFloat4x4(&mShadowPassCB->InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mShadowPassCB->Proj, XMMatrixTranspose(lightProj));
	XMStoreFloat4x4(&mShadowPassCB->InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mShadowPassCB->ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mShadowPassCB->InvViewProj, XMMatrixTranspose(invViewProj));
	XMStoreFloat3(&mShadowPassCB->EyePosW, lightPos);

	auto& currPassCB = mCurrFrameResource->PassCB;
	currPassCB.CopyData(1, *mShadowPassCB);

	return true;
}

bool Renderer::UpdateMaterialCB(const GameTimer& gt) {
	auto& currMaterialSB = mCurrFrameResource->MaterialSB;
	for (auto& e : mMaterials) {
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if (mat->NumFramesDirty > 0) {
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialData matData;
			matData.DiffuseAlbedo = mat->DiffuseAlbedo;
			matData.FresnelR0 = mat->FresnelR0;
			matData.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matData.MatTransform, XMMatrixTranspose(matTransform));

			currMaterialSB.CopyData(mat->MatSBIndex, matData);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}

	return true;
}

bool Renderer::UpdateBlurPassCB(const GameTimer& gt) {
	BlurConstants blurCB;
	blurCB.Proj = mMainPassCB->Proj;
	blurCB.BlurWeights[0] = mBlurWeights[0];
	blurCB.BlurWeights[1] = mBlurWeights[1];
	blurCB.BlurWeights[2] = mBlurWeights[2];
	blurCB.BlurRadius = 5;

	auto& currBlurCB = mCurrFrameResource->BlurCB;
	currBlurCB.CopyData(0, blurCB);

	return true;
}

bool Renderer::UpdateSsaoPassCB(const GameTimer& gt) {
	SsaoConstants ssaoCB;
	ssaoCB.View = mMainPassCB->View;
	ssaoCB.InvView = mMainPassCB->InvView;
	ssaoCB.Proj = mMainPassCB->Proj;
	ssaoCB.InvProj = mMainPassCB->InvProj;

	XMMATRIX P = mCamera->GetProjectionMatrix();
	// Transform NDC space [-1,+1]^2 to texture space [0,1]^2
	XMMATRIX T(
		0.5f, 0.0f, 0.0f, 0.0f,
		0.0f, -0.5f, 0.0f, 0.0f,
		0.0f, 0.0f, 1.0f, 0.0f,
		0.5f, 0.5f, 0.0f, 1.0f
	);
	XMStoreFloat4x4(&ssaoCB.ProjTex, XMMatrixTranspose(P * T));

	mSsao->GetOffsetVectors(ssaoCB.OffsetVectors);

	// Coordinates given in view space.
	ssaoCB.OcclusionRadius = mSsaoRadius;
	ssaoCB.OcclusionFadeStart = mSsaoFadeStart;
	ssaoCB.OcclusionFadeEnd = mSsaoFadeEnd;
	ssaoCB.SurfaceEpsilon = mSsaoEpsilon;

	auto& currSsaoCB = mCurrFrameResource->SsaoCB;
	currSsaoCB.CopyData(0, ssaoCB);

	return true;
}

bool Renderer::UpdateRtaoPassCB(const GameTimer& gt) {
	static UINT count = 0;
	static auto prev = mMainPassCB->View;

	RtaoConstants rtaoCB;
	rtaoCB.View = mMainPassCB->View;
	rtaoCB.InvView = mMainPassCB->InvView;
	rtaoCB.Proj = mMainPassCB->Proj;
	rtaoCB.InvProj = mMainPassCB->InvProj;
	
	// Coordinates given in view space.
	rtaoCB.OcclusionRadius = mRtaoRadius;
	rtaoCB.OcclusionFadeStart = mRtaoFadeStart;
	rtaoCB.OcclusionFadeEnd = mRtaoFadeEnd;
	rtaoCB.SurfaceEpsilon = mRtaoEpsilon;
	rtaoCB.FrameCount = count++;
	rtaoCB.SampleCount = mRtaoSampleCount;

	++mRtaoAccum;

	bool isEqual = true;
	for (int i = 0; i < 4; ++i) {
		bool quit = false;
		for (int j = 0; j < 4; ++j) {
			if (prev.m[i][j] != mMainPassCB->View.m[i][j]) {
				isEqual = false;
				quit = true;
				break;
			}
		}
		if (quit) break;
	}
	if (!isEqual) mRtaoAccum = 0;
	prev = mMainPassCB->View;

	auto& currRtaoCB = mCurrFrameResource->RtaoCB;
	currRtaoCB.CopyData(0, rtaoCB);

	return true;
}

bool Renderer::UpdateCrossBilateralFilterCB(const GameTimer& gt) {
	CrossBilateralFilterConstants filterCB;
	filterCB.DepthSigma = 1.0f;
	filterCB.DepthNumMantissaBits = D3D12Util::NumMantissaBitsInFloatFormat(16);

	auto& currFilterCB = mCurrFrameResource->CrossBilateralFilterCB;
	currFilterCB.CopyData(0, filterCB);

	return true;
}

bool Renderer::UpdateCalcMeanVarCB(const GameTimer& gt) {
	CalcLocalMeanVarianceConstants calcLocalMeanVarCB;

	calcLocalMeanVarCB.TextureDimension = { mRtao->Width(), mRtao->Height() };
	calcLocalMeanVarCB.KernelWidth = 9;
	calcLocalMeanVarCB.KernelRadius = 9 >> 1;
	calcLocalMeanVarCB.CheckerboardSamplingEnabled = bCheckerboardSamplingEnabled;
	bCheckerboardGenerateRaysForEvenPixels = !bCheckerboardGenerateRaysForEvenPixels;
	calcLocalMeanVarCB.EvenPixelActivated = bCheckerboardGenerateRaysForEvenPixels;
	calcLocalMeanVarCB.PixelStepY = bCheckerboardSamplingEnabled ? 2 : 1;

	auto& currLocalCalcMeanVarCB = mCurrFrameResource->CalcLocalMeanVarCB;
	currLocalCalcMeanVarCB.CopyData(0, calcLocalMeanVarCB);

	return true;
}

bool Renderer::Rasterize() {
	CheckIsValid(DrawShadowMap());
	CheckIsValid(DrawGBuffer());
	CheckIsValid(DrawSsao());
	CheckIsValid(DrawBackBuffer());

	return true;
}

bool Renderer::DrawRenderItems(const std::vector<RenderItem*>& ritems) { 
	// For each render item...
	for (size_t i = 0; i < ritems.size(); ++i) {
		auto& ri = ritems[i];

		mCommandList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		mCommandList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		mCommandList->IASetPrimitiveTopology(ri->PrimitiveType);

		mCommandList->SetGraphicsRoot32BitConstant(
			ERasterization::RootSignatureLayout::EC_Consts,
			ri->ObjSBIndex, 
			ERasterization::RootConstantsLayout::EInstanceID
		);

		mCommandList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}

	return true;
}

bool Renderer::DrawShadowMap() {
	CheckHResult(mCommandList->Reset(mCurrFrameResource->CmdListAlloc.Get(), mPSOs["shadow"].Get()));

	mCommandList->SetGraphicsRootSignature(mRootSignatures["raster"].Get());

	const auto pDeshHeap = mCbvSrvUavHeap.Get();
	ID3D12DescriptorHeap* descriptorHeaps[] = { pDeshHeap };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->RSSetViewports(1, &mShadowMap->Viewport());
	mCommandList->RSSetScissorRects(1, &mShadowMap->ScissorRect());

	const auto pShadowMap = mShadowMap->Resource();
	mCommandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			pShadowMap,
			D3D12_RESOURCE_STATE_DEPTH_READ,
			D3D12_RESOURCE_STATE_DEPTH_WRITE
		)
	);
	
	mCommandList->ClearDepthStencilView(mShadowMap->Dsv(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);
	mCommandList->OMSetRenderTargets(0, nullptr, false, &mShadowMap->Dsv());
	
	UINT passCBByteSize = D3D12Util::CalcConstantBufferByteSize(sizeof(PassConstants));
	D3D12_GPU_VIRTUAL_ADDRESS shadowPassCBAddress = mCurrFrameResource->PassCB.Resource()->GetGPUVirtualAddress() + 1 * passCBByteSize;
	mCommandList->SetGraphicsRootConstantBufferView(ERasterization::RootSignatureLayout::ECB_Pass, shadowPassCBAddress);

	const auto& objSBAddress = mCurrFrameResource->ObjectSB.Resource()->GetGPUVirtualAddress();
	mCommandList->SetGraphicsRootShaderResourceView(ERasterization::RootSignatureLayout::ESB_Object, objSBAddress);

	const auto& matSBAddress = mCurrFrameResource->MaterialSB.Resource()->GetGPUVirtualAddress();
	mCommandList->SetGraphicsRootShaderResourceView(ERasterization::RootSignatureLayout::ESB_Material, matSBAddress);
	
	CheckIsValid(DrawRenderItems(mRitems[ERenderTypes::EOpaque]));
	
	mCommandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			pShadowMap,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			D3D12_RESOURCE_STATE_DEPTH_READ
		)
	);

	CheckHResult(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	return true;
}

bool Renderer::DrawGBuffer() {
	CheckHResult(mCommandList->Reset(mCurrFrameResource->CmdListAlloc.Get(), mPSOs["gbuffer"].Get()));

	mCommandList->SetGraphicsRootSignature(mRootSignatures["raster"].Get());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvSrvUavHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	const auto pColorMap = mGBuffer->ColorMapResource();
	const auto pAlbedoMap = mGBuffer->AlbedoMapResource();
	const auto pNormalDepthMap = mGBuffer->NormalDepthMapResource();
	const auto pSpecularMap = mGBuffer->SpecularMapResource();
	const auto pVelocityMap = mGBuffer->VelocityMapResource();
	const auto pPrevNormalDepthMap = mGBuffer->ReprojectedNormalDepthMapResource();
	{
		D3D12_RESOURCE_BARRIER barriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(
				pColorMap,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_RENDER_TARGET
			),
			CD3DX12_RESOURCE_BARRIER::Transition(
				pAlbedoMap,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_RENDER_TARGET
			),
			CD3DX12_RESOURCE_BARRIER::Transition(
				pNormalDepthMap,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_RENDER_TARGET
			),
			CD3DX12_RESOURCE_BARRIER::Transition(
				mDepthStencilBuffer.Get(),
				D3D12_RESOURCE_STATE_DEPTH_READ,
				D3D12_RESOURCE_STATE_DEPTH_WRITE
			),
			CD3DX12_RESOURCE_BARRIER::Transition(
				pSpecularMap,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_RENDER_TARGET
			),
			CD3DX12_RESOURCE_BARRIER::Transition(
				pVelocityMap,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_RENDER_TARGET
			),
			CD3DX12_RESOURCE_BARRIER::Transition(
				pPrevNormalDepthMap,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_RENDER_TARGET
			)
		};
		mCommandList->ResourceBarrier(
			_countof(barriers),
			barriers
		);
	}

	const auto colorRtv = mGBuffer->ColorMapRtv();
	const auto albedoRtv = mGBuffer->AlbedoMapRtv();
	const auto normalRtv = mGBuffer->NormalDepthMapRtv();
	const auto specularRtv = mGBuffer->SpecularMapRtv();
	const auto velocityRtv = mGBuffer->VelocityMapRtv();
	const auto prevNormalDepthRtv = mGBuffer->ReprojectedNormalDepthMapRtv();
	mCommandList->ClearRenderTargetView(colorRtv, GBuffer::ColorMapClearValues, 0, nullptr);
	mCommandList->ClearRenderTargetView(albedoRtv, GBuffer::AlbedoMapClearValues, 0, nullptr);
	mCommandList->ClearRenderTargetView(normalRtv, GBuffer::NormalDepthMapClearValues, 0, nullptr);
	mCommandList->ClearRenderTargetView(specularRtv, GBuffer::SpecularMapClearValues, 0, nullptr);
	mCommandList->ClearRenderTargetView(velocityRtv, GBuffer::VelocityMapClearValues, 0, nullptr);
	mCommandList->ClearRenderTargetView(prevNormalDepthRtv, GBuffer::ReprojectedNormalDepthMapClearValues, 0, nullptr);

	std::array<D3D12_CPU_DESCRIPTOR_HANDLE, GBuffer::NumRenderTargets> renderTargets = { colorRtv, albedoRtv, normalRtv, specularRtv };
	mCommandList->OMSetRenderTargets(static_cast<UINT>(renderTargets.size()), renderTargets.data(), true, &DepthStencilView());
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	auto passCBAddress = mCurrFrameResource->PassCB.Resource()->GetGPUVirtualAddress();
	mCommandList->SetGraphicsRootConstantBufferView(ERasterization::RootSignatureLayout::ECB_Pass, passCBAddress);

	const auto& objSBAddress = mCurrFrameResource->ObjectSB.Resource()->GetGPUVirtualAddress();
	mCommandList->SetGraphicsRootShaderResourceView(ERasterization::RootSignatureLayout::ESB_Object, objSBAddress);

	const auto& matSBAddress = mCurrFrameResource->MaterialSB.Resource()->GetGPUVirtualAddress();
	mCommandList->SetGraphicsRootShaderResourceView(ERasterization::RootSignatureLayout::ESB_Material, matSBAddress);

	CheckIsValid(DrawRenderItems(mRitems[ERenderTypes::EOpaque]));

	{
		D3D12_RESOURCE_BARRIER barriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(
				pColorMap,
				D3D12_RESOURCE_STATE_RENDER_TARGET,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
			),
			CD3DX12_RESOURCE_BARRIER::Transition(
				pAlbedoMap,
				D3D12_RESOURCE_STATE_RENDER_TARGET,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
			),
			CD3DX12_RESOURCE_BARRIER::Transition(
				pNormalDepthMap,
				D3D12_RESOURCE_STATE_RENDER_TARGET,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
			),
			CD3DX12_RESOURCE_BARRIER::Transition(
				mDepthStencilBuffer.Get(),
				D3D12_RESOURCE_STATE_DEPTH_WRITE,
				D3D12_RESOURCE_STATE_DEPTH_READ
			),
			CD3DX12_RESOURCE_BARRIER::Transition(
				pSpecularMap,
				D3D12_RESOURCE_STATE_RENDER_TARGET,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
			),
			CD3DX12_RESOURCE_BARRIER::Transition(
				pVelocityMap,
				D3D12_RESOURCE_STATE_RENDER_TARGET,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
			),
			CD3DX12_RESOURCE_BARRIER::Transition(
				pPrevNormalDepthMap,
				D3D12_RESOURCE_STATE_RENDER_TARGET,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
			)
		};
		mCommandList->ResourceBarrier(
			_countof(barriers),
			barriers
		);
	}

	CheckHResult(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	return true;
}

bool Renderer::DrawSsao() {
	const auto cmdList = mCommandList.Get();

	CheckHResult(cmdList->Reset(mCurrFrameResource->CmdListAlloc.Get(), mPSOs["ssao"].Get()));

	cmdList->SetGraphicsRootSignature(mRootSignatures["ssao"].Get());

	const auto pDescHeap = mCbvSrvUavHeap.Get();
	auto descSize = GetCbvSrvUavDescriptorSize();

	ID3D12DescriptorHeap* descriptorHeaps[] = { pDescHeap };
	cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	cmdList->RSSetViewports(1, &mSsao->Viewport());
	cmdList->RSSetScissorRects(1, &mSsao->ScissorRect());

	// We compute the initial SSAO to AmbientMap0.
	auto pAmbientMap0 = mSsao->AmbientMap0Resource();
	cmdList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			pAmbientMap0,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_RENDER_TARGET
		)
	);

	auto ambientMap0Rtv = mSsao->AmbientMap0Rtv();

	// Specify the buffers we are going to render to.
	cmdList->OMSetRenderTargets(1, &ambientMap0Rtv, true, nullptr);

	// Bind the constant buffer for this pass.
	auto ssaoCBAddress = mCurrFrameResource->SsaoCB.Resource()->GetGPUVirtualAddress();
	cmdList->SetGraphicsRootConstantBufferView(EScreenSpaceAO::RootSignatureLayout::ECB_SsaoPass, ssaoCBAddress);

	// Bind the normal and depth maps.
	cmdList->SetGraphicsRootDescriptorTable(
		EScreenSpaceAO::RootSignatureLayout::ESI_NormalAndDepth,
		D3D12Util::GetGpuHandle(pDescHeap, EDescriptors::ES_NormalDepth, descSize)
	);

	// Bind the random vector map.
	cmdList->SetGraphicsRootDescriptorTable(
		EScreenSpaceAO::RootSignatureLayout::ESI_RandomVector,
		D3D12Util::GetGpuHandle(pDescHeap, EDescriptors::ES_RandomVector, descSize)
	);

	// Draw fullscreen quad.
	cmdList->IASetVertexBuffers(0, 0, nullptr);
	cmdList->IASetIndexBuffer(nullptr);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->DrawInstanced(6, 1, 0, 0);

	// Change back to GENERIC_READ so we can read the texture in a shader.
	cmdList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			pAmbientMap0,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		)
	);

	static const auto blurAmbientMap = [&](bool horzBlur) {
		ID3D12Resource* output = nullptr;
		CD3DX12_GPU_DESCRIPTOR_HANDLE inputSrv;
		CD3DX12_CPU_DESCRIPTOR_HANDLE outputRtv;
	
		// Ping-pong the two ambient map textures as we apply
		// horizontal and vertical blur passes.
		if (horzBlur == true) {
			output = mSsao->AmbientMap1Resource();
			outputRtv = mSsao->AmbientMap1Rtv();
			inputSrv = mSsao->AmbientMap0Srv();
			cmdList->SetGraphicsRoot32BitConstant(EGaussianBlur::RootSignatureLayout::EC_Consts, 1, EGaussianBlur::RootConstantsLayout::EHorizontal);
		}
		else {
			output = mSsao->AmbientMap0Resource();
			outputRtv = mSsao->AmbientMap0Rtv();
			inputSrv = mSsao->AmbientMap1Srv();
			cmdList->SetGraphicsRoot32BitConstant(EGaussianBlur::RootSignatureLayout::EC_Consts, 0, EGaussianBlur::RootConstantsLayout::EHorizontal);
		}
	
		cmdList->ResourceBarrier(
			1,
			&CD3DX12_RESOURCE_BARRIER::Transition(
				output,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_RENDER_TARGET
			)
		);
	
		cmdList->ClearRenderTargetView(outputRtv, Ssao::AmbientMapClearValues, 0, nullptr);
	
		cmdList->OMSetRenderTargets(1, &outputRtv, true, nullptr);
	
		// Bind the input ambient map to second texture table.
		cmdList->SetGraphicsRootDescriptorTable(EGaussianBlur::RootSignatureLayout::ESI_Input, inputSrv);
	
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
	};
	
	cmdList->SetPipelineState(mPSOs["ssaoBlur"].Get());
	cmdList->SetGraphicsRootSignature(mRootSignatures["gaussianBlur"].Get());
	
	auto blurCBAddress = mCurrFrameResource->BlurCB.Resource()->GetGPUVirtualAddress();
	cmdList->SetGraphicsRootConstantBufferView(EGaussianBlur::RootSignatureLayout::ECB_BlurPass, blurCBAddress);
	cmdList->SetGraphicsRootDescriptorTable(EGaussianBlur::RootSignatureLayout::ESI_NormalAndDepth, mGBuffer->NormalDepthMapSrv());

	float values[] = { mSsaoDotThreshold, mSsaoDepthThreshold };
	cmdList->SetGraphicsRoot32BitConstants(
		EGaussianBlur::RootSignatureLayout::EC_Consts,
		_countof(values), 
		values, 
		EGaussianBlur::RootConstantsLayout::EDotThreshold
	);
	
	for (int i = 0; i < mNumSsaoBlurs; ++i) {
		blurAmbientMap(true);
		blurAmbientMap(false);
	}

	CheckHResult(cmdList->Close());
	ID3D12CommandList* cmdsLists[] = { cmdList };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	return true;
}

bool Renderer::DrawBackBuffer() {
	CheckHResult(mCommandList->Reset(mCurrFrameResource->CmdListAlloc.Get(), mPSOs["backBuffer"].Get()));

	mCommandList->SetGraphicsRootSignature(mRootSignatures["raster"].Get());

	const auto pDescHeap = mCbvSrvUavHeap.Get();
	ID3D12DescriptorHeap* descriptorHeaps[] = { pDescHeap };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	const auto pCurrBackBuffer = CurrentBackBuffer();
	mCommandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			pCurrBackBuffer,
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET
		)
	);

	auto pCurrBackBufferView = CurrentBackBufferView();
	mCommandList->OMSetRenderTargets(1, &pCurrBackBufferView, true, nullptr);

	auto passCBAddress = mCurrFrameResource->PassCB.Resource()->GetGPUVirtualAddress();
	mCommandList->SetGraphicsRootConstantBufferView(ERasterization::RootSignatureLayout::ECB_Pass, passCBAddress);

	const auto& objSBAddress = mCurrFrameResource->ObjectSB.Resource()->GetGPUVirtualAddress();
	mCommandList->SetGraphicsRootShaderResourceView(ERasterization::RootSignatureLayout::ESB_Object, objSBAddress);

	const auto& matSBAddress = mCurrFrameResource->MaterialSB.Resource()->GetGPUVirtualAddress();
	mCommandList->SetGraphicsRootShaderResourceView(ERasterization::RootSignatureLayout::ESB_Material, matSBAddress);

	mCommandList->SetGraphicsRootDescriptorTable(
		ERasterization::RootSignatureLayout::ESI_SrvMaps,
		D3D12Util::GetGpuHandle(pDescHeap, EDescriptors::Srv_Start, GetCbvSrvUavDescriptorSize())
	);
	mCommandList->SetGraphicsRootDescriptorTable(
		ERasterization::RootSignatureLayout::EUIO_UavMaps,
		D3D12Util::GetGpuHandle(pDescHeap, EDescriptors::Uav_Start, GetCbvSrvUavDescriptorSize())
	);

	mCommandList->IASetVertexBuffers(0, 0, nullptr);
	mCommandList->IASetIndexBuffer(nullptr);
	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	mCommandList->DrawInstanced(6, 1, 0, 0);

	mCommandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			pCurrBackBuffer,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT
		)
	);

	CheckHResult(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	return true;
}

bool Renderer::DrawDebugLayer() {
	CheckHResult(mCommandList->Reset(mCurrFrameResource->CmdListAlloc.Get(), mPSOs["gizmo"].Get()));

	mCommandList->SetGraphicsRootSignature(mRootSignatures["raster"].Get());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvSrvUavHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->RSSetViewports(1, &mDebugViewport);
	mCommandList->RSSetScissorRects(1, &mDebugScissorRect);

	const auto renderTarget = CurrentBackBuffer();
	const auto dxrShadowMap0 = mDxrShadowMap->ShadowMap0Resource();
	const auto dxrShadowMap1 = mDxrShadowMap->ShadowMap1Resource();
	std::vector<ID3D12Resource*> resources = { dxrShadowMap0, dxrShadowMap1 };
	{
		D3D12_RESOURCE_BARRIER barriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(
				renderTarget,
				D3D12_RESOURCE_STATE_PRESENT,
				D3D12_RESOURCE_STATE_RENDER_TARGET
			),
			CD3DX12_RESOURCE_BARRIER::Transition(
				dxrShadowMap0,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
			),
			CD3DX12_RESOURCE_BARRIER::Transition(
				dxrShadowMap1,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
			)
		};
		mCommandList->ResourceBarrier(
			_countof(barriers),
			barriers
		);

		D3D12Util::UavBarriers(mCommandList.Get(), resources.data(), resources.size());
	}

	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, nullptr);

	const auto& passCBAddress = mCurrFrameResource->PassCB.Resource()->GetGPUVirtualAddress();
	mCommandList->SetGraphicsRootConstantBufferView(ERasterization::RootSignatureLayout::ECB_Pass, passCBAddress);

	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
	mCommandList->DrawInstanced(2, 3, 0, 0);

	if (bDisplayMaps) {
		mCommandList->SetPipelineState(mPSOs["debug"].Get());

		mCommandList->RSSetViewports(1, &mScreenViewport);
		mCommandList->RSSetScissorRects(1, &mScissorRect);

		mCommandList->SetGraphicsRoot32BitConstant(
			ERasterization::RootSignatureLayout::EC_Consts,
			bRaytracing,
			ERasterization::RootConstantsLayout::EIsRaytracing
		);

		mCommandList->SetGraphicsRootDescriptorTable(
			ERasterization::RootSignatureLayout::ESI_SrvMaps,
			D3D12Util::GetGpuHandle(mCbvSrvUavHeap.Get(), EDescriptors::Srv_Start, GetCbvSrvUavDescriptorSize())
		);
		mCommandList->SetGraphicsRootDescriptorTable(
			ERasterization::RootSignatureLayout::EUIO_UavMaps,
			D3D12Util::GetGpuHandle(mCbvSrvUavHeap.Get(), EDescriptors::Uav_Start, GetCbvSrvUavDescriptorSize())
		);

		mCommandList->IASetVertexBuffers(0, 0, nullptr);
		mCommandList->IASetIndexBuffer(nullptr);
		mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		mCommandList->DrawInstanced(6, 5, 0, 0);
	}

	{
		D3D12_RESOURCE_BARRIER barriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(
				renderTarget,
				D3D12_RESOURCE_STATE_RENDER_TARGET,
				D3D12_RESOURCE_STATE_PRESENT
			),
			CD3DX12_RESOURCE_BARRIER::Transition(
				dxrShadowMap0,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS
			),
			CD3DX12_RESOURCE_BARRIER::Transition(
				dxrShadowMap1,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS
			)
		};
		mCommandList->ResourceBarrier(
			_countof(barriers),
			barriers
		);

		D3D12Util::UavBarriers(mCommandList.Get(), resources.data(), resources.size());
	}

	CheckHResult(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	return true;
}

bool Renderer::DrawImGui() {
	CheckHResult(mCommandList->Reset(mCurrFrameResource->CmdListAlloc.Get(), nullptr));

	ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvSrvUavHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	const auto pCurrBackBuffer = CurrentBackBuffer();
	mCommandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			pCurrBackBuffer,
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET
		)
	);

	auto pCurrBackBufferView = CurrentBackBufferView();
	mCommandList->OMSetRenderTargets(1, &pCurrBackBufferView, true, nullptr);

	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();

	{
		ImGui::Begin("Main Panel");
		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
		ImGui::NewLine();

		ImGui::Checkbox("Display Maps", &bDisplayMaps);

		ImGui::End();
	}
	{
		ImGui::Begin("Sub Panel");
		ImGui::NewLine();

		if (ImGui::CollapsingHeader("Raytracing")) {
			if (ImGui::TreeNode("Shadow")) {
				ImGui::SliderInt("Number of Blurs", &mNumDxrShadowBlurs, 0, 8);

				ImGui::TreePop();
			}
			if (ImGui::TreeNode("RTAO")) {
				ImGui::SliderInt("Sample Count", reinterpret_cast<int*>(&mRtaoSampleCount), 1, 4);
				ImGui::SliderFloat("Occlusion Radius", &mRtaoRadius, 0.01f, 100.0f);
				ImGui::SliderFloat("Occlusion Fade Start", &mRtaoFadeStart, 0.0f, 10.0f);
				ImGui::SliderFloat("Occlusion Fade End", &mRtaoFadeEnd, 0.0f, 100.0f);
				ImGui::SliderFloat("Surface Epsilon", &mRtaoEpsilon, 0.01f, 1.0f);
				ImGui::Checkbox("Checkerboard Sampling", &bCheckerboardSamplingEnabled);

				ImGui::TreePop();
			}
		}
		if (ImGui::CollapsingHeader("Rasterization")) {
			if (ImGui::TreeNode("SSAO")) {
				ImGui::SliderFloat("Occlusion Radius", &mSsaoRadius, 0.01f, 1.0f);
				ImGui::SliderFloat("Occlusion Fade Start", &mSsaoFadeStart, 0.0f, 10.0f);
				ImGui::SliderFloat("Occlusion Fade End", &mSsaoFadeEnd, 0.0f, 10.0f);
				ImGui::SliderFloat("Surface Epsilon", &mSsaoEpsilon, 0.01f, 1.0f);
				ImGui::SliderFloat("Blur Dot Threshold", &mSsaoDotThreshold, -1.0f, 1.0f);
				ImGui::SliderFloat("Blur Depth Threshold", &mSsaoDepthThreshold, 0.0f, 10.0f);
				ImGui::SliderInt("Number of Blurs", &mNumSsaoBlurs, 0, 8);

				ImGui::TreePop();
			}
		}

		ImGui::End();
	}

	ImGui::Render();

	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), mCommandList.Get());

	mCommandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			pCurrBackBuffer,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT
		)
	);

	CheckHResult(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	return true;
}

bool Renderer::Raytrace() {
	CheckIsValid(DrawGBuffer());
	CheckIsValid(dxrDrawShadowMap());
	CheckIsValid(dxrDrawRtao());
	CheckIsValid(dxrDrawBackBuffer());

	return true;
}

bool Renderer::dxrDrawShadowMap() {
	const auto cmdList = mCommandList.Get();
	CheckHResult(cmdList->Reset(mCurrFrameResource->CmdListAlloc.Get(), nullptr));

	cmdList->SetComputeRootSignature(mRootSignatures["dxr_global"].Get());

	const auto pDescHeap = mCbvSrvUavHeap.Get();
	UINT descSize = GetCbvSrvUavDescriptorSize();

	ID3D12DescriptorHeap* descriptorHeaps[] = { pDescHeap };
	cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	cmdList->SetComputeRootShaderResourceView(EDefaultRaytracing::RootSignatureLayout::ESI_AccelerationStructure, mTLAS->Result->GetGPUVirtualAddress());

	const auto& passCBAddress = mCurrFrameResource->PassCB.Resource()->GetGPUVirtualAddress();
	cmdList->SetComputeRootConstantBufferView(EDefaultRaytracing::RootSignatureLayout::ECB_Pass, passCBAddress);

	const auto& objSBAddress = mCurrFrameResource->ObjectSB.Resource()->GetGPUVirtualAddress();
	cmdList->SetComputeRootShaderResourceView(EDefaultRaytracing::RootSignatureLayout::ESB_Object, objSBAddress);

	const auto& matSBAddress = mCurrFrameResource->MaterialSB.Resource()->GetGPUVirtualAddress();
	cmdList->SetComputeRootShaderResourceView(EDefaultRaytracing::RootSignatureLayout::ESB_Material, matSBAddress);

	cmdList->SetComputeRootDescriptorTable(
		EDefaultRaytracing::RootSignatureLayout::ESB_Vertices,
		D3D12Util::GetGpuHandle(pDescHeap, EDescriptors::ES_Vertices, descSize)
	);
	cmdList->SetComputeRootDescriptorTable(
		EDefaultRaytracing::RootSignatureLayout::ESB_Indices,
		D3D12Util::GetGpuHandle(pDescHeap, EDescriptors::ES_Indices, descSize)
	);

	cmdList->SetComputeRootDescriptorTable(
		EDefaultRaytracing::RootSignatureLayout::ESI_SrvMaps,
		D3D12Util::GetGpuHandle(pDescHeap, EDescriptors::Srv_Start, descSize)
	);
	cmdList->SetComputeRootDescriptorTable(
		EDefaultRaytracing::RootSignatureLayout::EUIO_UavMaps,
		D3D12Util::GetGpuHandle(pDescHeap, EDescriptors::Uav_Start, descSize)
	);

	D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
	const auto& rayGen = mShaderTables["shadowRayGen"];
	const auto& miss = mShaderTables["shadowMiss"];
	const auto& hitGroup = mShaderTables["shadowHitGroup"];
	dispatchDesc.RayGenerationShaderRecord.StartAddress = rayGen->GetGPUVirtualAddress();
	dispatchDesc.RayGenerationShaderRecord.SizeInBytes = rayGen->GetDesc().Width;
	dispatchDesc.MissShaderTable.StartAddress = miss->GetGPUVirtualAddress();
	dispatchDesc.MissShaderTable.SizeInBytes = miss->GetDesc().Width;
	dispatchDesc.MissShaderTable.StrideInBytes = dispatchDesc.MissShaderTable.SizeInBytes;
	dispatchDesc.HitGroupTable.StartAddress = hitGroup->GetGPUVirtualAddress();
	dispatchDesc.HitGroupTable.SizeInBytes = hitGroup->GetDesc().Width;
	dispatchDesc.HitGroupTable.StrideInBytes = dispatchDesc.HitGroupTable.SizeInBytes;
	dispatchDesc.Width = GetClientWidth();
	dispatchDesc.Height = GetClientHeight();
	dispatchDesc.Depth = 1;
	
	cmdList->SetPipelineState1(mDXRPSOs["default"].Get());
	cmdList->DispatchRays(&dispatchDesc);
	
	std::vector<ID3D12Resource*> resources = { mDxrShadowMap->ShadowMap0Resource(), mDxrShadowMap->ShadowMap1Resource() };
	D3D12Util::UavBarriers(cmdList, resources.data(), resources.size());

	mCommandList->SetComputeRootSignature(mRootSignatures["csBlur"].Get());

	auto blurPassCBAddress = mCurrFrameResource->BlurCB.Resource()->GetGPUVirtualAddress();
	cmdList->SetComputeRootConstantBufferView(EComputeGaussianBlur::RootSignatureLayout::ECB_BlurPass, blurPassCBAddress);

	UINT values[EComputeGaussianBlur::RootConstantsLayout::Count] = { GetClientWidth(), GetClientHeight() };
	cmdList->SetComputeRoot32BitConstants(EComputeGaussianBlur::RootSignatureLayout::EC_Consts, _countof(values), values, 0);

	cmdList->SetComputeRootDescriptorTable(
		EComputeGaussianBlur::RootSignatureLayout::ESI_NormalAndDepth,
		D3D12Util::GetGpuHandle(pDescHeap, EDescriptors::ES_NormalDepth, descSize)
	);

	for (int i = 0; i < mNumDxrShadowBlurs; ++i) {
		mCommandList->SetPipelineState(mPSOs["horzBlur"].Get());

		cmdList->SetComputeRootDescriptorTable(
			EComputeGaussianBlur::RootSignatureLayout::ESI_Input,
			mDxrShadowMap->ShadowMap0Srv()
		);

		cmdList->SetComputeRootDescriptorTable(
			EComputeGaussianBlur::RootSignatureLayout::EUO_Output,
			mDxrShadowMap->ShadowMap1Uav()
		);

		UINT numGroupsX = static_cast<UINT>(ceilf(GetClientWidth() / static_cast<float>(GaussianBlurComputeShaderParams::ThreadGroup::Size)));
		cmdList->Dispatch(numGroupsX, GetClientHeight(), 1);
		D3D12Util::UavBarriers(cmdList, resources.data(), resources.size());

		mCommandList->SetPipelineState(mPSOs["vertBlur"].Get());

		cmdList->SetComputeRootDescriptorTable(
			EComputeGaussianBlur::RootSignatureLayout::ESI_Input,
			mDxrShadowMap->ShadowMap1Srv()
		);

		cmdList->SetComputeRootDescriptorTable(
			EComputeGaussianBlur::RootSignatureLayout::EUO_Output,
			mDxrShadowMap->ShadowMap0Uav()
		);

		UINT numGroupsY = static_cast<UINT>(ceilf(GetClientHeight() / static_cast<float>(GaussianBlurComputeShaderParams::ThreadGroup::Size)));
		cmdList->Dispatch(GetClientWidth(), numGroupsY, 1);
		D3D12Util::UavBarriers(cmdList, resources.data(), resources.size());
	}

	CheckHResult(cmdList->Close());
	ID3D12CommandList* cmdsLists[] = { cmdList };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	return true;
}

bool Renderer::dxrDrawRtao() {
	const auto cmdList = mCommandList.Get();

	CheckHResult(cmdList->Reset(mCurrFrameResource->CmdListAlloc.Get(), nullptr));

	cmdList->SetComputeRootSignature(mRootSignatures["rtao_global"].Get());

	const auto pDescHeap = mCbvSrvUavHeap.Get();
	UINT descSize = GetCbvSrvUavDescriptorSize();

	ID3D12DescriptorHeap* descriptorHeaps[] = { pDescHeap };
	cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	const auto& aoResources = mRtao->AOResources();
	const auto& aoResourcesGpuDescriptors = mRtao->AOResourcesGpuDescriptors();
	const auto& temporalCaches = mRtao->TemporalCaches();
	const auto& temporalCachesGpuDescriptors = mRtao->TemporalCachesGpuDescriptors();

	// RTAO
	{
		cmdList->SetComputeRootShaderResourceView(ERaytracedAO::RootSignatureLayout::ESI_AccelerationStructure, mTLAS->Result->GetGPUVirtualAddress());

		const auto& rtaoPassCBAddress = mCurrFrameResource->RtaoCB.Resource()->GetGPUVirtualAddress();
		cmdList->SetComputeRootConstantBufferView(ERaytracedAO::RootSignatureLayout::ECB_RtaoPass, rtaoPassCBAddress);

		const UINT values[ERaytracedAO::RootConstantsLayout::Count] = { mGBuffer->Width(), mGBuffer->Height() };
		cmdList->SetComputeRoot32BitConstants(ERaytracedAO::RootSignatureLayout::EC_Consts, _countof(values), values, 0);

		cmdList->SetComputeRootDescriptorTable(
			ERaytracedAO::RootSignatureLayout::ESI_NormalAndDepth,
			D3D12Util::GetGpuHandle(pDescHeap, EDescriptors::ES_NormalDepth, descSize)
		);
		cmdList->SetComputeRootDescriptorTable(
			ERaytracedAO::RootSignatureLayout::EUO_AmbientCoefficient,
			D3D12Util::GetGpuHandle(pDescHeap, EDescriptors::EU_DxrAmbient0, descSize)
		);

		D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
		const auto& rayGen = mShaderTables["rtaoRayGen"];
		const auto& miss = mShaderTables["rtaoMiss"];
		const auto& hitGroup = mShaderTables["rtaoHitGroup"];
		dispatchDesc.RayGenerationShaderRecord.StartAddress = rayGen->GetGPUVirtualAddress();
		dispatchDesc.RayGenerationShaderRecord.SizeInBytes = rayGen->GetDesc().Width;
		dispatchDesc.MissShaderTable.StartAddress = miss->GetGPUVirtualAddress();
		dispatchDesc.MissShaderTable.SizeInBytes = miss->GetDesc().Width;
		dispatchDesc.MissShaderTable.StrideInBytes = dispatchDesc.MissShaderTable.SizeInBytes;
		dispatchDesc.HitGroupTable.StartAddress = hitGroup->GetGPUVirtualAddress();
		dispatchDesc.HitGroupTable.SizeInBytes = hitGroup->GetDesc().Width;
		dispatchDesc.HitGroupTable.StrideInBytes = dispatchDesc.HitGroupTable.SizeInBytes;
		dispatchDesc.Width = GetClientWidth();
		dispatchDesc.Height = GetClientHeight();
		dispatchDesc.Depth = 1;

		cmdList->SetPipelineState1(mDXRPSOs["rtao"].Get());
		cmdList->DispatchRays(&dispatchDesc);

		std::vector<ID3D12Resource*> resources = { aoResources[RaytracedAO::AOResources::EAmbientCoefficient].Get() };
		D3D12Util::UavBarriers(cmdList, resources.data(), resources.size());
	}
	// Calculate partial-derivatives
	{
		cmdList->SetPipelineState(mPSOs["partialDerivatives"].Get());
		cmdList->SetComputeRootSignature(mRootSignatures["partialDerivatives"].Get());

		const UINT values[ECalcPartialDerivatives::RootConstantsLayout::Count] = { mGBuffer->Width(), mGBuffer->Height() };
		cmdList->SetComputeRoot32BitConstants(ECalcPartialDerivatives::RootSignatureLayout::EC_Consts, _countof(values), values, 0);

		cmdList->SetComputeRootDescriptorTable(
			ECalcPartialDerivatives::RootSignatureLayout::ESI_Depth,
			mGBuffer->DepthMapSrv()
		);
		cmdList->SetComputeRootDescriptorTable(
			ECalcPartialDerivatives::RootSignatureLayout::EUO_LinearDepthDerivatives,
			mRtao->LinearDepthDerivativesMapUav()
		);

		UINT numGroupsX = static_cast<UINT>(ceilf(mRtao->Width() / static_cast<float>(DefaultComputeShaderParams::ThreadGroup::Width)));
		UINT numGroupsY = static_cast<UINT>(ceilf(mRtao->Height() / static_cast<float>(DefaultComputeShaderParams::ThreadGroup::Height)));
		cmdList->Dispatch(numGroupsX, numGroupsY, 1);

		std::vector<ID3D12Resource*> resources = { mRtao->LinearDepthDerivativesMapResource() };
		D3D12Util::UavBarriers(cmdList, resources.data(), resources.size());
	}
	// Denosing(Spatio-Temporal Variance Guided Filtering)
	{
		// Stage 1: Reverse reprojection
		//
		// Retrieves values from previous frame via reverse reprojection.
		{
			cmdList->SetPipelineState(mPSOs["tspp_reproj"].Get());
			cmdList->SetComputeRootSignature(mRootSignatures["tspp_reproj"].Get());

			const auto pCurrTemporalSupersamplingMap = temporalCaches[0][RaytracedAO::TemporalCaches::ETemporalSupersampling].Get();
			{
				D3D12_RESOURCE_BARRIER barriers[] = {
					CD3DX12_RESOURCE_BARRIER::Transition(
						pCurrTemporalSupersamplingMap,
						D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
						D3D12_RESOURCE_STATE_UNORDERED_ACCESS
					)
				};
				cmdList->ResourceBarrier(
					_countof(barriers),
					barriers
				);
				std::vector<ID3D12Resource*> resources = {
					pCurrTemporalSupersamplingMap
				};
				D3D12Util::UavBarriers(cmdList, resources.data(), resources.size());
			}

			const auto filterCBAddress = mCurrFrameResource->CrossBilateralFilterCB.Resource()->GetGPUVirtualAddress();
			cmdList->SetComputeRootConstantBufferView(
				ETemporalSupersamplingReverseReproject::RootSinatureLayout::ECB_CrossBilateralFilter,
				filterCBAddress
			);

			cmdList->SetComputeRootDescriptorTable(
				ETemporalSupersamplingReverseReproject::RootSinatureLayout::ESI_NormalDepth,
				mGBuffer->NormalDepthMapSrv()
			);
			cmdList->SetComputeRootDescriptorTable(
				ETemporalSupersamplingReverseReproject::RootSinatureLayout::ESI_Velocity,
				mGBuffer->VelocityMapSrv()
			);
			cmdList->SetComputeRootDescriptorTable(
				ETemporalSupersamplingReverseReproject::RootSinatureLayout::ESI_ReprojectedNormalDepth,
				mGBuffer->ReprojectedNormalDepthMapSrv()
			);
			cmdList->SetComputeRootDescriptorTable(
				ETemporalSupersamplingReverseReproject::RootSinatureLayout::ESI_CachedNormalDepth,
				mRtao->CachedNormalDepthMapSrv()
			);
			cmdList->SetComputeRootDescriptorTable(
				ETemporalSupersamplingReverseReproject::RootSinatureLayout::ESI_CachedTemporalAOCoefficient,
				mRtao->CachedTemporalAOCoefficientMapSrv()
			);
			cmdList->SetComputeRootDescriptorTable(
				ETemporalSupersamplingReverseReproject::RootSinatureLayout::ESI_CachedTemporalSuperSampling,
				temporalCachesGpuDescriptors[1][RaytracedAO::TemporalCaches::Descriptors::ES_TemporalSupersampling]
			);
			cmdList->SetComputeRootDescriptorTable(
				ETemporalSupersamplingReverseReproject::RootSinatureLayout::ESI_CachedCoefficientSquaredMean,
				mRtao->CachedCoefficientSquaredMeanMapSrv()
			);
			cmdList->SetComputeRootDescriptorTable(
				ETemporalSupersamplingReverseReproject::RootSinatureLayout::ESI_CachedRayHitDistance,
				mRtao->CachedRayHitDistanceMapSrv()
			);
			cmdList->SetComputeRootDescriptorTable(
				ETemporalSupersamplingReverseReproject::RootSinatureLayout::EUO_TemporalSuperSampling,
				temporalCachesGpuDescriptors[0][RaytracedAO::TemporalCaches::Descriptors::EU_TemporalSupersampling]
			);
			cmdList->SetComputeRootDescriptorTable(
				ETemporalSupersamplingReverseReproject::RootSinatureLayout::EUI_LinearDepthDerivatives,
				mRtao->LinearDepthDerivativesMapUav()
			);
			cmdList->SetComputeRootDescriptorTable(
				ETemporalSupersamplingReverseReproject::RootSinatureLayout::EUO_TsppCoefficientSquaredMeanRayHitDistacne,
				mRtao->TsppCoefficientSquaredMeanRayHitDistanceMapUav()
			);

			UINT values[ETemporalSupersamplingReverseReproject::RootConstantsLayout::Count] = { mRtao->Width(), mRtao->Height() };
			cmdList->SetComputeRoot32BitConstants(ETemporalSupersamplingReverseReproject::RootSinatureLayout::EC_Consts, _countof(values), values, 0);

			UINT numGroupsX = static_cast<UINT>(ceilf(mRtao->Width() / static_cast<float>(DefaultComputeShaderParams::ThreadGroup::Width)));
			UINT numGroupsY = static_cast<UINT>(ceilf(mRtao->Height() / static_cast<float>(DefaultComputeShaderParams::ThreadGroup::Height)));
			cmdList->Dispatch(numGroupsX, numGroupsY, 1);
			{
				D3D12_RESOURCE_BARRIER barriers[] = {
					CD3DX12_RESOURCE_BARRIER::Transition(
						pCurrTemporalSupersamplingMap,
						D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
						D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
					)
				};
				cmdList->ResourceBarrier(
					_countof(barriers),
					barriers
				);
				std::vector<ID3D12Resource*> resources = {
					pCurrTemporalSupersamplingMap, mRtao->LinearDepthDerivativesMapResource(), mRtao->TsppCoefficientSquaredMeanRayHitDistanceMapResource()
				};
				D3D12Util::UavBarriers(cmdList, resources.data(), resources.size());
			}

			// Copy the current normal and depth values to the cached map.
			{
				const auto pNormalDepthMap = mGBuffer->NormalDepthMapResource();
				const auto pCachedNormalDepthMap = mRtao->CachedNormalDepthMapResource();
				{
					D3D12_RESOURCE_BARRIER barriers[] = {
						CD3DX12_RESOURCE_BARRIER::Transition(
							pNormalDepthMap,
							D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
							D3D12_RESOURCE_STATE_COPY_SOURCE
						),
						CD3DX12_RESOURCE_BARRIER::Transition(
							pCachedNormalDepthMap,
							D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
							D3D12_RESOURCE_STATE_COPY_DEST
						)
					};
					cmdList->ResourceBarrier(
						_countof(barriers),
						barriers
					);
				}
				cmdList->CopyResource(pCachedNormalDepthMap, pNormalDepthMap);
				{
					D3D12_RESOURCE_BARRIER barriers[] = {
						CD3DX12_RESOURCE_BARRIER::Transition(
							pNormalDepthMap,
							D3D12_RESOURCE_STATE_COPY_SOURCE,
							D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
						),
						CD3DX12_RESOURCE_BARRIER::Transition(
							pCachedNormalDepthMap,
							D3D12_RESOURCE_STATE_COPY_DEST,
							D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
						)
					};
					cmdList->ResourceBarrier(
						_countof(barriers),
						barriers
					);
				}
			}
		}
		// Stage 2: Blending current frame value with the reprojected cachec value
		//
		// Blends reprojected values with current frame values.
		// Inactive pixels are filtered from active neighbors on checkerboard sampling before the blending operation.
		{
			// Calculate local mean and variance for clamping during the blending operation.
			{
				cmdList->SetPipelineState(mPSOs["localMeanVariance"].Get());
				cmdList->SetComputeRootSignature(mRootSignatures["localMeanVariance"].Get());

				const auto pAmbientCoefficientMap = aoResources[RaytracedAO::AOResources::EAmbientCoefficient].Get();
				const auto pRawLocalMeanVarMap = mRtao->RawLocalMeanVarianceMapResource();
				std::vector<ID3D12Resource*> resources = { pAmbientCoefficientMap, pRawLocalMeanVarMap };
				{
					D3D12_RESOURCE_BARRIER barriers[] = {
						CD3DX12_RESOURCE_BARRIER::Transition(
							pAmbientCoefficientMap,
							D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
							D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
						),
						CD3DX12_RESOURCE_BARRIER::Transition(
							pRawLocalMeanVarMap,
							D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
							D3D12_RESOURCE_STATE_UNORDERED_ACCESS
						)
					};
					cmdList->ResourceBarrier(
						_countof(barriers),
						barriers
					);

					D3D12Util::UavBarriers(cmdList, resources.data(), resources.size());
				}

				const auto meanVarCBAddress = mCurrFrameResource->CalcLocalMeanVarCB.Resource()->GetGPUVirtualAddress();
				cmdList->SetComputeRootConstantBufferView(ECalcLocalMeanVariance::RootSignatureLayout::ECB_LocalMeanVar, meanVarCBAddress);

				cmdList->SetComputeRootDescriptorTable(
					ECalcLocalMeanVariance::RootSignatureLayout::ESI_AmbientCoefficient,
					aoResourcesGpuDescriptors[RaytracedAO::AOResources::Descriptors::ES_AmbientCoefficient]
				);
				cmdList->SetComputeRootDescriptorTable(
					ECalcLocalMeanVariance::RootSignatureLayout::EUO_LocalMeanVar,
					mRtao->RawLocalMeanVarianceMapUav()
				);

				
				int pixelStepY = bCheckerboardSamplingEnabled ? 2 : 1;
				UINT numGroupsX = static_cast<UINT>(ceilf(mRtao->Width() / static_cast<float>(DefaultComputeShaderParams::ThreadGroup::Width)));
				UINT numGroupsY = static_cast<UINT>(ceilf(mRtao->Height() / static_cast<float>(DefaultComputeShaderParams::ThreadGroup::Height * pixelStepY)));
				cmdList->Dispatch(numGroupsX, numGroupsY, 1);

				{
					D3D12_RESOURCE_BARRIER barriers[] = {
						CD3DX12_RESOURCE_BARRIER::Transition(
							pAmbientCoefficientMap,
							D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
							D3D12_RESOURCE_STATE_UNORDERED_ACCESS
						),
						CD3DX12_RESOURCE_BARRIER::Transition(
							pRawLocalMeanVarMap,
							D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
							D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
						)
					};
					cmdList->ResourceBarrier(
						_countof(barriers),
						barriers
					);

					D3D12Util::UavBarriers(cmdList, resources.data(), resources.size());
				}

				// Interpolate the variance for the inactive cells from the valid checkerboard cells.
				if (bCheckerboardSamplingEnabled) {
					cmdList->SetPipelineState(mPSOs["fillInCheckerboard"].Get());
					cmdList->SetComputeRootSignature(mRootSignatures["fillInCheckerboard"].Get());

					cmdList->ResourceBarrier(
						1,
						&CD3DX12_RESOURCE_BARRIER::Transition(
							pRawLocalMeanVarMap,
							D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
							D3D12_RESOURCE_STATE_UNORDERED_ACCESS
						)
					);
					D3D12Util::UavBarrier(cmdList, pRawLocalMeanVarMap);

					cmdList->SetComputeRootConstantBufferView(EFillInCheckerboard::RootSignatureLayout::ECB_LocalMeanVar, meanVarCBAddress);

					cmdList->SetComputeRootDescriptorTable(
						EFillInCheckerboard::RootSignatureLayout::EUIO_LocalMeanVar,
						mRtao->RawLocalMeanVarianceMapUav()
					);

					UINT numGroupsX = static_cast<UINT>(ceilf(mRtao->Width() / static_cast<float>(DefaultComputeShaderParams::ThreadGroup::Width)));
					UINT numGroupsY = static_cast<UINT>(ceilf(mRtao->Height() / static_cast<float>(DefaultComputeShaderParams::ThreadGroup::Height * 2)));
					cmdList->Dispatch(numGroupsX, numGroupsY, 1);

					cmdList->ResourceBarrier(
						1,
						&CD3DX12_RESOURCE_BARRIER::Transition(
							pRawLocalMeanVarMap,
							D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
							D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
						)
					);
					D3D12Util::UavBarrier(cmdList, pRawLocalMeanVarMap);
				}
			}

			//mRtao->Switch();
		}
	}

	CheckHResult(cmdList->Close());
	ID3D12CommandList* cmdsLists[] = { cmdList };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	return true;
}

bool Renderer::dxrDrawBackBuffer() {
	CheckHResult(mCommandList->Reset(mCurrFrameResource->CmdListAlloc.Get(), mPSOs["dxrBackBuffer"].Get()));

	mCommandList->SetGraphicsRootSignature(mRootSignatures["raster"].Get());

	const auto pDescHeap = mCbvSrvUavHeap.Get();
	ID3D12DescriptorHeap* descriptorHeaps[] = { pDescHeap };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	const auto pCurrBackBuffer = CurrentBackBuffer();
	mCommandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			pCurrBackBuffer,
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET
		)
	);

	auto pCurrBackBufferView = CurrentBackBufferView();
	mCommandList->OMSetRenderTargets(1, &pCurrBackBufferView, true, nullptr);

	auto passCBAddress = mCurrFrameResource->PassCB.Resource()->GetGPUVirtualAddress();
	mCommandList->SetGraphicsRootConstantBufferView(ERasterization::RootSignatureLayout::ECB_Pass, passCBAddress);

	mCommandList->SetGraphicsRootDescriptorTable(
		ERasterization::RootSignatureLayout::ESI_SrvMaps,
		D3D12Util::GetGpuHandle(mCbvSrvUavHeap.Get(), EDescriptors::Srv_Start, GetCbvSrvUavDescriptorSize())
	);
	mCommandList->SetGraphicsRootDescriptorTable(
		ERasterization::RootSignatureLayout::EUIO_UavMaps,
		D3D12Util::GetGpuHandle(mCbvSrvUavHeap.Get(), EDescriptors::Uav_Start, GetCbvSrvUavDescriptorSize())
	);

	mCommandList->IASetVertexBuffers(0, 0, nullptr);
	mCommandList->IASetIndexBuffer(nullptr);
	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	mCommandList->DrawInstanced(6, 1, 0, 0);

	mCommandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			pCurrBackBuffer,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT
		)
	);

	CheckHResult(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	return true;
}
