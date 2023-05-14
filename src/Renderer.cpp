#include "Renderer.h"
#include "Logger.h"
#include "RenderMacros.h"
#include "D3D12Util.h"
#include "AccelerationStructure.h"
#include "ShaderTable.h"
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
#include "GaussianFilter.h"
#include "GaussianFilterCS.h"
#include "GaussianFilter3x3CS.h"
#include "ShadingHelpers.h"

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

namespace ShaderArgs {
	namespace ScreenSpaceAO {
		float OcclusionRadius = 0.5f;
		float OcclusionFadeStart = 0.2f;
		float OcclusionFadeEnd = 2.0f;
		float OcclusionEpsilon = 0.05f;
		float DotThreshold = 0.95f;
		float DepthThreshold = 0.5f;
		int BlurCount = 3;
	}

	namespace RaytracedAO {
		float OcclusionRadius = 10.0f;
		float OcclusionFadeStart = 1.0f;
		float OcclusionFadeEnd = 100.0f;
		float OcclusionEpsilon = 0.05f;
		UINT SampleCount = 2;
		bool QuarterResolutionAO = false;
		float MaxRayHitTime = 22.0f;
	}

	namespace DxrShadow {
		int BlurCount = 3;
	}

	namespace Denoiser {
		bool UseSmoothingVariance = false;
		bool LowTspp = true;
		UINT LowTsppBlurPasses = 3;

		namespace TemporalSupersampling {
			UINT MaxTspp = 33;

			namespace ClampCachedValues {
				BOOL UseClamping = true;
				float StdDevGamma = 0.6f;
				float MinStdDevTolerance = 0.05f;
				float DepthSigma = 1.0f;
			}

			float ClampDifferenceToTsppScale = 4.0f;
			UINT MinTsppToUseTemporalVariance = 4;
			UINT LowTsppMaxTspp = 12;
			float LowTsppDecayConstant = 1.0f;
		}

		namespace AtrousWaveletTransformFilter {
			float ValueSigma = 1.0f;
			float DepthSigma = 1.0f;
			float DepthWeightCutoff = 0.2f;
			float NormalSigma = 64;
			float MinVarianceToDenoise = 0.0f;
			bool UseSmoothedVariance = false;
			bool PerspectiveCorrectDepthInterpolation = true;
			bool UseAdaptiveKernelSize = true;
			bool KernelRadiusRotateKernelEnabled = true;
			int KernelRadiusRotateKernelNumCycles = 3;
			int FilterMinKernelWidth = 3;
			float FilterMaxKernelWidthPercentage = 1.5f;
			float AdaptiveKernelSizeRayHitDistanceScaleFactor = 0.02f;
			float AdaptiveKernelSizeRayHitDistanceScaleExponent = 2.0f;

		}
	}
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

	mGaussianFilter = std::make_unique<GaussianFilter::GaussianFilterClass>();
	mGaussianFilterCS = std::make_unique<GaussianFilterCS::GaussianFilterCSClass>();
	mGaussianFilter3x3CS = std::make_unique<GaussianFilter3x3CS::GaussianFilter3x3CSClass>();
	mGBuffer = std::make_unique<GBuffer::GBufferClass>();
	mShadow = std::make_unique<Shadow::ShadowClass>();
	mSsao = std::make_unique<Ssao::SsaoClass>();
	mDxrShadow = std::make_unique<DxrShadow::DxrShadowClass>();
	mRtao = std::make_unique<Rtao::RtaoClass>();

	mDXROutputs.resize(gNumFrameResources);

	bCheckerboardSamplingEnabled = false;
	bCheckerboardGenerateRaysForEvenPixels = false;

	mDebugDisplayMapInfos.reserve(DebugShadeParams::MapCount);
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
	const auto shaderManager = mShaderManager.get();
	
	CheckIsValid(mShadow->Initialize(device, shaderManager, 2048, 2048));
	CheckIsValid(mGBuffer->Initialize(device, shaderManager, width, height));
	CheckIsValid(mSsao->Initialize(device, cmdList, shaderManager, width, height, 1));
	CheckIsValid(mDxrShadow->Initialize(device, cmdList, shaderManager, width, height));
	CheckIsValid(mRtao->Initialize(device, cmdList, shaderManager, width, height));

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
	CheckIsValid(UpdateDebugCB(gt));
	CheckIsValid(UpdateShadowPassCB(gt));
	CheckIsValid(UpdateMaterialCB(gt));
	CheckIsValid(UpdateBlurPassCB(gt));
	if (!bRaytracing) {
		CheckIsValid(UpdateSsaoPassCB(gt));
	}
	else {
		CheckIsValid(UpdateRtaoPassCB(gt));
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
	CheckIsValid(mDxrShadow->OnResize(pCmdList, width, height));
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
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount + GBuffer::Resources::Count + Ssao::NumRenderTargets;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	CheckHResult(md3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 1 + Shadow::Resources::Count;
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
		const auto filePath = ShaderFilePathW + L"NonFloatingPointMapDebug.hlsl";
		auto vsInfo = D3D12ShaderInfo(filePath.c_str(), L"VS", L"vs_6_3");
		auto psInfo = D3D12ShaderInfo(filePath.c_str(), L"PS", L"ps_6_3");
		CheckIsValid(mShaderManager->CompileShader(vsInfo, "nonFPDebugVS"));
		CheckIsValid(mShaderManager->CompileShader(psInfo, "nonFPDebugPS"));
	}
	{
		const auto filePath = ShaderFilePathW + L"Gizmo.hlsl";
		auto vsInfo = D3D12ShaderInfo(filePath.c_str(), L"VS", L"vs_6_3");
		auto psInfo = D3D12ShaderInfo(filePath.c_str(), L"PS", L"ps_6_3");
		CheckIsValid(mShaderManager->CompileShader(vsInfo, "gizmoVS"));
		CheckIsValid(mShaderManager->CompileShader(psInfo, "gizmoPS"));
	}
	{
		const auto filePath = ShaderFilePathW + L"BackBuffer.hlsl";
		auto vsInfo = D3D12ShaderInfo(filePath.c_str(), L"VS", L"vs_6_3");
		auto psInfo = D3D12ShaderInfo(filePath.c_str(), L"PS", L"ps_6_3");
		CheckIsValid(mShaderManager->CompileShader(vsInfo, "backBufferVS"));
		CheckIsValid(mShaderManager->CompileShader(psInfo, "backBufferPS"));
	}
	{
		const auto filePath = ShaderFilePathW + L"DxrBackBuffer.hlsl";
		auto vsInfo = D3D12ShaderInfo(filePath.c_str(), L"VS", L"vs_6_3");
		auto psInfo = D3D12ShaderInfo(filePath.c_str(), L"PS", L"ps_6_3");
		CheckIsValid(mShaderManager->CompileShader(vsInfo, "dxrBackBufferVS"));
		CheckIsValid(mShaderManager->CompileShader(psInfo, "dxrBackBufferPS"));
	}
	{
		const auto filePath = ShaderFilePathW + L"Rtao.hlsl";
		auto shaderInfo = D3D12ShaderInfo(filePath.c_str(), L"", L"lib_6_3");
		CheckIsValid(mShaderManager->CompileShader(shaderInfo, "rtao"));
	}
	CheckIsValid(mShadow->CompileShaders(ShaderFilePathW));
	CheckIsValid(mGBuffer->CompileShaders(ShaderFilePathW));
	CheckIsValid(mGaussianFilter->CompileShaders(mShaderManager.get(), ShaderFilePathW));
	CheckIsValid(mGaussianFilterCS->CompileShaders(mShaderManager.get(), ShaderFilePathW));
	CheckIsValid(mGaussianFilter3x3CS->CompileShaders(mShaderManager.get(), ShaderFilePathW));
	CheckIsValid(mSsao->CompileShaders(ShaderFilePathW));
	CheckIsValid(mDxrShadow->CompileShaders(ShaderFilePathW));
	CheckIsValid(mRtao->CompileShaders(ShaderFilePathW));

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
	auto samplers = Samplers::GetStaticSamplers();
	//
	// Rasterization
	//
	// Drawing back-buffer
	{
		CD3DX12_ROOT_PARAMETER slotRootParameter[BackBuffer::RootSignatureLayout::Count];

		CD3DX12_DESCRIPTOR_RANGE texTables[7];
		texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
		texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0);
		texTables[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);
		texTables[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 0);
		texTables[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4, 0);
		texTables[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5, 0);
		texTables[6].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 6, 0);

		slotRootParameter[BackBuffer::RootSignatureLayout::ECB_Pass].InitAsConstantBufferView(0);
		slotRootParameter[BackBuffer::RootSignatureLayout::ESI_Color].InitAsDescriptorTable(1, &texTables[0]);
		slotRootParameter[BackBuffer::RootSignatureLayout::ESI_Albedo].InitAsDescriptorTable(1, &texTables[1]);
		slotRootParameter[BackBuffer::RootSignatureLayout::ESI_Normal].InitAsDescriptorTable(1, &texTables[2]);
		slotRootParameter[BackBuffer::RootSignatureLayout::ESI_Depth].InitAsDescriptorTable(1, &texTables[3]);
		slotRootParameter[BackBuffer::RootSignatureLayout::ESI_Specular].InitAsDescriptorTable(1, &texTables[4]);
		slotRootParameter[BackBuffer::RootSignatureLayout::ESI_Shadow].InitAsDescriptorTable(1, &texTables[5]);
		slotRootParameter[BackBuffer::RootSignatureLayout::ESI_AmbientCoefficient].InitAsDescriptorTable(1, &texTables[6]);

		CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
			_countof(slotRootParameter), slotRootParameter,
			static_cast<UINT>(samplers.size()), samplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
		);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice.Get(), rootSigDesc, mRootSignatures["backBuffer"].GetAddressOf()));
	}
	// Gizmo
	{
		CD3DX12_ROOT_PARAMETER slotRootParameter[Gizmo::RootSignatureLayout::Count];
		
		slotRootParameter[Gizmo::RootSignatureLayout::ECB_Pass].InitAsConstantBufferView(0, 0);

		CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
			_countof(slotRootParameter), slotRootParameter,
			static_cast<UINT>(samplers.size()), samplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
		);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice.Get(), rootSigDesc, mRootSignatures["gizmo"].GetAddressOf()));
	}
	// Debug
	{
		CD3DX12_ROOT_PARAMETER slotRootParameter[Debug::RootSignatureLayout::Count];

		CD3DX12_DESCRIPTOR_RANGE texTables[5];
		texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
		texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0);
		texTables[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);
		texTables[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 0);
		texTables[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4, 0);

		slotRootParameter[Debug::RootSignatureLayout::ECB_Debug].InitAsConstantBufferView(0);
		slotRootParameter[Debug::RootSignatureLayout::EC_Consts].InitAsConstants(Debug::RootConstantsLayout::Count, 1);
		slotRootParameter[Debug::RootSignatureLayout::ESI_Debug0].InitAsDescriptorTable(1, &texTables[0]);
		slotRootParameter[Debug::RootSignatureLayout::ESI_Debug1].InitAsDescriptorTable(1, &texTables[1]);
		slotRootParameter[Debug::RootSignatureLayout::ESI_Debug2].InitAsDescriptorTable(1, &texTables[2]);
		slotRootParameter[Debug::RootSignatureLayout::ESI_Debug3].InitAsDescriptorTable(1, &texTables[3]);
		slotRootParameter[Debug::RootSignatureLayout::ESI_Debug4].InitAsDescriptorTable(1, &texTables[4]);

		CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
			_countof(slotRootParameter), slotRootParameter,
			static_cast<UINT>(samplers.size()), samplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
		);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice.Get(), rootSigDesc, mRootSignatures["debug"].GetAddressOf()));
	}
	// Non floating point map debug
	{
		CD3DX12_ROOT_PARAMETER slotRootParameter[NonFloatingPointMapDebug::RootSignatureLayout::Count];

		CD3DX12_DESCRIPTOR_RANGE texTables[2];
		texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0);
		texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1);

		slotRootParameter[NonFloatingPointMapDebug::RootSignatureLayout::ECB_Debug].InitAsConstantBufferView(0);
		slotRootParameter[NonFloatingPointMapDebug::RootSignatureLayout::EC_Consts].InitAsConstants(NonFloatingPointMapDebug::RootConstantsLayout::Count, 1);
		slotRootParameter[NonFloatingPointMapDebug::RootSignatureLayout::ESI_TsppAOCoefficientSquaredMeanRayHitDistance].InitAsDescriptorTable(1, &texTables[0]);
		slotRootParameter[NonFloatingPointMapDebug::RootSignatureLayout::ESI_Tspp].InitAsDescriptorTable(1, &texTables[1]);

		CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
			_countof(slotRootParameter), slotRootParameter,
			static_cast<UINT>(samplers.size()), samplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
		);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice.Get(), rootSigDesc, mRootSignatures["nonFPDebug"].GetAddressOf()));
	}
	// Screen-space ambient occlusion
	
	//
	// Raytracing
	//
	// Drawing DXR back-buffer
	{
		CD3DX12_ROOT_PARAMETER slotRootParameter[DxrBackBuffer::RootSignatureLayout::Count];

		CD3DX12_DESCRIPTOR_RANGE texTables[7];
		texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 0, 0);
		texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 1, 0);
		texTables[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 2, 0);
		texTables[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 3, 0);
		texTables[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 4, 0);
		texTables[5].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 5, 0);
		texTables[6].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 1, 6, 0);

		slotRootParameter[DxrBackBuffer::RootSignatureLayout::ECB_Pass].InitAsConstantBufferView(0);
		slotRootParameter[DxrBackBuffer::RootSignatureLayout::ESI_Color].InitAsDescriptorTable(1, &texTables[0]);
		slotRootParameter[DxrBackBuffer::RootSignatureLayout::ESI_Albedo].InitAsDescriptorTable(1, &texTables[1]);
		slotRootParameter[DxrBackBuffer::RootSignatureLayout::ESI_Normal].InitAsDescriptorTable(1, &texTables[2]);
		slotRootParameter[DxrBackBuffer::RootSignatureLayout::ESI_Depth].InitAsDescriptorTable(1, &texTables[3]);
		slotRootParameter[DxrBackBuffer::RootSignatureLayout::ESI_Specular].InitAsDescriptorTable(1, &texTables[4]);
		slotRootParameter[DxrBackBuffer::RootSignatureLayout::ESI_Shadow].InitAsDescriptorTable(1, &texTables[5]);
		slotRootParameter[DxrBackBuffer::RootSignatureLayout::ESI_AmbientCoefficient].InitAsDescriptorTable(1, &texTables[6]);

		CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(
			_countof(slotRootParameter), slotRootParameter,
			static_cast<UINT>(samplers.size()), samplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_NONE
		);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice.Get(), globalRootSignatureDesc, mRootSignatures["dxrBackBuffer"].GetAddressOf()));
	}
	// Default local root signature
	{
		CD3DX12_ROOT_SIGNATURE_DESC localRootSignatureDesc(0, nullptr);
		localRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice.Get(), localRootSignatureDesc, mRootSignatures["dxr_local"].GetAddressOf()));
	}

	CheckIsValid(mGBuffer->BuildRootSignature(samplers));
	CheckIsValid(mShadow->BuildRootSignatures(samplers));
	CheckIsValid(mSsao->BuildRootSignature(samplers));
	CheckIsValid(mGaussianFilter->BuildRootSignature(md3dDevice.Get(), samplers));
	CheckIsValid(mGaussianFilterCS->BuildRootSignature(md3dDevice.Get(), samplers));
	CheckIsValid(mGaussianFilter3x3CS->BuildRootSignature(md3dDevice.Get(), samplers));
	CheckIsValid(mDxrShadow->BuildRootSignatures(samplers, gNumGeometryBuffers));
	CheckIsValid(mRtao->BuildRootSignatures(samplers));

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

	auto cpuDesc = CD3DX12_CPU_DESCRIPTOR_HANDLE(pDescHeap->GetCPUDescriptorHandleForHeapStart()).Offset(EDescriptors::Count , descSize);
	auto gpuDesc = CD3DX12_GPU_DESCRIPTOR_HANDLE(pDescHeap->GetGPUDescriptorHandleForHeapStart()).Offset(EDescriptors::Count, descSize);
	auto rtvCpuDesc = CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvHeap->GetCPUDescriptorHandleForHeapStart()).Offset(SwapChainBufferCount, rtvDescSize);
	auto dsvCpuDesc = CD3DX12_CPU_DESCRIPTOR_HANDLE(mDsvHeap->GetCPUDescriptorHandleForHeapStart()).Offset(1, dsvDescSize);

	mShadow->BuildDescriptors(cpuDesc, gpuDesc, dsvCpuDesc, descSize, dsvDescSize);
	mGBuffer->BuildDescriptors(cpuDesc, gpuDesc, rtvCpuDesc, descSize, rtvDescSize, mDepthStencilBuffer.Get());
	mDxrShadow->BuildDescriptors(cpuDesc, gpuDesc, descSize);
	mSsao->BuildDescriptors(cpuDesc, gpuDesc, rtvCpuDesc, descSize, rtvDescSize);
	mRtao->BuildDescriptors(cpuDesc, gpuDesc, descSize);	

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
	backBufferPsoDesc.pRootSignature = mRootSignatures["backBuffer"].Get();
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
	gizmoPsoDesc.pRootSignature = mRootSignatures["gizmo"].Get();
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

	D3D12_GRAPHICS_PIPELINE_STATE_DESC debugPsoDesc = quadPsoDesc;
	debugPsoDesc.pRootSignature = mRootSignatures["debug"].Get();
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

	D3D12_GRAPHICS_PIPELINE_STATE_DESC nonFPDebugPsoDesc = quadPsoDesc;
	nonFPDebugPsoDesc.pRootSignature = mRootSignatures["nonFPDebug"].Get();
	{
		auto vs = mShaderManager->GetDxcShader("nonFPDebugVS");
		auto ps = mShaderManager->GetDxcShader("nonFPDebugPS");
		nonFPDebugPsoDesc.VS = {
			reinterpret_cast<BYTE*>(vs->GetBufferPointer()),
			vs->GetBufferSize()
		};
		nonFPDebugPsoDesc.PS = {
			reinterpret_cast<BYTE*>(ps->GetBufferPointer()),
			ps->GetBufferSize()
		};
	}
	nonFPDebugPsoDesc.RTVFormats[0] = BackBufferFormat;
	CheckHResult(md3dDevice->CreateGraphicsPipelineState(&nonFPDebugPsoDesc, IID_PPV_ARGS(&mPSOs["nonFPDebug"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC dxrBackBufferPsoDesc = quadPsoDesc;
	dxrBackBufferPsoDesc.pRootSignature = mRootSignatures["dxrBackBuffer"].Get();
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
	
	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc = { inputLayout.data(), static_cast<UINT>(inputLayout.size()) };
	CheckIsValid(mShadow->BuildPSO(inputLayoutDesc, DepthStencilFormat));
	CheckIsValid(mGBuffer->BuildPso(inputLayoutDesc, DepthStencilFormat));
	CheckIsValid(mGaussianFilter->BuildPso(md3dDevice.Get(), mShaderManager.get()));
	CheckIsValid(mGaussianFilterCS->BuildPso(md3dDevice.Get(), mShaderManager.get()));
	CheckIsValid(mGaussianFilter3x3CS->BuildPso(md3dDevice.Get(), mShaderManager.get()));
	CheckIsValid(mSsao->BuildPso());
	CheckIsValid(mRtao->BuildPSO());

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
		mRitems[RenderItem::RenderType::EOpaque].push_back(sphereRitem.get());
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
		mRitems[RenderItem::RenderType::EOpaque].push_back(sphereRitem.get());
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
		mRitems[RenderItem::RenderType::EOpaque].push_back(sphereRitem.get());
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
		mRitems[RenderItem::RenderType::EOpaque].push_back(gridRitem.get());
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
	CheckIsValid(mDxrShadow->BuildDXRPSO());
	CheckIsValid(mRtao->BuildDXRPSO());

	return true;
}

bool Renderer::BuildShaderTables() {
	CheckIsValid(mDxrShadow->BuildShaderTables());
	CheckIsValid(mRtao->BuildShaderTables());

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

bool Renderer::UpdateDebugCB(const GameTimer& gt) {
	DebugConstants debugCB;

	debugCB.RtaoOcclusionRadius = ShaderArgs::RaytracedAO::OcclusionRadius;
	debugCB.MaxTspp = ShaderArgs::Denoiser::TemporalSupersampling::MaxTspp;

	auto& currDebugCB = mCurrFrameResource->DebugCB;
	currDebugCB.CopyData(0, debugCB);

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
	ssaoCB.OcclusionRadius = ShaderArgs::ScreenSpaceAO::OcclusionRadius;
	ssaoCB.OcclusionFadeStart = ShaderArgs::ScreenSpaceAO::OcclusionFadeStart;
	ssaoCB.OcclusionFadeEnd = ShaderArgs::ScreenSpaceAO::OcclusionFadeEnd;
	ssaoCB.SurfaceEpsilon = ShaderArgs::ScreenSpaceAO::OcclusionEpsilon;

	auto& currSsaoCB = mCurrFrameResource->SsaoCB;
	currSsaoCB.CopyData(0, ssaoCB);

	return true;
}

bool Renderer::UpdateRtaoPassCB(const GameTimer& gt) {
	// Ambient occlusion
	{
		static UINT count = 0;
		static auto prev = mMainPassCB->View;

		RtaoConstants rtaoCB;
		rtaoCB.View = mMainPassCB->View;
		rtaoCB.InvView = mMainPassCB->InvView;
		rtaoCB.Proj = mMainPassCB->Proj;
		rtaoCB.InvProj = mMainPassCB->InvProj;

		// Coordinates given in view space.
		rtaoCB.OcclusionRadius = ShaderArgs::RaytracedAO::OcclusionRadius;
		rtaoCB.OcclusionFadeStart = ShaderArgs::RaytracedAO::OcclusionFadeStart;
		rtaoCB.OcclusionFadeEnd = ShaderArgs::RaytracedAO::OcclusionFadeEnd;
		rtaoCB.SurfaceEpsilon = ShaderArgs::RaytracedAO::OcclusionEpsilon;

		rtaoCB.FrameCount = count++;
		rtaoCB.SampleCount = ShaderArgs::RaytracedAO::SampleCount;
		
		prev = mMainPassCB->View;

		auto& currRtaoCB = mCurrFrameResource->RtaoCB;
		currRtaoCB.CopyData(0, rtaoCB);
	}
	// Calculate local mean/variance
	{
		CalcLocalMeanVarianceConstants calcLocalMeanVarCB;

		bCheckerboardGenerateRaysForEvenPixels = !bCheckerboardGenerateRaysForEvenPixels;

		calcLocalMeanVarCB.TextureDim = { mRtao->Width(), mRtao->Height() };
		calcLocalMeanVarCB.KernelWidth = 9;
		calcLocalMeanVarCB.KernelRadius = 9 >> 1;

		calcLocalMeanVarCB.CheckerboardSamplingEnabled = bCheckerboardSamplingEnabled;
		calcLocalMeanVarCB.EvenPixelActivated = bCheckerboardGenerateRaysForEvenPixels;
		calcLocalMeanVarCB.PixelStepY = bCheckerboardSamplingEnabled ? 2 : 1;

		auto& currLocalCalcMeanVarCB = mCurrFrameResource->CalcLocalMeanVarCB;
		currLocalCalcMeanVarCB.CopyData(0, calcLocalMeanVarCB);
	}
	// Temporal supersampling reverse reproject
	{
		CrossBilateralFilterConstants filterCB;
		filterCB.DepthSigma = 1.0f;
		filterCB.DepthNumMantissaBits = D3D12Util::NumMantissaBitsInFloatFormat(16);

		auto& currFilterCB = mCurrFrameResource->CrossBilateralFilterCB;
		currFilterCB.CopyData(0, filterCB);
	}
	// Temporal supersampling blend with current frame
	{
		TemporalSupersamplingBlendWithCurrentFrameConstants tsppBlendCB;
		tsppBlendCB.StdDevGamma = ShaderArgs::Denoiser::TemporalSupersampling::ClampCachedValues::StdDevGamma;
		tsppBlendCB.ClampCachedValues = ShaderArgs::Denoiser::TemporalSupersampling::ClampCachedValues::UseClamping;
		tsppBlendCB.ClampingMinStdDevTolerance = ShaderArgs::Denoiser::TemporalSupersampling::ClampCachedValues::MinStdDevTolerance;

		tsppBlendCB.ClampDifferenceToTsppScale = ShaderArgs::Denoiser::TemporalSupersampling::ClampDifferenceToTsppScale;
		tsppBlendCB.ForceUseMinSmoothingFactor = false;
		tsppBlendCB.MinSmoothingFactor = 1.0f / ShaderArgs::Denoiser::TemporalSupersampling::MaxTspp;
		tsppBlendCB.MinTsppToUseTemporalVariance = ShaderArgs::Denoiser::TemporalSupersampling::MinTsppToUseTemporalVariance;

		tsppBlendCB.BlurStrengthMaxTspp = ShaderArgs::Denoiser::TemporalSupersampling::LowTsppMaxTspp;
		tsppBlendCB.BlurDecayStrength = ShaderArgs::Denoiser::TemporalSupersampling::LowTsppDecayConstant;
		tsppBlendCB.CheckerboardEnabled = bCheckerboardSamplingEnabled;
		tsppBlendCB.CheckerboardEvenPixelActivated = bCheckerboardGenerateRaysForEvenPixels;

		auto& currTsppBlendCB = mCurrFrameResource->TsppBlendCB;
		currTsppBlendCB.CopyData(0, tsppBlendCB);
	}
	// Atrous wavelet transform filter
	{
		AtrousWaveletTransformFilterConstantBuffer atrousFilterCB;

		// Adaptive kernel radius rotation.
		float kernelRadiusLerfCoef = 0;
		if (ShaderArgs::Denoiser::AtrousWaveletTransformFilter::KernelRadiusRotateKernelEnabled) {
			static UINT frameID = 0;
			UINT i = frameID++ % ShaderArgs::Denoiser::AtrousWaveletTransformFilter::KernelRadiusRotateKernelNumCycles;
			kernelRadiusLerfCoef = i / static_cast<float>(ShaderArgs::Denoiser::AtrousWaveletTransformFilter::KernelRadiusRotateKernelNumCycles);
		}

		atrousFilterCB.TextureDim = XMUINT2(mRtao->Width(), mRtao->Height());
		atrousFilterCB.DepthWeightCutoff = ShaderArgs::Denoiser::AtrousWaveletTransformFilter::DepthWeightCutoff;
		atrousFilterCB.UsingBilateralDownsamplingBuffers = ShaderArgs::RaytracedAO::QuarterResolutionAO;

		atrousFilterCB.UseAdaptiveKernelSize = ShaderArgs::Denoiser::AtrousWaveletTransformFilter::UseAdaptiveKernelSize;
		atrousFilterCB.KernelRadiusLerfCoef = kernelRadiusLerfCoef;
		atrousFilterCB.MinKernelWidth = ShaderArgs::Denoiser::AtrousWaveletTransformFilter::FilterMinKernelWidth;
		atrousFilterCB.MaxKernelWidth = static_cast<UINT>((ShaderArgs::Denoiser::AtrousWaveletTransformFilter::FilterMaxKernelWidthPercentage / 100) * mRtao->Width());

		atrousFilterCB.RayHitDistanceToKernelWidthScale = 22 / ShaderArgs::RaytracedAO::MaxRayHitTime * ShaderArgs::Denoiser::AtrousWaveletTransformFilter::AdaptiveKernelSizeRayHitDistanceScaleFactor;
		atrousFilterCB.RayHitDistanceToKernelSizeScaleExponent = Lerp(
			1, 
			ShaderArgs::Denoiser::AtrousWaveletTransformFilter::AdaptiveKernelSizeRayHitDistanceScaleExponent, 
			RelativeCoef(ShaderArgs::RaytracedAO::MaxRayHitTime, 4, 22)
		);
		atrousFilterCB.PerspectiveCorrectDepthInterpolation = ShaderArgs::Denoiser::AtrousWaveletTransformFilter::PerspectiveCorrectDepthInterpolation;
		atrousFilterCB.MinVarianceToDenoise = ShaderArgs::Denoiser::AtrousWaveletTransformFilter::MinVarianceToDenoise;

		atrousFilterCB.ValueSigma = ShaderArgs::Denoiser::AtrousWaveletTransformFilter::ValueSigma;
		atrousFilterCB.DepthSigma = ShaderArgs::Denoiser::AtrousWaveletTransformFilter::DepthSigma;
		atrousFilterCB.NormalSigma = ShaderArgs::Denoiser::AtrousWaveletTransformFilter::NormalSigma;
		atrousFilterCB.FovY = mCamera->FovY();

		auto& currAtrousFilterCB = mCurrFrameResource->AtrousFilterCB;
		currAtrousFilterCB.CopyData(0, atrousFilterCB);
	}

	return true;
}

bool Renderer::Rasterize() {
	CheckIsValid(DrawShadowMap());
	CheckIsValid(DrawGBuffer());
	CheckIsValid(DrawSsao());
	CheckIsValid(DrawBackBuffer());

	return true;
}

bool Renderer::DrawShadowMap() {
	const auto cmdList = mCommandList.Get();

	CheckHResult(cmdList->Reset(mCurrFrameResource->CmdListAlloc.Get(), nullptr));

	const auto pDeshHeap = mCbvSrvUavHeap.Get();
	ID3D12DescriptorHeap* descriptorHeaps[] = { pDeshHeap };
	cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	const auto pShadowMap = mShadow->Resource();
	cmdList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			pShadowMap,
			D3D12_RESOURCE_STATE_DEPTH_READ,
			D3D12_RESOURCE_STATE_DEPTH_WRITE
		)
	);

	UINT passCBByteSize = D3D12Util::CalcConstantBufferByteSize(sizeof(PassConstants));
	D3D12_GPU_VIRTUAL_ADDRESS shadowPassCBAddress = mCurrFrameResource->PassCB.Resource()->GetGPUVirtualAddress() + 1 * passCBByteSize;

	mShadow->Run(cmdList,
		shadowPassCBAddress,
		mCurrFrameResource->ObjectSB.Resource()->GetGPUVirtualAddress(),
		mCurrFrameResource->MaterialSB.Resource()->GetGPUVirtualAddress(),
		mRitems[RenderItem::RenderType::EOpaque]
	);
	
	cmdList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			pShadowMap,
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			D3D12_RESOURCE_STATE_DEPTH_READ
		)
	);

	CheckHResult(cmdList->Close());
	ID3D12CommandList* cmdsLists[] = { cmdList };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	return true;
}

bool Renderer::DrawGBuffer() {
	const auto cmdList = mCommandList.Get();

	CheckHResult(cmdList->Reset(mCurrFrameResource->CmdListAlloc.Get(), nullptr));

	ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvSrvUavHeap.Get() };
	cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	cmdList ->RSSetViewports(1, &mScreenViewport);
	cmdList ->RSSetScissorRects(1, &mScissorRect);

	const auto& resources = mGBuffer->Resources();
	const auto& cpuDescriptors = mGBuffer->ResourcesCpuDescriptors();
	const auto& gpuDescriptors = mGBuffer->ResourcesGpuDescriptors();

	const auto pColorMap = resources[GBuffer::Resources::EColor].Get();
	const auto pAlbedoMap = resources[GBuffer::Resources::EAlbedo].Get();
	const auto pNormalDepthMap = resources[GBuffer::Resources::ENormalDepth].Get();
	const auto pSpecularMap = resources[GBuffer::Resources::ESpecular].Get();
	const auto pVelocityMap = resources[GBuffer::Resources::EVelocity].Get();
	const auto pReprojNormalDepthMap = resources[GBuffer::Resources::EReprojectedNormalDepth].Get();
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
				pReprojNormalDepthMap,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_RENDER_TARGET
			)
		};
		cmdList->ResourceBarrier(
			_countof(barriers),
			barriers
		);
	}

	mGBuffer->Run(
		cmdList, 
		DepthStencilView(), 
		mCurrFrameResource->PassCB.Resource()->GetGPUVirtualAddress(),
		mCurrFrameResource->ObjectSB.Resource()->GetGPUVirtualAddress(),
		mCurrFrameResource->MaterialSB.Resource()->GetGPUVirtualAddress(),
		mRitems[RenderItem::RenderType::EOpaque]
	);

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
				pReprojNormalDepthMap,
				D3D12_RESOURCE_STATE_RENDER_TARGET,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
			)
		};
		cmdList->ResourceBarrier(
			_countof(barriers),
			barriers
		);
	}

	CheckHResult(cmdList->Close());
	ID3D12CommandList* cmdsLists[] = { cmdList };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	return true;
}

bool Renderer::DrawSsao() {
	const auto cmdList = mCommandList.Get();

	CheckHResult(cmdList->Reset(mCurrFrameResource->CmdListAlloc.Get(), nullptr));

	const auto pDescHeap = mCbvSrvUavHeap.Get();
	auto descSize = GetCbvSrvUavDescriptorSize();

	ID3D12DescriptorHeap* descriptorHeaps[] = { pDescHeap };
	cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	const auto& gbufferGpuDescriptors = mGBuffer->ResourcesGpuDescriptors();
	const auto& resources = mSsao->Resources();
	const auto& cpuDescriptors = mSsao->ResourcesCpuDescriptors();
	const auto& gpuDescriptors = mSsao->ResourcesGpuDescriptors();

	auto rawAmbientCoefficient = resources[Ssao::Resources::EAmbientCoefficient].Get();
	cmdList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			rawAmbientCoefficient,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
			D3D12_RESOURCE_STATE_RENDER_TARGET
		)
	);

	auto ssaoCBAddress = mCurrFrameResource->SsaoCB.Resource()->GetGPUVirtualAddress();
	mSsao->Run(
		cmdList,
		ssaoCBAddress,
		gbufferGpuDescriptors[GBuffer::Resources::Descriptors::ES_NormalDepth]
	);

	// Change back to GENERIC_READ so we can read the texture in a shader.
	cmdList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			rawAmbientCoefficient,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		)
	);

	float values[] = { ShaderArgs::ScreenSpaceAO::DotThreshold, ShaderArgs::ScreenSpaceAO::DepthThreshold };
	mGaussianFilter->Run(
		cmdList,
		mCurrFrameResource->BlurCB.Resource()->GetGPUVirtualAddress(),
		gbufferGpuDescriptors[GBuffer::Resources::Descriptors::ES_NormalDepth],
		resources[Ssao::Resources::EAmbientCoefficient].Get(),
		resources[Ssao::Resources::ETemporary].Get(),
		cpuDescriptors[Ssao::Resources::Descriptors::ER_AmbientCoefficient],
		gpuDescriptors[Ssao::Resources::Descriptors::ES_AmbientCoefficient],
		cpuDescriptors[Ssao::Resources::Descriptors::ER_Temporary],
		gpuDescriptors[Ssao::Resources::Descriptors::ES_Temporary],
		values,
		GaussianFilter::FilterType::R16,
		ShaderArgs::ScreenSpaceAO::BlurCount
	);

	CheckHResult(cmdList->Close());
	ID3D12CommandList* cmdsLists[] = { cmdList };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	return true;
}

bool Renderer::DrawBackBuffer() {
	const auto cmdList = mCommandList.Get();
	CheckHResult(cmdList->Reset(mCurrFrameResource->CmdListAlloc.Get(), mPSOs["backBuffer"].Get()));
	cmdList->SetGraphicsRootSignature(mRootSignatures["backBuffer"].Get());

	const auto pDescHeap = mCbvSrvUavHeap.Get();
	ID3D12DescriptorHeap* descriptorHeaps[] = { pDescHeap };
	cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	cmdList ->RSSetViewports(1, &mScreenViewport);
	cmdList ->RSSetScissorRects(1, &mScissorRect);

	const auto& gbufferResourcesGpuDescriptors = mGBuffer->ResourcesGpuDescriptors();
	const auto& aoResourcesGpuDescriptors = mSsao->ResourcesGpuDescriptors();

	const auto pCurrBackBuffer = CurrentBackBuffer();
	cmdList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			pCurrBackBuffer,
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET
		)
	);

	auto pCurrBackBufferView = CurrentBackBufferView();
	cmdList->OMSetRenderTargets(1, &pCurrBackBufferView, true, nullptr);

	auto passCBAddress = mCurrFrameResource->PassCB.Resource()->GetGPUVirtualAddress();
	cmdList->SetGraphicsRootConstantBufferView(BackBuffer::RootSignatureLayout::ECB_Pass, passCBAddress);
	
	cmdList->SetGraphicsRootDescriptorTable(
		BackBuffer::RootSignatureLayout::ESI_Color,
		gbufferResourcesGpuDescriptors[GBuffer::Resources::Descriptors::ES_Color]
	);
	cmdList->SetGraphicsRootDescriptorTable(
		BackBuffer::RootSignatureLayout::ESI_Albedo,
		gbufferResourcesGpuDescriptors[GBuffer::Resources::Descriptors::ES_Albedo]
	);
	cmdList->SetGraphicsRootDescriptorTable(
		BackBuffer::RootSignatureLayout::ESI_Normal,
		gbufferResourcesGpuDescriptors[GBuffer::Resources::Descriptors::ES_NormalDepth]
	);
	cmdList->SetGraphicsRootDescriptorTable(
		BackBuffer::RootSignatureLayout::ESI_Depth,
		gbufferResourcesGpuDescriptors[GBuffer::Resources::Descriptors::ES_Depth]
	);
	cmdList->SetGraphicsRootDescriptorTable(
		BackBuffer::RootSignatureLayout::ESI_Specular,
		gbufferResourcesGpuDescriptors[GBuffer::Resources::Descriptors::ES_Specular]
	);
	cmdList->SetGraphicsRootDescriptorTable(
		BackBuffer::RootSignatureLayout::ESI_Shadow,
		mShadow->Srv()
	);
	cmdList->SetGraphicsRootDescriptorTable(
		BackBuffer::RootSignatureLayout::ESI_AmbientCoefficient,
		aoResourcesGpuDescriptors[Ssao::Resources::Descriptors::ES_AmbientCoefficient]
	);

	cmdList->IASetVertexBuffers(0, 0, nullptr);
	cmdList->IASetIndexBuffer(nullptr);
	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
	cmdList->DrawInstanced(6, 1, 0, 0);

	cmdList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			pCurrBackBuffer,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT
		)
	);

	CheckHResult(cmdList->Close());
	ID3D12CommandList* cmdsLists[] = { cmdList };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	return true;
}

bool Renderer::DrawDebugLayer() {
	const auto cmdList = mCommandList.Get();

	CheckHResult(cmdList->Reset(mCurrFrameResource->CmdListAlloc.Get(), mPSOs["gizmo"].Get()));
	cmdList->SetGraphicsRootSignature(mRootSignatures["gizmo"].Get());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mCbvSrvUavHeap.Get() };
	cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	cmdList->RSSetViewports(1, &mDebugViewport);
	cmdList->RSSetScissorRects(1, &mDebugScissorRect);
		
	const auto renderTarget = CurrentBackBuffer();
	{
		D3D12_RESOURCE_BARRIER barriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(
				renderTarget,
				D3D12_RESOURCE_STATE_PRESENT,
				D3D12_RESOURCE_STATE_RENDER_TARGET
			),
		};
		cmdList->ResourceBarrier(
			_countof(barriers),
			barriers
		);
	}

	cmdList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, nullptr);

	const auto& passCBAddress = mCurrFrameResource->PassCB.Resource()->GetGPUVirtualAddress();
	cmdList->SetGraphicsRootConstantBufferView(Gizmo::RootSignatureLayout::ECB_Pass, passCBAddress);

	cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
	cmdList->DrawInstanced(2, 3, 0, 0);

	{
		cmdList->RSSetViewports(1, &mScreenViewport);
		cmdList->RSSetScissorRects(1, &mScissorRect);

		const auto debugCBAddress = mCurrFrameResource->DebugCB.Resource()->GetGPUVirtualAddress();

		cmdList->IASetVertexBuffers(0, 0, nullptr);
		cmdList->IASetIndexBuffer(nullptr);
		cmdList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		{
			UINT size = static_cast<UINT>(mDebugDisplayMapInfos.size());
			if (size > 0) {
				cmdList->SetPipelineState(mPSOs["debug"].Get());
				cmdList->SetGraphicsRootSignature(mRootSignatures["debug"].Get());

				cmdList->SetGraphicsRootConstantBufferView(Debug::RootSignatureLayout::ECB_Debug, debugCBAddress);

				UINT values[Debug::RootConstantsLayout::Count];
				for (UINT i = 0; i < size; ++i) {
					cmdList->SetGraphicsRootDescriptorTable(Debug::RootSignatureLayout::ESI_Debug0 + i, mDebugDisplayMapInfos[i].Handle);
					values[i] = mDebugDisplayMapInfos[i].SampleMask;
				}
				cmdList->SetGraphicsRoot32BitConstants(Debug::RootSignatureLayout::EC_Consts, _countof(values), values, 0);
				cmdList->DrawInstanced(6, size, 0, 0);
			}
		}
		{
			auto temporalCurrentFrameResourceIndex = mRtao->TemporalCurrentFrameResourceIndex();

			const auto& temporalCachesGpuDescriptors = mRtao->TemporalCachesGpuDescriptors();

			cmdList->SetPipelineState(mPSOs["nonFPDebug"].Get());
			cmdList->SetGraphicsRootSignature(mRootSignatures["nonFPDebug"].Get());

			cmdList->SetGraphicsRootConstantBufferView(NonFloatingPointMapDebug::RootSignatureLayout::ECB_Debug, debugCBAddress);

			UINT values[NonFloatingPointMapDebug::RootConstantsLayout::Count] = { GetClientWidth(), GetClientHeight() };
			cmdList->SetGraphicsRoot32BitConstants(NonFloatingPointMapDebug::RootSignatureLayout::EC_Consts, _countof(values), values, 0);

			cmdList->SetGraphicsRootDescriptorTable(
				NonFloatingPointMapDebug::RootSignatureLayout::ESI_TsppAOCoefficientSquaredMeanRayHitDistance,
				mRtao->TsppCoefficientSquaredMeanRayHitDistanceSrv()
			);
			cmdList->SetGraphicsRootDescriptorTable(
				NonFloatingPointMapDebug::RootSignatureLayout::ESI_Tspp,
				temporalCachesGpuDescriptors[temporalCurrentFrameResourceIndex][Rtao::TemporalCaches::Descriptors::ES_Tspp]
			);

			cmdList->DrawInstanced(6, 1, 0, 0);
		}
	}

	{
		D3D12_RESOURCE_BARRIER barriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(
				renderTarget,
				D3D12_RESOURCE_STATE_RENDER_TARGET,
				D3D12_RESOURCE_STATE_PRESENT
			),
		};
		cmdList->ResourceBarrier(
			_countof(barriers),
			barriers
		);
	}

	CheckHResult(cmdList->Close());
	ID3D12CommandList* cmdsLists[] = { cmdList };
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

		static const auto BuildDebugDescriptors = [&](bool& mode, D3D12_GPU_DESCRIPTOR_HANDLE handle, UINT mask) {
			DebugDisplayMapInfo mapInfo = { handle, mask };
			if (mode) {
				if (mDebugDisplayMapInfos.size() >= 5) {
					mode = false;
					return;
				}

				mDebugDisplayMapInfos.push_back(mapInfo);
			}
			else {
				auto begin = mDebugDisplayMapInfos.begin();
				auto end = mDebugDisplayMapInfos.end();
				auto iter = std::find(begin, end, mapInfo);
				if (iter == end) return;

				std::iter_swap(iter, end - 1);
				mDebugDisplayMapInfos.pop_back();
			}
		};

		const auto& gbufferResourcesGpuDescriptors = mGBuffer->ResourcesGpuDescriptors();
		const auto& ssaoResourcesGpuDescriptors = mSsao->ResourcesGpuDescriptors();
		const auto& dxrShadowResourcesGpuDescriptors = mDxrShadow->ResourcesGpuDescriptors();
		const auto& rtaoResourcesGpuDescriptors = mRtao->AOResourcesGpuDescriptors();
		const auto& temporalCachesGpuDescriptors = mRtao->TemporalCachesGpuDescriptors();
		const auto& temporalAOCoefficientsGpuDescriptors = mRtao->TemporalAOCoefficientsGpuDescriptors();
		const auto& localMeanVarianceResourcesGpuDescriptors = mRtao->LocalMeanVarianceResourcesGpuDescriptors();
		const auto& aoVarianceResourcesGpuDescriptors = mRtao->AOVarianceResourcesGpuDescriptors();

		auto temporalCurrentFrameResourceIndex = mRtao->TemporalCurrentFrameResourceIndex();
		auto temporalCurrentFrameTemporalAOCoefficientResourceIndex = mRtao->TemporalCurrentFrameTemporalAOCoefficientResourceIndex();
		UINT temporalPrevFrameTemporalAOCoefficientResourceIndex = (temporalCurrentFrameTemporalAOCoefficientResourceIndex + 1) % 2;

		if (ImGui::Checkbox("Color Map", &mDebugDisplays[DebugDisplay::Layout::EColor])) {
			BuildDebugDescriptors(
				std::ref(mDebugDisplays[DebugDisplay::Layout::EColor]),
				gbufferResourcesGpuDescriptors[GBuffer::Resources::Descriptors::ES_Color],
				DebugShadeParams::DisplayMark::RGB
			);
		}
		if (ImGui::Checkbox("Albedo Map", &mDebugDisplays[DebugDisplay::Layout::EAlbedo])) {
			BuildDebugDescriptors(
				std::ref(mDebugDisplays[DebugDisplay::Layout::EAlbedo]),
				gbufferResourcesGpuDescriptors[GBuffer::Resources::Descriptors::ES_Albedo],
				DebugShadeParams::DisplayMark::RGB
			);
		}
		if (ImGui::Checkbox("NormalDepth Map", &mDebugDisplays[DebugDisplay::Layout::ENormalDepth])) {
			BuildDebugDescriptors(
				std::ref(mDebugDisplays[DebugDisplay::Layout::ENormalDepth]),
				gbufferResourcesGpuDescriptors[GBuffer::Resources::Descriptors::ES_NormalDepth],
				DebugShadeParams::DisplayMark::RGB
			);
		}
		if (ImGui::Checkbox("Depth Map", &mDebugDisplays[DebugDisplay::Layout::EDepth])) {
			BuildDebugDescriptors(
				std::ref(mDebugDisplays[DebugDisplay::Layout::EDepth]),
				gbufferResourcesGpuDescriptors[GBuffer::Resources::Descriptors::ES_Depth],
				DebugShadeParams::DisplayMark::RRR
			);
		}
		if (ImGui::Checkbox("Specular Map", &mDebugDisplays[DebugDisplay::Layout::ESpecular])) {
			BuildDebugDescriptors(
				std::ref(mDebugDisplays[DebugDisplay::Layout::ESpecular]),
				gbufferResourcesGpuDescriptors[GBuffer::Resources::Descriptors::ES_Specular],
				DebugShadeParams::DisplayMark::RGB
			);
		}
		if (ImGui::Checkbox("Velocity Map", &mDebugDisplays[DebugDisplay::Layout::EVelocity])) {
			BuildDebugDescriptors(
				std::ref(mDebugDisplays[DebugDisplay::Layout::EVelocity]),
				gbufferResourcesGpuDescriptors[GBuffer::Resources::Descriptors::ES_Velocity],
				DebugShadeParams::DisplayMark::RG
			);
		}
		if (ImGui::Checkbox("SSAO", &mDebugDisplays[DebugDisplay::Layout::EScreenAO])) {
			BuildDebugDescriptors(
				std::ref(mDebugDisplays[DebugDisplay::Layout::EScreenAO]),
				ssaoResourcesGpuDescriptors[Ssao::Resources::Descriptors::ES_AmbientCoefficient],
				DebugShadeParams::DisplayMark::RRR
			);
		}
		if (ImGui::Checkbox("Shadow Map", &mDebugDisplays[DebugDisplay::Layout::EShadow])) {
			BuildDebugDescriptors(
				std::ref(mDebugDisplays[DebugDisplay::Layout::EShadow]),
				mShadow->Srv(),
				DebugShadeParams::DisplayMark::RRR
			);
		}
		if (ImGui::Checkbox("DXR Shadow Map", &mDebugDisplays[DebugDisplay::Layout::EDxrShadow])) {
			BuildDebugDescriptors(
				std::ref(mDebugDisplays[DebugDisplay::Layout::EDxrShadow]),
				dxrShadowResourcesGpuDescriptors[DxrShadow::Resources::Descriptors::ES_Shadow],
				DebugShadeParams::DisplayMark::RRR
			);
		}
		if (ImGui::Checkbox("AO Coefficient", &mDebugDisplays[DebugDisplay::Layout::EAOCoefficient])) {
			BuildDebugDescriptors(
				std::ref(mDebugDisplays[DebugDisplay::Layout::EAOCoefficient]),
				rtaoResourcesGpuDescriptors[Rtao::AOResources::Descriptors::ES_AmbientCoefficient],
				DebugShadeParams::DisplayMark::RRR
			);
		}
		if (ImGui::Checkbox("Temporal AO Coefficient", &mDebugDisplays[DebugDisplay::Layout::ETemporalAOCoefficient])) {
			BuildDebugDescriptors(
				std::ref(mDebugDisplays[DebugDisplay::Layout::ETemporalAOCoefficient]),
				temporalAOCoefficientsGpuDescriptors[temporalCurrentFrameTemporalAOCoefficientResourceIndex][Rtao::TemporalAOCoefficients::Descriptors::Srv],
				DebugShadeParams::DisplayMark::RRR
			);
		}
		if (ImGui::Checkbox("Local Mean", &mDebugDisplays[DebugDisplay::Layout::ELocalMeanVariance_Mean])) {
			BuildDebugDescriptors(
				std::ref(mDebugDisplays[DebugDisplay::Layout::ELocalMeanVariance_Mean]),
				localMeanVarianceResourcesGpuDescriptors[Rtao::LocalMeanVarianceResources::Descriptors::ES_Raw],
				DebugShadeParams::DisplayMark::RRR
			);
		}
		if (ImGui::Checkbox("Local Variance", &mDebugDisplays[DebugDisplay::Layout::ELocalMeanVariance_Var])) {
			BuildDebugDescriptors(
				std::ref(mDebugDisplays[DebugDisplay::Layout::ELocalMeanVariance_Var]),
				localMeanVarianceResourcesGpuDescriptors[Rtao::LocalMeanVarianceResources::Descriptors::ES_Raw],
				DebugShadeParams::DisplayMark::GGG
			);
		}
		if (ImGui::Checkbox("AO Variance", &mDebugDisplays[DebugDisplay::Layout::EAOVariance])) {
			BuildDebugDescriptors(
				std::ref(mDebugDisplays[DebugDisplay::Layout::EAOVariance]),
				aoVarianceResourcesGpuDescriptors[ShaderArgs::Denoiser::UseSmoothingVariance ? 
					Rtao::AOVarianceResources::Descriptors::ES_Smoothed : Rtao::AOVarianceResources::Descriptors::ES_Raw],
				DebugShadeParams::DisplayMark::RRR
			);
		}
		if (ImGui::Checkbox("AO Ray Hit Distance", &mDebugDisplays[DebugDisplay::Layout::EAORayHitDistance])) {
			BuildDebugDescriptors(
				std::ref(mDebugDisplays[DebugDisplay::Layout::EAORayHitDistance]),
				rtaoResourcesGpuDescriptors[Rtao::AOResources::Descriptors::ES_RayHitDistance],
				DebugShadeParams::DisplayMark::RayHitDist
			);
		}
		if (ImGui::Checkbox("Temporal Ray Hit Distance", &mDebugDisplays[DebugDisplay::Layout::ETemporalRayHitDistance])) {
			BuildDebugDescriptors(
				std::ref(mDebugDisplays[DebugDisplay::Layout::ETemporalRayHitDistance]),
				temporalCachesGpuDescriptors[temporalCurrentFrameResourceIndex][Rtao::TemporalCaches::Descriptors::ES_RayHitDistance],
				DebugShadeParams::DisplayMark::RayHitDist
			);
		}
		if (ImGui::Checkbox("Partial Depth Derivatives", &mDebugDisplays[DebugDisplay::Layout::EPartialDepthDerivatives])) {
			BuildDebugDescriptors(
				std::ref(mDebugDisplays[DebugDisplay::Layout::EPartialDepthDerivatives]),
				mRtao->TsppCoefficientSquaredMeanRayHitDistanceSrv(),
				DebugShadeParams::DisplayMark::RG
			);
		}
		if (ImGui::Checkbox("Disocclusion Blur Strength", &mDebugDisplays[DebugDisplay::Layout::EDisocclusionBlurStrength])) {
			BuildDebugDescriptors(
				std::ref(mDebugDisplays[DebugDisplay::Layout::EDisocclusionBlurStrength]),
				mRtao->DisocclusionBlurStrengthSrv(),
				DebugShadeParams::DisplayMark::RRR
			);
		}

		ImGui::End();
	}
	{
		ImGui::Begin("Sub Panel");
		ImGui::NewLine();

		if (ImGui::CollapsingHeader("Raytracing")) {
			if (ImGui::TreeNode("Shadow")) {
				ImGui::SliderInt("Number of Blurs", &ShaderArgs::DxrShadow::BlurCount, 0, 8);

				ImGui::TreePop();
			}
			if (ImGui::TreeNode("RTAO")) {
				ImGui::SliderInt("Sample Count", reinterpret_cast<int*>(&ShaderArgs::RaytracedAO::SampleCount), 1, 4);
				ImGui::SliderFloat("Occlusion Radius", &ShaderArgs::RaytracedAO::OcclusionRadius, 0.01f, 100.0f);
				ImGui::SliderFloat("Occlusion Fade Start", &ShaderArgs::RaytracedAO::OcclusionFadeStart, 0.0f, 10.0f);
				ImGui::SliderFloat("Occlusion Fade End", &ShaderArgs::RaytracedAO::OcclusionFadeEnd, 0.0f, 100.0f);
				ImGui::SliderFloat("Surface Epsilon", &ShaderArgs::RaytracedAO::OcclusionEpsilon, 0.01f, 1.0f);
				ImGui::Checkbox("Checkerboard Sampling", &bCheckerboardSamplingEnabled);
				ImGui::Checkbox("Smoothing Variance", &ShaderArgs::Denoiser::UseSmoothingVariance);
				ImGui::Checkbox("Blur Low Tspp", &ShaderArgs::Denoiser::LowTspp);

				ImGui::TreePop();
			}
		}
		if (ImGui::CollapsingHeader("Rasterization")) {
			if (ImGui::TreeNode("SSAO")) {
				ImGui::SliderFloat("Occlusion Radius", &ShaderArgs::ScreenSpaceAO::OcclusionRadius, 0.01f, 1.0f);
				ImGui::SliderFloat("Occlusion Fade Start", &ShaderArgs::ScreenSpaceAO::OcclusionFadeStart, 0.0f, 10.0f);
				ImGui::SliderFloat("Occlusion Fade End", &ShaderArgs::ScreenSpaceAO::OcclusionFadeEnd, 0.0f, 10.0f);
				ImGui::SliderFloat("Surface Epsilon", &ShaderArgs::ScreenSpaceAO::OcclusionEpsilon, 0.01f, 1.0f);
				ImGui::SliderFloat("Blur Dot Threshold", &ShaderArgs::ScreenSpaceAO::DotThreshold, -1.0f, 1.0f);
				ImGui::SliderFloat("Blur Depth Threshold", &ShaderArgs::ScreenSpaceAO::DepthThreshold, 0.0f, 10.0f);
				ImGui::SliderInt("Number of Blurs", &ShaderArgs::ScreenSpaceAO::BlurCount, 0, 8);

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

	const auto pDescHeap = mCbvSrvUavHeap.Get();
	UINT descSize = GetCbvSrvUavDescriptorSize();

	ID3D12DescriptorHeap* descriptorHeaps[] = { pDescHeap };
	cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	const auto& gbufferResources = mGBuffer->Resources();
	const auto& gbufferResourcesGpuDescriptors = mGBuffer->ResourcesGpuDescriptors();
	const auto& dxrShadowResources = mDxrShadow->Resources();
	const auto& dxrShadowGpuDescriptors = mDxrShadow->ResourcesGpuDescriptors();

	const auto shadow = dxrShadowResources[DxrShadow::Resources::EShadow].Get();
	const auto temporary = dxrShadowResources[DxrShadow::Resources::ETemporary].Get();

	ID3D12Resource* resources[DxrShadow::Resources::Count] = { shadow, temporary };
	{
		D3D12_RESOURCE_BARRIER barriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(
				shadow,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS
			),
			CD3DX12_RESOURCE_BARRIER::Transition(
				temporary,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS
			)
		};
		cmdList->ResourceBarrier(_countof(barriers), barriers);
		D3D12Util::UavBarriers(cmdList, resources, _countof(resources));
	}
	
	mDxrShadow->Run(
		cmdList,
		mTLAS->Result->GetGPUVirtualAddress(),
		mCurrFrameResource->PassCB.Resource()->GetGPUVirtualAddress(),
		mCurrFrameResource->ObjectSB.Resource()->GetGPUVirtualAddress(),
		mCurrFrameResource->MaterialSB.Resource()->GetGPUVirtualAddress(),
		D3D12Util::GetGpuHandle(pDescHeap, EDescriptors::ES_Vertices, descSize),
		D3D12Util::GetGpuHandle(pDescHeap, EDescriptors::ES_Indices, descSize),
		gbufferResourcesGpuDescriptors[GBuffer::Resources::Descriptors::ES_Depth],
		dxrShadowGpuDescriptors[DxrShadow::Resources::Descriptors::EU_Shadow],
		GetClientWidth(), GetClientHeight()
	);
	{
		D3D12_RESOURCE_BARRIER barriers[] = {
			CD3DX12_RESOURCE_BARRIER::Transition(
				shadow,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
			),
			CD3DX12_RESOURCE_BARRIER::Transition(
				temporary,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
			)
		};
		cmdList->ResourceBarrier(_countof(barriers), barriers);
		D3D12Util::UavBarriers(cmdList, resources, _countof(resources));
	}
		
	mGaussianFilterCS->Run(
		cmdList,
		mCurrFrameResource->BlurCB.Resource()->GetGPUVirtualAddress(),
		gbufferResourcesGpuDescriptors[GBuffer::Resources::Descriptors::ES_NormalDepth],
		shadow,
		temporary,
		dxrShadowGpuDescriptors[DxrShadow::Resources::Descriptors::ES_Shadow],
		dxrShadowGpuDescriptors[DxrShadow::Resources::Descriptors::EU_Shadow],
		dxrShadowGpuDescriptors[DxrShadow::Resources::Descriptors::ES_Temporary],
		dxrShadowGpuDescriptors[DxrShadow::Resources::Descriptors::EU_Temporary],
		GaussianFilterCS::Filter::Type::R16,
		mDxrShadow->Width(), mDxrShadow->Height(),
		ShaderArgs::DxrShadow::BlurCount
	);
	
	CheckHResult(cmdList->Close());
	ID3D12CommandList* cmdsLists[] = { cmdList };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	return true;
}

bool Renderer::dxrDrawRtao() {
	const auto cmdList = mCommandList.Get();
	CheckHResult(cmdList->Reset(mCurrFrameResource->CmdListAlloc.Get(), nullptr));

	const auto pDescHeap = mCbvSrvUavHeap.Get();
	UINT descSize = GetCbvSrvUavDescriptorSize();

	ID3D12DescriptorHeap* descriptorHeaps[] = { pDescHeap };
	cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	const auto& gbufferResources = mGBuffer->Resources();
	const auto& gbufferResourcesGpuDescriptors = mGBuffer->ResourcesGpuDescriptors();
	const auto& aoResources = mRtao->AOResources();
	const auto& aoResourcesGpuDescriptors = mRtao->AOResourcesGpuDescriptors();
	const auto& temporalCaches = mRtao->TemporalCaches();
	const auto& temporalCachesGpuDescriptors = mRtao->TemporalCachesGpuDescriptors();
	const auto& temporalAOCoefficients = mRtao->TemporalAOCoefficients();
	const auto& temporalAOCoefficientsGpuDescriptors = mRtao->TemporalAOCoefficientsGpuDescriptors();
	const auto& localMeanVarianceResources = mRtao->LocalMeanVarianceResources();
	const auto& localMeanVarianceResourcesGpuDescriptors = mRtao->LocalMeanVarianceResourcesGpuDescriptors();
	const auto& varianceResources = mRtao->AOVarianceResources();
	const auto& varianceResourcesGpuDescriptors = mRtao->AOVarianceResourcesGpuDescriptors();

	const auto depthPartialDerivative = mRtao->DepthPartialDerivativeMapResource();

	// Calculate ambient occlusion.
	{
		const auto ambientCoefficient = aoResources[Rtao::AOResources::EAmbientCoefficient].Get();
		{
			D3D12_RESOURCE_BARRIER barriers[] = {
				CD3DX12_RESOURCE_BARRIER::Transition(
					ambientCoefficient,
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
					D3D12_RESOURCE_STATE_UNORDERED_ACCESS
				)
			};
			cmdList->ResourceBarrier(
				_countof(barriers),
				barriers
			);
			D3D12Util::UavBarrier(cmdList, ambientCoefficient);
		}

		mRtao->RunCalculatingAmbientOcclusion(
			cmdList,
			mTLAS->Result->GetGPUVirtualAddress(),
			mCurrFrameResource->RtaoCB.Resource()->GetGPUVirtualAddress(),
			gbufferResourcesGpuDescriptors[GBuffer::Resources::Descriptors::ES_NormalDepth],
			gbufferResourcesGpuDescriptors[GBuffer::Resources::Descriptors::ES_Depth],
			aoResourcesGpuDescriptors[Rtao::AOResources::Descriptors::EU_AmbientCoefficient],
			aoResourcesGpuDescriptors[Rtao::AOResources::Descriptors::EU_RayHitDistance]
		);

		{
			D3D12_RESOURCE_BARRIER barriers[] = {
				CD3DX12_RESOURCE_BARRIER::Transition(
					ambientCoefficient,
					D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
				)
			};
			cmdList->ResourceBarrier(
				_countof(barriers),
				barriers
			);
			D3D12Util::UavBarrier(cmdList, ambientCoefficient);
		}
	}
	// Calculate partial-derivatives.
	{
		cmdList->ResourceBarrier(
			1,
			&CD3DX12_RESOURCE_BARRIER::Transition(
				depthPartialDerivative,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS
			)
		);
		D3D12Util::UavBarrier(cmdList, depthPartialDerivative);

		mRtao->RunCalculatingDepthPartialDerivative(
			cmdList,
			gbufferResourcesGpuDescriptors[GBuffer::Resources::Descriptors::ES_Depth],
			mRtao->DepthPartialDerivativeUav(),
			GetClientWidth(), GetClientHeight()
		);

		cmdList->ResourceBarrier(
			1,
			&CD3DX12_RESOURCE_BARRIER::Transition(
				depthPartialDerivative,
				D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
				D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
			)
		);
		D3D12Util::UavBarrier(cmdList, depthPartialDerivative);
	}
	// Denosing(Spatio-Temporal Variance Guided Filtering)
	{
		// Stage 1: Reverse reprojection
		{
			UINT temporalPreviousFrameResourceIndex = mRtao->TemporalCurrentFrameResourceIndex();
			UINT temporalCurrentFrameResourcIndex = mRtao->MoveToNextFrame();
	
			UINT temporalPreviousFrameTemporalAOCoefficientResourceIndex = mRtao->TemporalCurrentFrameTemporalAOCoefficientResourceIndex();
			UINT temporalCurrentFrameTemporalAOCoefficientResourceIndex = mRtao->MoveToNextFrameTemporalAOCoefficient();
		
			const auto currTsppMap = temporalCaches[temporalCurrentFrameResourcIndex][Rtao::TemporalCaches::ETspp].Get();
			const auto tsppCoefficientSquaredMeanRayHitDistance = mRtao->TsppCoefficientSquaredMeanRayHitDistance();
			std::vector<ID3D12Resource*> resources = { currTsppMap, tsppCoefficientSquaredMeanRayHitDistance };
			{
				D3D12_RESOURCE_BARRIER barriers[] = {
					CD3DX12_RESOURCE_BARRIER::Transition(
						currTsppMap,
						D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
						D3D12_RESOURCE_STATE_UNORDERED_ACCESS
					),
					CD3DX12_RESOURCE_BARRIER::Transition(
						tsppCoefficientSquaredMeanRayHitDistance,
						D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
						D3D12_RESOURCE_STATE_UNORDERED_ACCESS
					),
				};
				cmdList->ResourceBarrier(
					_countof(barriers),
					barriers
				);
				D3D12Util::UavBarriers(cmdList, resources.data(), resources.size());
			}

			// Retrieves values from previous frame via reverse reprojection.				
			mRtao->ReverseReprojectPreviousFrame(
				cmdList,
				mCurrFrameResource->CrossBilateralFilterCB.Resource()->GetGPUVirtualAddress(),
				gbufferResourcesGpuDescriptors[GBuffer::Resources::Descriptors::ES_NormalDepth],
				mRtao->DepthPartialDerivativeSrv(),
				gbufferResourcesGpuDescriptors[GBuffer::Resources::Descriptors::ES_ReprojectedNormalDepth],
				mRtao->PrevFrameNormalDepthSrv(),
				gbufferResourcesGpuDescriptors[GBuffer::Resources::Descriptors::ES_Velocity],
				temporalAOCoefficientsGpuDescriptors[temporalPreviousFrameTemporalAOCoefficientResourceIndex][Rtao::TemporalAOCoefficients::Descriptors::Srv],
				temporalCachesGpuDescriptors[temporalPreviousFrameResourceIndex][Rtao::TemporalCaches::Descriptors::ES_Tspp],
				temporalCachesGpuDescriptors[temporalPreviousFrameResourceIndex][Rtao::TemporalCaches::Descriptors::ES_CoefficientSquaredMean],
				temporalCachesGpuDescriptors[temporalPreviousFrameResourceIndex][Rtao::TemporalCaches::Descriptors::ES_RayHitDistance],
				temporalCachesGpuDescriptors[temporalCurrentFrameResourcIndex][Rtao::TemporalCaches::Descriptors::EU_Tspp],
				mRtao->TsppCoefficientSquaredMeanRayHitDistanceUav()
			);
	
			{
				D3D12_RESOURCE_BARRIER barriers[] = {
					CD3DX12_RESOURCE_BARRIER::Transition(
						currTsppMap,
						D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
						D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
					),
					CD3DX12_RESOURCE_BARRIER::Transition(
						tsppCoefficientSquaredMeanRayHitDistance,
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
		
			// Copy the current normal and depth values to the cached map.
			{
				const auto pNormalDepth = gbufferResources[GBuffer::Resources::ENormalDepth].Get();
				const auto pPrevFrameNormalDepth = mRtao->PrevFrameNormalDepth();
				{
					D3D12_RESOURCE_BARRIER barriers[] = {
						CD3DX12_RESOURCE_BARRIER::Transition(
							pNormalDepth,
							D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
							D3D12_RESOURCE_STATE_COPY_SOURCE
						),
						CD3DX12_RESOURCE_BARRIER::Transition(
							pPrevFrameNormalDepth ,
							D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
							D3D12_RESOURCE_STATE_COPY_DEST
						)
					};
					cmdList->ResourceBarrier(
						_countof(barriers),
						barriers
					);
				}
				cmdList->CopyResource(pPrevFrameNormalDepth, pNormalDepth);
				{
					D3D12_RESOURCE_BARRIER barriers[] = {
						CD3DX12_RESOURCE_BARRIER::Transition(
							pNormalDepth,
							D3D12_RESOURCE_STATE_COPY_SOURCE,
							D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
						),
						CD3DX12_RESOURCE_BARRIER::Transition(
							pPrevFrameNormalDepth,
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
		{
			// Calculate local mean and variance for clamping during the blending operation.
			{		
				const auto rawLocalMeanVariance = localMeanVarianceResources[Rtao::LocalMeanVarianceResources::ERaw].Get();
				cmdList->ResourceBarrier(
					1,
					&CD3DX12_RESOURCE_BARRIER::Transition(
						rawLocalMeanVariance,
						D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
						D3D12_RESOURCE_STATE_UNORDERED_ACCESS
					)
				);
				D3D12Util::UavBarrier(cmdList, rawLocalMeanVariance);					
	
				mRtao->RunCalculatingLocalMeanVariance(
					cmdList,
					mCurrFrameResource->CalcLocalMeanVarCB.Resource()->GetGPUVirtualAddress(),
					aoResourcesGpuDescriptors[Rtao::AOResources::Descriptors::ES_AmbientCoefficient],
					localMeanVarianceResourcesGpuDescriptors[Rtao::LocalMeanVarianceResources::Descriptors::EU_Raw],
					mRtao->Width(), mRtao->Height(),
					bCheckerboardSamplingEnabled
				);
	
				cmdList->ResourceBarrier(
					1,
					&CD3DX12_RESOURCE_BARRIER::Transition(
						rawLocalMeanVariance,
						D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
						D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
					)
				);
				D3D12Util::UavBarrier(cmdList, rawLocalMeanVariance);
		
				// Interpolate the variance for the inactive cells from the valid checkerboard cells.
				if (bCheckerboardSamplingEnabled) {				
					cmdList->ResourceBarrier(
						1,
						&CD3DX12_RESOURCE_BARRIER::Transition(
							rawLocalMeanVariance,
							D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
							D3D12_RESOURCE_STATE_UNORDERED_ACCESS
						)
					);
					D3D12Util::UavBarrier(cmdList, rawLocalMeanVariance);
				
					mRtao->FillInCheckerboard(
						cmdList,
						mCurrFrameResource->CalcLocalMeanVarCB.Resource()->GetGPUVirtualAddress(),
						localMeanVarianceResourcesGpuDescriptors[Rtao::LocalMeanVarianceResources::Descriptors::EU_Raw]
					);

					cmdList->ResourceBarrier(
						1,
						&CD3DX12_RESOURCE_BARRIER::Transition(
							rawLocalMeanVariance,
							D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
							D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
						)
					);
					D3D12Util::UavBarrier(cmdList, rawLocalMeanVariance);
				}
			}
	
			// Blends reprojected values with current frame values.
			// Inactive pixels are filtered from active neighbors on checkerboard sampling before the blending operation.
			{
				UINT temporalCurrentFrameResourceIndex = mRtao->TemporalCurrentFrameResourceIndex();
				UINT temporalCurrentFrameTemporalAOCoefficientResourceIndex = mRtao->TemporalCurrentFrameTemporalAOCoefficientResourceIndex();

				const auto currTemporalAOCoefficient = temporalAOCoefficients[temporalCurrentFrameTemporalAOCoefficientResourceIndex].Get();
				const auto currTemporalSupersampling = temporalCaches[temporalCurrentFrameResourceIndex][Rtao::TemporalCaches::ETspp].Get();
				const auto currCoefficientSquaredMean = temporalCaches[temporalCurrentFrameResourceIndex][Rtao::TemporalCaches::ECoefficientSquaredMean].Get();
				const auto currRayHitDistance = temporalCaches[temporalCurrentFrameResourceIndex][Rtao::TemporalCaches::ERayHitDistance].Get();
				const auto rawVariance = varianceResources[Rtao::AOVarianceResources::ERaw].Get();
				const auto diocclusionBlurStrength = mRtao->DisocclusionBlurStrengthResource();

				std::vector<ID3D12Resource*> resources = {
					currTemporalAOCoefficient, currTemporalSupersampling, currCoefficientSquaredMean, currRayHitDistance, rawVariance, diocclusionBlurStrength
				};
				{
					D3D12_RESOURCE_BARRIER barriers[] = {
						CD3DX12_RESOURCE_BARRIER::Transition(
							currTemporalAOCoefficient,
							D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
							D3D12_RESOURCE_STATE_UNORDERED_ACCESS
							),
							CD3DX12_RESOURCE_BARRIER::Transition(
								currTemporalSupersampling,
								D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
								D3D12_RESOURCE_STATE_UNORDERED_ACCESS
							),
							CD3DX12_RESOURCE_BARRIER::Transition(
								currCoefficientSquaredMean,
								D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
								D3D12_RESOURCE_STATE_UNORDERED_ACCESS
							),
							CD3DX12_RESOURCE_BARRIER::Transition(
								currRayHitDistance,
								D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
								D3D12_RESOURCE_STATE_UNORDERED_ACCESS
							),
							CD3DX12_RESOURCE_BARRIER::Transition(
								rawVariance,
								D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
								D3D12_RESOURCE_STATE_UNORDERED_ACCESS
							),
							CD3DX12_RESOURCE_BARRIER::Transition(
								diocclusionBlurStrength,
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

				mRtao->BlendWithCurrentFrame(
					cmdList,
					mCurrFrameResource->TsppBlendCB.Resource()->GetGPUVirtualAddress(),
					aoResourcesGpuDescriptors[Rtao::AOResources::Descriptors::ES_AmbientCoefficient],
					localMeanVarianceResourcesGpuDescriptors[Rtao::LocalMeanVarianceResources::Descriptors::ES_Raw],
					aoResourcesGpuDescriptors[Rtao::AOResources::Descriptors::ES_RayHitDistance],
					mRtao->TsppCoefficientSquaredMeanRayHitDistanceSrv(),
					temporalAOCoefficientsGpuDescriptors[temporalCurrentFrameTemporalAOCoefficientResourceIndex][Rtao::TemporalAOCoefficients::Descriptors::Uav],
					temporalCachesGpuDescriptors[temporalCurrentFrameResourceIndex][Rtao::TemporalCaches::Descriptors::EU_Tspp],
					temporalCachesGpuDescriptors[temporalCurrentFrameResourceIndex][Rtao::TemporalCaches::Descriptors::EU_CoefficientSquaredMean],
					temporalCachesGpuDescriptors[temporalCurrentFrameResourceIndex][Rtao::TemporalCaches::Descriptors::EU_RayHitDistance],
					varianceResourcesGpuDescriptors[Rtao::AOVarianceResources::Descriptors::EU_Raw],
					mRtao->DisocclusionBlurStrengthUav()
				);

				{
					D3D12_RESOURCE_BARRIER barriers[] = {
						CD3DX12_RESOURCE_BARRIER::Transition(
							currTemporalAOCoefficient,
							D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
							D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
						),
						CD3DX12_RESOURCE_BARRIER::Transition(
							currTemporalSupersampling,
							D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
							D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
						),
						CD3DX12_RESOURCE_BARRIER::Transition(
							currCoefficientSquaredMean,
							D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
							D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
						),
						CD3DX12_RESOURCE_BARRIER::Transition(
							currRayHitDistance,
							D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
							D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
						),
						CD3DX12_RESOURCE_BARRIER::Transition(
							rawVariance,
							D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
							D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
						),
						CD3DX12_RESOURCE_BARRIER::Transition(
							diocclusionBlurStrength,
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
			}
	
			if (ShaderArgs::Denoiser::UseSmoothingVariance) {
				const auto smoothedVariance = varianceResources[Rtao::AOVarianceResources::ESmoothed].Get();

				cmdList->ResourceBarrier(
					1,
					&CD3DX12_RESOURCE_BARRIER::Transition(
						smoothedVariance,
						D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
						D3D12_RESOURCE_STATE_UNORDERED_ACCESS
					)
				);
				D3D12Util::UavBarrier(cmdList, smoothedVariance);

				mGaussianFilter3x3CS->Run(
					cmdList,
					varianceResourcesGpuDescriptors[Rtao::AOVarianceResources::Descriptors::ES_Raw],
					varianceResourcesGpuDescriptors[Rtao::AOVarianceResources::Descriptors::EU_Smoothed],
					GaussianFilter3x3CS::Filter3x3,
					mRtao->Width(), mRtao->Height()
				);

				cmdList->ResourceBarrier(
					1,
					&CD3DX12_RESOURCE_BARRIER::Transition(
						smoothedVariance,
						D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
						D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
					)
				);
				D3D12Util::UavBarrier(cmdList, smoothedVariance);
			}
		}
		// Applies a single pass of a Atrous wavelet transform filter.
		{
			UINT temporalCurrentFrameResourceIndex = mRtao->TemporalCurrentFrameResourceIndex();
			UINT inputAOCoefficientIndex = mRtao->TemporalCurrentFrameTemporalAOCoefficientResourceIndex();
			UINT outputAOCoefficientIndex = mRtao->MoveToNextFrameTemporalAOCoefficient();

			const auto outputAOCoefficient = temporalAOCoefficients[outputAOCoefficientIndex].Get();

			cmdList->ResourceBarrier(
				1,
				&CD3DX12_RESOURCE_BARRIER::Transition(
					outputAOCoefficient,
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
					D3D12_RESOURCE_STATE_UNORDERED_ACCESS
				)
			);
			D3D12Util::UavBarrier(cmdList, outputAOCoefficient);

			mRtao->ApplyAtrousWaveletTransformFilter(
				cmdList,
				mCurrFrameResource->AtrousFilterCB.Resource()->GetGPUVirtualAddress(),
				temporalAOCoefficientsGpuDescriptors[inputAOCoefficientIndex][Rtao::TemporalAOCoefficients::Descriptors::Srv],
				gbufferResourcesGpuDescriptors[GBuffer::Resources::Descriptors::ES_NormalDepth],
				varianceResourcesGpuDescriptors[ShaderArgs::Denoiser::UseSmoothingVariance ? Rtao::AOVarianceResources::Descriptors::ES_Smoothed : Rtao::AOVarianceResources::Descriptors::ES_Raw],
				temporalCachesGpuDescriptors[temporalCurrentFrameResourceIndex][Rtao::TemporalCaches::Descriptors::ES_RayHitDistance],
				mRtao->DepthPartialDerivativeSrv(),
				temporalCachesGpuDescriptors[temporalCurrentFrameResourceIndex][Rtao::TemporalCaches::Descriptors::ES_Tspp],
				temporalAOCoefficientsGpuDescriptors[outputAOCoefficientIndex][Rtao::TemporalAOCoefficients::Descriptors::Uav]
			);

			cmdList->ResourceBarrier(
				1,
				&CD3DX12_RESOURCE_BARRIER::Transition(
					outputAOCoefficient,
					D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
				)
			);
			D3D12Util::UavBarrier(cmdList, outputAOCoefficient);
		}
		if (ShaderArgs::Denoiser::LowTspp) {
			UINT temporalCurrentFrameTemporalAOCoefficientResourceIndex = mRtao->TemporalCurrentFrameTemporalAOCoefficientResourceIndex();

			const auto aoCoefficient = temporalAOCoefficients[temporalCurrentFrameTemporalAOCoefficientResourceIndex].Get();

			cmdList->ResourceBarrier(
				1,
				&CD3DX12_RESOURCE_BARRIER::Transition(
					aoCoefficient,
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,
					D3D12_RESOURCE_STATE_UNORDERED_ACCESS
				)
			);

			mRtao->BlurDisocclusion(
				cmdList,
				aoCoefficient,
				gbufferResourcesGpuDescriptors[GBuffer::Resources::Descriptors::ES_Depth],
				mRtao->DisocclusionBlurStrengthSrv(),
				temporalAOCoefficientsGpuDescriptors[temporalCurrentFrameTemporalAOCoefficientResourceIndex][Rtao::TemporalAOCoefficients::Descriptors::Uav],
				mRtao->Width(), mRtao->Height(),
				ShaderArgs::Denoiser::LowTsppBlurPasses
			);

			cmdList->ResourceBarrier(
				1,
				&CD3DX12_RESOURCE_BARRIER::Transition(
					aoCoefficient,
					D3D12_RESOURCE_STATE_UNORDERED_ACCESS,
					D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
				)
			);
		}
	}

	CheckHResult(cmdList->Close());
	ID3D12CommandList* cmdsLists[] = { cmdList };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	return true;
}

bool Renderer::dxrDrawBackBuffer() {
	const auto cmdList = mCommandList.Get();

	CheckHResult(cmdList->Reset(mCurrFrameResource->CmdListAlloc.Get(), mPSOs["dxrBackBuffer"].Get()));
	cmdList->SetGraphicsRootSignature(mRootSignatures["dxrBackBuffer"].Get());

	const auto pDescHeap = mCbvSrvUavHeap.Get();
	ID3D12DescriptorHeap* descriptorHeaps[] = { pDescHeap };
	cmdList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	cmdList->RSSetViewports(1, &mScreenViewport);
	cmdList->RSSetScissorRects(1, &mScissorRect);

	const auto& gbufferResourcesGpuDescriptors = mGBuffer->ResourcesGpuDescriptors();
	const auto& dxrShadowResourcesGpuDescriptors = mDxrShadow->ResourcesGpuDescriptors();
	const auto& aoResources = mRtao->AOResources();
	const auto& aoResourcesGpuDescriptors = mRtao->AOResourcesGpuDescriptors();
	const auto& temporalAOCoefficientsGpuDescriptors = mRtao->TemporalAOCoefficientsGpuDescriptors();

	auto temporalCurrentFrameTemporalAOCoefficientResourceIndex = mRtao->TemporalCurrentFrameTemporalAOCoefficientResourceIndex();

	const auto pCurrBackBuffer = CurrentBackBuffer();
	cmdList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			pCurrBackBuffer,
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET
		)
	);

	auto pCurrBackBufferView = CurrentBackBufferView();
	cmdList->OMSetRenderTargets(1, &pCurrBackBufferView, true, nullptr);
	
	auto passCBAddress = mCurrFrameResource->PassCB.Resource()->GetGPUVirtualAddress();
	mCommandList->SetGraphicsRootConstantBufferView(DxrBackBuffer::RootSignatureLayout::ECB_Pass, passCBAddress);

	mCommandList->SetGraphicsRootDescriptorTable(
		DxrBackBuffer::RootSignatureLayout::ESI_Color,
		gbufferResourcesGpuDescriptors[GBuffer::Resources::Descriptors::ES_Color]
	);
	mCommandList->SetGraphicsRootDescriptorTable(
		DxrBackBuffer::RootSignatureLayout::ESI_Albedo,
		gbufferResourcesGpuDescriptors[GBuffer::Resources::Descriptors::ES_Albedo]
	);
	mCommandList->SetGraphicsRootDescriptorTable(
		DxrBackBuffer::RootSignatureLayout::ESI_Normal,
		gbufferResourcesGpuDescriptors[GBuffer::Resources::Descriptors::ES_NormalDepth]
	);
	mCommandList->SetGraphicsRootDescriptorTable(
		DxrBackBuffer::RootSignatureLayout::ESI_Depth,
		gbufferResourcesGpuDescriptors[GBuffer::Resources::Descriptors::ES_Depth]
	);
	mCommandList->SetGraphicsRootDescriptorTable(
		DxrBackBuffer::RootSignatureLayout::ESI_Specular,
		gbufferResourcesGpuDescriptors[GBuffer::Resources::Descriptors::ES_Specular]
	);
	mCommandList->SetGraphicsRootDescriptorTable(
		DxrBackBuffer::RootSignatureLayout::ESI_Shadow,
		dxrShadowResourcesGpuDescriptors[DxrShadow::Resources::Descriptors::ES_Shadow]
	);
	mCommandList->SetGraphicsRootDescriptorTable(
		DxrBackBuffer::RootSignatureLayout::ESI_AmbientCoefficient,
		temporalAOCoefficientsGpuDescriptors[temporalCurrentFrameTemporalAOCoefficientResourceIndex][Rtao::TemporalAOCoefficients::Descriptors::Srv]
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
