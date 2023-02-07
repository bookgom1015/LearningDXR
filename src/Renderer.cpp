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
#include "ShadowMap.h"
#include "GBuffer.h"
#include "DxrShadowMap.h"

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
			D3D12_FILTER_MIN_MAG_MIP_LINEAR,	// filter
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

	mShaderManager = std::make_unique<ShaderManager>();
	mMainPassCB = std::make_unique<PassConstants>();
	mShadowPassCB = std::make_unique<PassConstants>();
	mTLAS = std::make_unique<AccelerationStructureBuffer>();

	mDXROutputs.resize(gNumFrameResources);

	mShadowMap = std::make_unique<ShadowMap>();
	mGBuffer = std::make_unique<GBuffer>();
	mDxrShadowMap = std::make_unique<DxrShadowMap>();
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

	const auto pDevice = md3dDevice.Get();
	CheckIsValid(mShadowMap->Initialize(pDevice, 2048, 2048));
	CheckIsValid(mGBuffer->Initialize(pDevice, width, height, BackBufferFormat, NormalMapFormat, DXGI_FORMAT_R24_UNORM_X8_TYPELESS, SpecularMapFormat));
	CheckIsValid(mDxrShadowMap->Initialize(pDevice, width, height));

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

	CheckHResult(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
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

	return true;
}

bool Renderer::Draw() {
	CheckHResult(mCurrFrameResource->CmdListAlloc->Reset());

	if (bRaytracing) { CheckIsValid(Raytrace()); }
	else { CheckIsValid(Rasterize()); }

	CheckIsValid(DrawDebugLayer());

	if (bDisplayImgGui) CheckIsValid(DrawImGui());;

	CheckHResult(mSwapChain->Present(0, 0));
	NextBackBuffer();

	mCurrFrameResource->Fence = static_cast<UINT>(IncCurrentFence());
	mCommandQueue->Signal(mFence.Get(), GetCurrentFence());

	return true;
}

bool Renderer::OnResize(UINT width, UINT height) {
	CheckIsValid(LowRenderer::OnResize(width, height));

	BuildDebugViewport();

	CheckIsValid(mGBuffer->OnResize(width, height, mDepthStencilBuffer.Get()));
	CheckIsValid(mDxrShadowMap->OnResize(width, height));

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
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount + GBuffer::NumRenderTargets;
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
	// Rasterization
	//
	{
		const auto filePath = ShaderFilePathW + L"Gizmo.hlsl";
		CheckIsValid(mShaderManager->CompileShader(filePath, nullptr, "VS", "vs_5_1", "gizmoVS"));
		CheckIsValid(mShaderManager->CompileShader(filePath, nullptr, "PS", "ps_5_1", "gizmoPS"));
	}
	{
		const auto filePath = ShaderFilePathW + L"Shadow.hlsl";
		CheckIsValid(mShaderManager->CompileShader(filePath, nullptr, "VS", "vs_5_1", "shadowVS"));
		CheckIsValid(mShaderManager->CompileShader(filePath, nullptr, "PS", "ps_5_1", "shadowPS"));
	}
	{
		const auto filePath = ShaderFilePathW + L"GBuffer.hlsl";
		CheckIsValid(mShaderManager->CompileShader(filePath, nullptr, "VS", "vs_5_1", "gbufferVS"));
		CheckIsValid(mShaderManager->CompileShader(filePath, nullptr, "PS", "ps_5_1", "gbufferPS"));
	}
	{
		const auto filePath = ShaderFilePathW + L"Debug.hlsl";
		CheckIsValid(mShaderManager->CompileShader(filePath, nullptr, "VS", "vs_5_1", "debugVS"));
		CheckIsValid(mShaderManager->CompileShader(filePath, nullptr, "PS", "ps_5_1", "debugPS"));
	}
	{
		const auto filePath = ShaderFilePathW + L"BackBuffer.hlsl";
		CheckIsValid(mShaderManager->CompileShader(filePath, nullptr, "VS", "vs_5_1", "backBufferVS"));
		CheckIsValid(mShaderManager->CompileShader(filePath, nullptr, "PS", "ps_5_1", "backBufferPS"));
	}
	{
		const auto filePath = ShaderFilePathW + L"DxrBackBuffer.hlsl";
		CheckIsValid(mShaderManager->CompileShader(filePath, nullptr, "VS", "vs_5_1", "dxrBackBufferVS"));
		CheckIsValid(mShaderManager->CompileShader(filePath, nullptr, "PS", "ps_5_1", "dxrBackBufferPS"));
	}
	//
	// Raytracing
	//
	{
		const auto filePath = ShaderFilePathW + L"RayGen.hlsl";
		auto rayGenInfo = D3D12ShaderInfo(filePath.c_str(), L"", L"lib_6_3");
		CheckIsValid(mShaderManager->CompileShader(rayGenInfo, "rayGen"));
	}
	{
		const auto filePath = ShaderFilePathW + L"Miss.hlsl";
		auto rayGenInfo = D3D12ShaderInfo(filePath.c_str(), L"", L"lib_6_3");
		CheckIsValid(mShaderManager->CompileShader(rayGenInfo, "miss"));
	}
	{
		const auto filePath = ShaderFilePathW + L"ClosestHit.hlsl";
		auto rayGenInfo = D3D12ShaderInfo(filePath.c_str(), L"", L"lib_6_3");
		CheckIsValid(mShaderManager->CompileShader(rayGenInfo, "closestHit"));
	}
	{
		const auto filePath = ShaderFilePathW + L"ShadowRay.hlsl";
		auto rayGenInfo = D3D12ShaderInfo(filePath.c_str(), L"", L"lib_6_3");
		CheckIsValid(mShaderManager->CompileShader(rayGenInfo, "shadowRay"));
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
	// For default
	{
		CD3DX12_ROOT_PARAMETER slotRootParameter[static_cast<int>(ERasterRootSignatureLayout::Count)];

		CD3DX12_DESCRIPTOR_RANGE texTables[1];
		texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, (UINT)EDescriptors::Srv_End - (UINT)EDescriptors::Srv_Start + 1, 0, 0);

		slotRootParameter[static_cast<int>(ERasterRootSignatureLayout::EPassCB)].InitAsConstantBufferView(0);
		slotRootParameter[static_cast<int>(ERasterRootSignatureLayout::EConsts)].InitAsConstants(static_cast<UINT>(ERasterRootConstantsLayout::Count), 1);
		slotRootParameter[static_cast<int>(ERasterRootSignatureLayout::EObjSB)].InitAsShaderResourceView(0, 1);
		slotRootParameter[static_cast<int>(ERasterRootSignatureLayout::EMatSB)].InitAsShaderResourceView(0, 2);
		slotRootParameter[static_cast<int>(ERasterRootSignatureLayout::ESrvMaps)].InitAsDescriptorTable(1, &texTables[0]);

		auto samplers = GetStaticSamplers();

		CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
			_countof(slotRootParameter), slotRootParameter,
			static_cast<UINT>(samplers.size()), samplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT
		);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice.Get(), rootSigDesc, mRootSignatures["raster"].GetAddressOf()));
	}
	//
	// Raytracing
	//
	// Global root signature
	{
		CD3DX12_DESCRIPTOR_RANGE texTables[5];
		texTables[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);		
		texTables[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, gNumGeometryBuffers, 0, 1);
		texTables[2].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, gNumGeometryBuffers, 0, 2);
		texTables[3].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, (UINT)EDescriptors::ES_Specular - (UINT)EDescriptors::Srv_Start + 1, 0, 3);
		texTables[4].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, (UINT)EDescriptors::Uav_End - (UINT)EDescriptors::Uav_Start + 1, 0, 2);

		CD3DX12_ROOT_PARAMETER slotRootParameter[static_cast<int>(EGlobalRootSignatureLayout::Count)];
		slotRootParameter[static_cast<int>(EGlobalRootSignatureLayout::EOutput)].InitAsDescriptorTable(1, &texTables[0]);
		slotRootParameter[static_cast<int>(EGlobalRootSignatureLayout::EAccelerationStructure)].InitAsShaderResourceView(0);
		slotRootParameter[static_cast<int>(EGlobalRootSignatureLayout::EPassCB)].InitAsConstantBufferView(0);
		slotRootParameter[static_cast<int>(EGlobalRootSignatureLayout::EObjSB)].InitAsShaderResourceView(1);
		slotRootParameter[static_cast<int>(EGlobalRootSignatureLayout::EMatSB)].InitAsShaderResourceView(2);
		slotRootParameter[static_cast<int>(EGlobalRootSignatureLayout::EVertices)].InitAsDescriptorTable(1, &texTables[1]);
		slotRootParameter[static_cast<int>(EGlobalRootSignatureLayout::EIndices)].InitAsDescriptorTable(1, &texTables[2]);
		slotRootParameter[static_cast<int>(EGlobalRootSignatureLayout::ESrvMaps)].InitAsDescriptorTable(1, &texTables[3]);
		slotRootParameter[static_cast<int>(EGlobalRootSignatureLayout::EUavMaps)].InitAsDescriptorTable(1, &texTables[4]);

		auto samplers = GetStaticSamplers();

		CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(
			_countof(slotRootParameter), slotRootParameter,
			static_cast<UINT>(samplers.size()), samplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_NONE
		);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice.Get(), globalRootSignatureDesc, mRootSignatures["dxr_global"].GetAddressOf()));
	}
	// Local root signature
	{
		CD3DX12_ROOT_SIGNATURE_DESC localRootSignatureDesc(0, nullptr);
		localRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice.Get(), localRootSignatureDesc, mRootSignatures["dxr_local"].GetAddressOf()));
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

	// Create the DXR output buffer UAV
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Format = BackBufferFormat;

	{
		auto handle = D3D12Util::GetCpuHandle(pDescHeap, static_cast<INT>(EDescriptors::EU_Output0), descSize);

		for (int i = 0; i < gNumFrameResources; ++i) {
			md3dDevice->CreateUnorderedAccessView(
				mDXROutputs[i].Get(),
				nullptr,
				&uavDesc,
				handle
			);
			handle.ptr += descSize;
		}
	}

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
			D3D12Util::GetCpuHandle(pDescHeap, static_cast<INT>(EDescriptors::ES_Vertices) + mGeometryBufferCount, descSize)
		);

		indexSrvDesc.Buffer.FirstElement = 0;
		indexSrvDesc.Buffer.NumElements = static_cast<UINT>(geo->IndexBufferCPU->GetBufferSize() / sizeof(std::uint32_t));

		md3dDevice->CreateShaderResourceView(
			geo->IndexBufferGPU.Get(),
			&indexSrvDesc,
			D3D12Util::GetCpuHandle(pDescHeap, static_cast<INT>(EDescriptors::ES_Indices) + mGeometryBufferCount, descSize)
		);

		++mGeometryBufferCount;
	}

	auto cpuStart = pDescHeap->GetCPUDescriptorHandleForHeapStart();
	auto gpuStart = pDescHeap->GetGPUDescriptorHandleForHeapStart();
	auto rtvCpuStart = mRtvHeap->GetCPUDescriptorHandleForHeapStart();
	auto dsvCpuStart = mDsvHeap->GetCPUDescriptorHandleForHeapStart();

	mShadowMap->BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuStart, static_cast<INT>(EDescriptors::ES_Shadow), descSize),
		CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuStart, static_cast<INT>(EDescriptors::ES_Shadow), descSize),
		CD3DX12_CPU_DESCRIPTOR_HANDLE(dsvCpuStart, static_cast<INT>(EDsvHeapLayout::EShaadow), dsvDescSize)
	);

	mGBuffer->BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuStart, static_cast<INT>(EDescriptors::ES_Color), descSize),
		CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuStart, static_cast<INT>(EDescriptors::ES_Color), descSize),
		CD3DX12_CPU_DESCRIPTOR_HANDLE(rtvCpuStart, static_cast<INT>(ERtvHeapLayout::EColor), rtvDescSize),
		descSize, rtvDescSize,
		mDepthStencilBuffer.Get()
	);

	mDxrShadowMap->BuildDescriptors(
		CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuStart, static_cast<INT>(EDescriptors::ES_DxrShadow), descSize),
		CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuStart, static_cast<INT>(EDescriptors::ES_DxrShadow), descSize),
		CD3DX12_CPU_DESCRIPTOR_HANDLE(cpuStart, static_cast<INT>(EDescriptors::EU_Shadow), descSize),
		CD3DX12_GPU_DESCRIPTOR_HANDLE(gpuStart, static_cast<INT>(EDescriptors::EU_Shadow), descSize)
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
		auto vs = mShaderManager->GetShader("backBufferVS");
		auto ps = mShaderManager->GetShader("backBufferPS");
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
		auto vs = mShaderManager->GetShader("gizmoVS");
		auto ps = mShaderManager->GetShader("gizmoPS");
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
		auto vs = mShaderManager->GetShader("shadowVS");
		auto ps = mShaderManager->GetShader("shadowPS");
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
		auto vs = mShaderManager->GetShader("gbufferVS");
		auto ps = mShaderManager->GetShader("gbufferPS");
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
	gbufferPsoDesc.RTVFormats[2] = NormalMapFormat;
	gbufferPsoDesc.RTVFormats[3] = SpecularMapFormat;
	CheckHResult(md3dDevice->CreateGraphicsPipelineState(&gbufferPsoDesc, IID_PPV_ARGS(&mPSOs["gbuffer"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC debugPsoDesc = quadPsoDesc;
	{
		auto vs = mShaderManager->GetShader("debugVS");
		auto ps = mShaderManager->GetShader("debugPS");
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
		auto vs = mShaderManager->GetShader("dxrBackBufferVS");
		auto ps = mShaderManager->GetShader("dxrBackBufferPS");
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
	std::vector<D3D12_RESOURCE_BARRIER> uavBarriers;
	for (const auto& pair : mBLASs) {
		D3D12_RESOURCE_BARRIER uavBarrier;
		uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
		uavBarrier.UAV.pResource = mBLASs[pair.first]->Result.Get();
		uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
		uavBarriers.push_back(uavBarrier);
	}
	mCommandList->ResourceBarrier(static_cast<UINT>(uavBarriers.size()), uavBarriers.data());

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
	D3D12_RESOURCE_BARRIER uavBarrier;
	uavBarrier.Type = D3D12_RESOURCE_BARRIER_TYPE_UAV;
	uavBarrier.UAV.pResource = mTLAS->Result.Get();
	uavBarrier.Flags = D3D12_RESOURCE_BARRIER_FLAG_NONE;
	mCommandList->ResourceBarrier(1, &uavBarrier);

	return true;
}

bool Renderer::BuildDXRPSOs() {
	// Subobjects need to be associated with DXIL exports (i.e. shaders) either by way of default or explicit associations.
	// Default association applies to every exported shader entrypoint that doesn't have any of the same type of subobject associated with it.
	// This simple sample utilizes default shader association except for local root signature subobject
	// which has an explicit association specified purely for demonstration purposes.
	CD3DX12_STATE_OBJECT_DESC dxrPso = { D3D12_STATE_OBJECT_TYPE_RAYTRACING_PIPELINE };

	auto rayGenLib = dxrPso.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
	auto rayGenShader = mShaderManager->GetRTShader("rayGen");
	D3D12_SHADER_BYTECODE rayGenLibDxil = CD3DX12_SHADER_BYTECODE(rayGenShader->GetBufferPointer(), rayGenShader->GetBufferSize());
	rayGenLib->SetDXILLibrary(&rayGenLibDxil);
	rayGenLib->DefineExport(L"RayGen");

	auto chitLib = dxrPso.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
	auto chitShader = mShaderManager->GetRTShader("closestHit");
	D3D12_SHADER_BYTECODE chitLibDxil = CD3DX12_SHADER_BYTECODE(chitShader->GetBufferPointer(), chitShader->GetBufferSize());
	chitLib->SetDXILLibrary(&chitLibDxil);
	chitLib->DefineExport(L"ClosestHit");

	auto missLib = dxrPso.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
	auto missShader = mShaderManager->GetRTShader("miss");
	D3D12_SHADER_BYTECODE missLibDxil = CD3DX12_SHADER_BYTECODE(missShader->GetBufferPointer(), missShader->GetBufferSize());
	missLib->SetDXILLibrary(&missLibDxil);
	missLib->DefineExport(L"Miss");

	auto higGroup = dxrPso.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
	higGroup->SetClosestHitShaderImport(L"ClosestHit");
	higGroup->SetHitGroupExport(L"HitGroup");
	higGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

	auto shadowRayLib = dxrPso.CreateSubobject<CD3DX12_DXIL_LIBRARY_SUBOBJECT>();
	auto shadowRayShader = mShaderManager->GetRTShader("shadowRay");
	D3D12_SHADER_BYTECODE shadowRayLibDxil = CD3DX12_SHADER_BYTECODE(shadowRayShader->GetBufferPointer(), shadowRayShader->GetBufferSize());
	shadowRayLib->SetDXILLibrary(&shadowRayLibDxil);
	LPCWSTR exports[] = { L"ShadowRayGen", L"ShadowClosestHit", L"ShadowMiss" };
	shadowRayLib->DefineExports(exports);

	auto shadowHitGroup = dxrPso.CreateSubobject<CD3DX12_HIT_GROUP_SUBOBJECT>();
	shadowHitGroup->SetClosestHitShaderImport(L"ShadowClosestHit");
	shadowHitGroup->SetHitGroupExport(L"ShadowHitGroup");
	shadowHitGroup->SetHitGroupType(D3D12_HIT_GROUP_TYPE_TRIANGLES);

	auto shaderConfig = dxrPso.CreateSubobject<CD3DX12_RAYTRACING_SHADER_CONFIG_SUBOBJECT>();
	UINT payloadSize = sizeof(XMFLOAT4);	// for pixel color
	UINT attribSize = sizeof(XMFLOAT2);		// for barycentrics
	shaderConfig->Config(payloadSize, attribSize);

	auto localRootSig = dxrPso.CreateSubobject<CD3DX12_LOCAL_ROOT_SIGNATURE_SUBOBJECT>();
	localRootSig->SetRootSignature(mRootSignatures["dxr_local"].Get());
	{
		auto rootSigAssociation = dxrPso.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
		rootSigAssociation->SetSubobjectToAssociate(*localRootSig);
		rootSigAssociation->AddExport(L"HitGroup");
	}
	{
		auto rootSigAssociation = dxrPso.CreateSubobject<CD3DX12_SUBOBJECT_TO_EXPORTS_ASSOCIATION_SUBOBJECT>();
		rootSigAssociation->SetSubobjectToAssociate(*localRootSig);
		rootSigAssociation->AddExport(L"ShadowHitGroup");
	}

	auto glbalRootSig = dxrPso.CreateSubobject<CD3DX12_GLOBAL_ROOT_SIGNATURE_SUBOBJECT>();
	glbalRootSig->SetRootSignature(mRootSignatures["dxr_global"].Get());

	auto pipelineConfig = dxrPso.CreateSubobject<CD3DX12_RAYTRACING_PIPELINE_CONFIG_SUBOBJECT>();
	UINT maxRecursionDepth = 1;
	pipelineConfig->Config(maxRecursionDepth);

	CheckHResult(md3dDevice->CreateStateObject(dxrPso, IID_PPV_ARGS(&mDXRPSOs["default"])));
	CheckHResult(mDXRPSOs["default"]->QueryInterface(IID_PPV_ARGS(&mDXRPSOProps["defaultProps"])));

	return true;
}

bool Renderer::BuildShaderTables() {
	UINT shaderIdentifierSize = D3D12_SHADER_IDENTIFIER_SIZE_IN_BYTES;

	void* rayGenShaderIdentifier = mDXRPSOProps["defaultProps"]->GetShaderIdentifier(L"RayGen");
	void* missShaderIdentifier = mDXRPSOProps["defaultProps"]->GetShaderIdentifier(L"Miss");
	void* hitGroupShaderIdentifier = mDXRPSOProps["defaultProps"]->GetShaderIdentifier(L"HitGroup");

	void* shadowRayGenShaderIdentifier = mDXRPSOProps["defaultProps"]->GetShaderIdentifier(L"ShadowRayGen");
	void* shadowMissShaderIdentifier = mDXRPSOProps["defaultProps"]->GetShaderIdentifier(L"ShadowMiss");
	void* shadowHitGroupShaderIdentifier = mDXRPSOProps["defaultProps"]->GetShaderIdentifier(L"ShadowHitGroup");

	// Ray gen shader table
	ShaderTable rayGenShaderTable(md3dDevice.Get(), 1, shaderIdentifierSize);
	CheckIsValid(rayGenShaderTable.Initialze());
	rayGenShaderTable.push_back(ShaderRecord(rayGenShaderIdentifier, shaderIdentifierSize));
	mShaderTables["rayGen"] = rayGenShaderTable.GetResource();

	// Miss shader table
	ShaderTable missShaderTable(md3dDevice.Get(), 1, shaderIdentifierSize);
	CheckIsValid(missShaderTable.Initialze());
	missShaderTable.push_back(ShaderRecord(missShaderIdentifier, shaderIdentifierSize));
	mShaderTables["miss"] = missShaderTable.GetResource();

	// Hit group shader table
	ShaderTable hitGroupTable(md3dDevice.Get(), 1, shaderIdentifierSize);
	CheckIsValid(hitGroupTable.Initialze());
	{
		//struct RootArguments {
		//	D3D12_GPU_VIRTUAL_ADDRESS MatCB;
		//} rootArguments;
		//
		//rootArguments.MatCB = mFrameResources[i]->MaterialCB.Resource()->GetGPUVirtualAddress();

		hitGroupTable.push_back(ShaderRecord(hitGroupShaderIdentifier, shaderIdentifierSize));
	}
	mShaderTables["hitGroup"] = hitGroupTable.GetResource();

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
			XMStoreFloat4x4(&objData.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objData.TexTransform, XMMatrixTranspose(texTransform));
			objData.GeometryIndex = e->Geo->GeometryIndex;
			objData.MaterialIndex = e->Mat->MatSBIndex;

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
	mMainPassCB->Lights[0].Direction = mLightDir;
	mMainPassCB->Lights[0].Strength = { 0.4f, 0.4f, 0.4f };

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

bool Renderer::Rasterize() {
	CheckIsValid(DrawShadowMap());
	CheckIsValid(DrawGBuffer());
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
			static_cast<UINT>(ERasterRootSignatureLayout::EConsts), 
			ri->ObjSBIndex, 
			static_cast<UINT>(ERasterRootConstantsLayout::EInstanceID)
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
	mCommandList->SetGraphicsRootConstantBufferView(static_cast<UINT>(ERasterRootSignatureLayout::EPassCB), shadowPassCBAddress);

	const auto& objSBAddress = mCurrFrameResource->ObjectSB.Resource()->GetGPUVirtualAddress();
	mCommandList->SetGraphicsRootShaderResourceView(static_cast<UINT>(ERasterRootSignatureLayout::EObjSB), objSBAddress);

	const auto& matSBAddress = mCurrFrameResource->MaterialSB.Resource()->GetGPUVirtualAddress();
	mCommandList->SetGraphicsRootShaderResourceView(static_cast<UINT>(ERasterRootSignatureLayout::EMatSB), matSBAddress);
	
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
	const auto pNormalMap = mGBuffer->NormalMapResource();
	const auto pSpecularMap = mGBuffer->SpecularMapResource();

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
			pNormalMap,
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
		)
		};
		mCommandList->ResourceBarrier(
			_countof(barriers),
			barriers
		);
	}

	const auto colorRtv = mGBuffer->ColorMapRtv();
	const auto albedoRtv = mGBuffer->AlbedoMapRtv();
	const auto normalRtv = mGBuffer->NormalMapRtv();
	const auto specularRtv = mGBuffer->SpecularMapRtv();
	mCommandList->ClearRenderTargetView(colorRtv, GBuffer::ColorMapClearValues, 0, nullptr);
	mCommandList->ClearRenderTargetView(albedoRtv, GBuffer::AlbedoMapClearValues, 0, nullptr);
	mCommandList->ClearRenderTargetView(normalRtv, GBuffer::NormalMapClearValues, 0, nullptr);
	mCommandList->ClearRenderTargetView(specularRtv, GBuffer::SpecularMapClearValues, 0, nullptr);

	std::array<D3D12_CPU_DESCRIPTOR_HANDLE, GBuffer::NumRenderTargets> renderTargets = { colorRtv, albedoRtv, normalRtv, specularRtv };
	mCommandList->OMSetRenderTargets(static_cast<UINT>(renderTargets.size()), renderTargets.data(), true, &DepthStencilView());
	mCommandList->ClearDepthStencilView(DepthStencilView(), D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL, 1.0f, 0, 0, nullptr);

	auto passCBAddress = mCurrFrameResource->PassCB.Resource()->GetGPUVirtualAddress();
	mCommandList->SetGraphicsRootConstantBufferView(static_cast<UINT>(ERasterRootSignatureLayout::EPassCB), passCBAddress);

	const auto& objSBAddress = mCurrFrameResource->ObjectSB.Resource()->GetGPUVirtualAddress();
	mCommandList->SetGraphicsRootShaderResourceView(static_cast<UINT>(ERasterRootSignatureLayout::EObjSB), objSBAddress);

	const auto& matSBAddress = mCurrFrameResource->MaterialSB.Resource()->GetGPUVirtualAddress();
	mCommandList->SetGraphicsRootShaderResourceView(static_cast<UINT>(ERasterRootSignatureLayout::EMatSB), matSBAddress);

	CheckIsValid(DrawRenderItems(mRitems[ERenderTypes::EOpaque]));

	{
		D3D12_RESOURCE_BARRIER barriers[] = {
		CD3DX12_RESOURCE_BARRIER::Transition(
			pSpecularMap,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		),
		CD3DX12_RESOURCE_BARRIER::Transition(
			mDepthStencilBuffer.Get(),
			D3D12_RESOURCE_STATE_DEPTH_WRITE,
			D3D12_RESOURCE_STATE_DEPTH_READ
		),
		CD3DX12_RESOURCE_BARRIER::Transition(
			pNormalMap,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		),
		CD3DX12_RESOURCE_BARRIER::Transition(
			pAlbedoMap,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE
		),
		CD3DX12_RESOURCE_BARRIER::Transition(
			pColorMap,
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
	mCommandList->SetGraphicsRootConstantBufferView(static_cast<UINT>(ERasterRootSignatureLayout::EPassCB), passCBAddress);

	const auto& objSBAddress = mCurrFrameResource->ObjectSB.Resource()->GetGPUVirtualAddress();
	mCommandList->SetGraphicsRootShaderResourceView(static_cast<UINT>(ERasterRootSignatureLayout::EObjSB), objSBAddress);

	const auto& matSBAddress = mCurrFrameResource->MaterialSB.Resource()->GetGPUVirtualAddress();
	mCommandList->SetGraphicsRootShaderResourceView(static_cast<UINT>(ERasterRootSignatureLayout::EMatSB), matSBAddress);

	mCommandList->SetGraphicsRootDescriptorTable(
		static_cast<UINT>(ERasterRootSignatureLayout::ESrvMaps),
		D3D12Util::GetGpuHandle(
			pDescHeap,
			static_cast<INT>(EDescriptors::ES_Color),
			GetCbvSrvUavDescriptorSize()
		)
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
	mCommandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			renderTarget,
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET
		)
	);

	mCommandList->OMSetRenderTargets(1, &CurrentBackBufferView(), true, nullptr);

	const auto& passCBAddress = mCurrFrameResource->PassCB.Resource()->GetGPUVirtualAddress();
	mCommandList->SetGraphicsRootConstantBufferView(static_cast<UINT>(ERasterRootSignatureLayout::EPassCB), passCBAddress);

	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
	mCommandList->DrawInstanced(2, 3, 0, 0);

	if (bDisplayMaps) {
		mCommandList->SetPipelineState(mPSOs["debug"].Get());

		mCommandList->RSSetViewports(1, &mScreenViewport);
		mCommandList->RSSetScissorRects(1, &mScissorRect);

		mCommandList->SetGraphicsRoot32BitConstant(
			static_cast<UINT>(ERasterRootSignatureLayout::EConsts),
			static_cast<UINT>(bRaytracing),
			static_cast<UINT>(ERasterRootConstantsLayout::EIsRaytracing)
		);

		mCommandList->SetGraphicsRootDescriptorTable(
			static_cast<UINT>(ERasterRootSignatureLayout::ESrvMaps),
			D3D12Util::GetGpuHandle(mCbvSrvUavHeap.Get(), static_cast<INT>(EDescriptors::Srv_Start), GetCbvSrvUavDescriptorSize())
		);

		mCommandList->IASetVertexBuffers(0, 0, nullptr);
		mCommandList->IASetIndexBuffer(nullptr);
		mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		mCommandList->DrawInstanced(6, 5, 0, 0);
	}

	mCommandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			renderTarget,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT
		)
	);

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
	CheckIsValid(dxrDrawBackBuffer());

	return true;
}

bool Renderer::dxrDrawShadowMap() {
	CheckHResult(mCommandList->Reset(mCurrFrameResource->CmdListAlloc.Get(), nullptr));

	mCommandList->SetComputeRootSignature(mRootSignatures["dxr_global"].Get());

	const auto pDescHeap = mCbvSrvUavHeap.Get();
	ID3D12DescriptorHeap* descriptorHeaps[] = { pDescHeap };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	UINT cbvSrvUavDescriptorSize = GetCbvSrvUavDescriptorSize();

	mCommandList->SetComputeRootShaderResourceView(static_cast<UINT>(EGlobalRootSignatureLayout::EAccelerationStructure), mTLAS->Result->GetGPUVirtualAddress());

	const auto& passCBAddress = mCurrFrameResource->PassCB.Resource()->GetGPUVirtualAddress();
	mCommandList->SetComputeRootConstantBufferView(static_cast<UINT>(EGlobalRootSignatureLayout::EPassCB), passCBAddress);

	const auto& objSBAddress = mCurrFrameResource->ObjectSB.Resource()->GetGPUVirtualAddress();
	mCommandList->SetComputeRootShaderResourceView(static_cast<UINT>(EGlobalRootSignatureLayout::EObjSB), objSBAddress);

	const auto& matSBAddress = mCurrFrameResource->MaterialSB.Resource()->GetGPUVirtualAddress();
	mCommandList->SetComputeRootShaderResourceView(static_cast<UINT>(EGlobalRootSignatureLayout::EMatSB), matSBAddress);

	mCommandList->SetComputeRootDescriptorTable(
		static_cast<UINT>(EGlobalRootSignatureLayout::EOutput),
		D3D12Util::GetGpuHandle(pDescHeap, static_cast<INT>(EDescriptors::EU_Output0) + mCurrFrameResourceIndex, cbvSrvUavDescriptorSize)
	);
	mCommandList->SetComputeRootDescriptorTable(
		static_cast<UINT>(EGlobalRootSignatureLayout::EVertices),
		D3D12Util::GetGpuHandle(pDescHeap, static_cast<INT>(EDescriptors::ES_Vertices), cbvSrvUavDescriptorSize)
	);
	mCommandList->SetComputeRootDescriptorTable(
		static_cast<UINT>(EGlobalRootSignatureLayout::EIndices),
		D3D12Util::GetGpuHandle(pDescHeap, static_cast<INT>(EDescriptors::ES_Indices), cbvSrvUavDescriptorSize)
	);

	mCommandList->SetComputeRootDescriptorTable(
		static_cast<UINT>(EGlobalRootSignatureLayout::ESrvMaps),
		D3D12Util::GetGpuHandle(pDescHeap, static_cast<INT>(EDescriptors::Srv_Start), cbvSrvUavDescriptorSize)
	);
	mCommandList->SetComputeRootDescriptorTable(
		static_cast<UINT>(EGlobalRootSignatureLayout::EUavMaps),
		D3D12Util::GetGpuHandle(pDescHeap, static_cast<INT>(EDescriptors::Uav_Start), cbvSrvUavDescriptorSize)
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
	
	mCommandList->SetPipelineState1(mDXRPSOs["default"].Get());
	mCommandList->DispatchRays(&dispatchDesc);

	CheckHResult(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
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
	mCommandList->SetGraphicsRootConstantBufferView(static_cast<UINT>(ERasterRootSignatureLayout::EPassCB), passCBAddress);

	mCommandList->SetGraphicsRootDescriptorTable(
		static_cast<UINT>(ERasterRootSignatureLayout::ESrvMaps),
		D3D12Util::GetGpuHandle(mCbvSrvUavHeap.Get(), static_cast<INT>(EDescriptors::Srv_Start), GetCbvSrvUavDescriptorSize())
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