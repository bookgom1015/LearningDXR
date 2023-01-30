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
	std::array<const CD3DX12_STATIC_SAMPLER_DESC, 6> GetStaticSamplers() {
		const CD3DX12_STATIC_SAMPLER_DESC pointWrap(
			0, // shaderRegister
			D3D12_FILTER_MIN_MAG_MIP_POINT,		// filter
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,	// addressU
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,	// addressV
			D3D12_TEXTURE_ADDRESS_MODE_WRAP		// addressW
		);

		const CD3DX12_STATIC_SAMPLER_DESC pointClamp(
			1, // shaderRegister
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
			3, // shaderRegister
			D3D12_FILTER_MIN_MAG_MIP_LINEAR,	// filter
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	// addressU
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	// addressV
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP	// addressW
		);

		const CD3DX12_STATIC_SAMPLER_DESC anisotropicWrap(
			4, // shaderRegister
			D3D12_FILTER_ANISOTROPIC,			// filter
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,	// addressU
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,	// addressV
			D3D12_TEXTURE_ADDRESS_MODE_WRAP,	// addressW
			0.0f,								// mipLODBias
			8									// maxAnisotropy
		);

		const CD3DX12_STATIC_SAMPLER_DESC anisotropicClamp(
			5, // shaderRegister
			D3D12_FILTER_ANISOTROPIC,			// filter
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	// addressU
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	// addressV
			D3D12_TEXTURE_ADDRESS_MODE_CLAMP,	// addressW
			0.0f,								// mipLODBias
			8									// maxAnisotropy
		);

		return { pointWrap, pointClamp, linearWrap, linearClamp, anisotropicWrap, anisotropicClamp };
	}
}

Renderer::Renderer() {
	bIsCleanedUp = false;
	bInitialized = false;
	bRaytracing = false;
	bShowImgGui = false;
	mCurrFrameResourceIndex = 0;

	mShaderManager = std::make_unique<ShaderManager>();
	mMainPassCB = std::make_unique<PassConstants>();
	mDebugPassCB = std::make_unique<DebugPassConstants>();
	mTLAS = std::make_unique<AccelerationStructureBuffer>();

	mDXROutputs.resize(gNumFrameResources);
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

	// Shared
	CheckIsValid(CompileShaders());
	CheckIsValid(BuildFrameResources());
	CheckIsValid(BuildGeometries());
	CheckIsValid(BuildMaterials());
	CheckIsValid(BuildResources());
	CheckIsValid(BuildDescriptorHeaps());
	CheckIsValid(BuildDescriptors());

	// Raterization
	CheckIsValid(BuildRootSignatures());
	CheckIsValid(BuildPSOs());
	CheckIsValid(BuildRenderItems());

	// Ray-tracing
	CheckIsValid(BuildDXRRootSignatures());
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
	CheckIsValid(UpdateMaterialCB(gt));

	CheckIsValid(UpdateDebugPassCB(gt));

	return true;
}

bool Renderer::Draw() {
	CheckHResult(mCurrFrameResource->CmdListAlloc->Reset());

	if (bRaytracing) {
		CheckIsValid(Raytrace());
	}
	else {
		CheckIsValid(Rasterize());
	}
	CheckIsValid(DrawDebugLayer());
	if (bShowImgGui) CheckIsValid(DrawImGui());;

	CheckHResult(mSwapChain->Present(0, 0));
	NextBackBuffer();

	mCurrFrameResource->Fence = static_cast<UINT>(IncCurrentFence());
	mCommandQueue->Signal(mFence.Get(), GetCurrentFence());

	return true;
}

bool Renderer::OnResize(UINT width, UINT height) {
	CheckIsValid(LowRenderer::OnResize(width, height));

	BuildDebugViewport();

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

void Renderer::ShowImGui(bool state) {
	bShowImgGui = state;
}

bool Renderer::CreateRtvAndDsvDescriptorHeaps() {
	D3D12_DESCRIPTOR_HEAP_DESC rtvHeapDesc;
	rtvHeapDesc.NumDescriptors = SwapChainBufferCount;
	rtvHeapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
	rtvHeapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
	rtvHeapDesc.NodeMask = 0;
	CheckHResult(md3dDevice->CreateDescriptorHeap(&rtvHeapDesc, IID_PPV_ARGS(mRtvHeap.GetAddressOf())));

	D3D12_DESCRIPTOR_HEAP_DESC dsvHeapDesc;
	dsvHeapDesc.NumDescriptors = 1;
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

	const auto pDescHeap = mDescriptorHeap.Get();
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
		const auto filePath = ShaderFilePathW + L"Default.hlsl";
		CheckIsValid(mShaderManager->CompileShader(filePath, nullptr, "VS", "vs_5_1", "defaultVS"));
		CheckIsValid(mShaderManager->CompileShader(filePath, nullptr, "PS", "ps_5_1", "defaultPS"));
	}
	{
		const auto filePath = ShaderFilePathW + L"Gizmo.hlsl";
		CheckIsValid(mShaderManager->CompileShader(filePath, nullptr, "VS", "vs_5_1", "gizmoVS"));
		CheckIsValid(mShaderManager->CompileShader(filePath, nullptr, "PS", "ps_5_1", "gizmoPS"));
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

	return true;
}

bool Renderer::BuildFrameResources() {
	for (int i = 0; i < gNumFrameResources; i++) {
		mFrameResources.push_back(std::make_unique<FrameResource>(md3dDevice.Get(), 1, 32, 32));
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

		geo->DrawArgs["grid"] = gridSubmesh;

		mGeometries[geo->Name] = std::move(geo);
	}

	return true;
}

bool Renderer::BuildMaterials() {
	auto defaultMat = std::make_unique<Material>();
	defaultMat->Name = "defaultMat";
	defaultMat->MatCBIndex = 0;
	defaultMat->DiffuseAlbedo = XMFLOAT4(1.0f, 1.0f, 1.0f, 1.0f);
	defaultMat->FresnelR0 = XMFLOAT3(0.88725f, 0.88725f, 0.88725f);
	defaultMat->Roughness = 0.1f;

	mMaterials[defaultMat->Name] = std::move(defaultMat);

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
	// For rasterization.
	//
	{
		CD3DX12_ROOT_PARAMETER slotRootParameter[static_cast<int>(ERasterRootSignatureParams::Count)];

		CD3DX12_DESCRIPTOR_RANGE uavTable;
		uavTable.Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0);

		slotRootParameter[static_cast<int>(ERasterRootSignatureParams::EObjectCB)].InitAsConstantBufferView(0);
		slotRootParameter[static_cast<int>(ERasterRootSignatureParams::EPassCB)].InitAsConstantBufferView(1);
		slotRootParameter[static_cast<int>(ERasterRootSignatureParams::EMatCB)].InitAsConstantBufferView(2);

		auto samplers = GetStaticSamplers();

		CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
			_countof(slotRootParameter), slotRootParameter,
			static_cast<UINT>(samplers.size()), samplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice.Get(), rootSigDesc, mRootSignatures["raster"].GetAddressOf()));
	}
	//
	// For debug.
	//
	{
		CD3DX12_ROOT_PARAMETER slotRootParameter[static_cast<int>(EDebugRootSignatureParams::Count)];

		slotRootParameter[static_cast<int>(EDebugRootSignatureParams::EPassCB)].InitAsConstantBufferView(0);

		auto samplers = GetStaticSamplers();

		CD3DX12_ROOT_SIGNATURE_DESC rootSigDesc(
			_countof(slotRootParameter), slotRootParameter,
			static_cast<UINT>(samplers.size()), samplers.data(),
			D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT);

		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice.Get(), rootSigDesc, mRootSignatures["debug"].GetAddressOf()));
	}

	return true;
}

bool Renderer::BuildDescriptorHeaps() {
	D3D12_DESCRIPTOR_HEAP_DESC heapDesc = {};
	heapDesc.NumDescriptors = 32;
	heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;
	heapDesc.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
	md3dDevice->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&mDescriptorHeap));

	return true;
}

bool Renderer::BuildDescriptors() {
	const auto pDescHeap = mDescriptorHeap.Get();
	auto descSize = GetCbvSrvUavDescriptorSize();

	// Create the DXR output buffer UAV
	D3D12_UNORDERED_ACCESS_VIEW_DESC uavDesc = {};
	uavDesc.ViewDimension = D3D12_UAV_DIMENSION_TEXTURE2D;
	uavDesc.Format = BackBufferFormat;

	{
		auto handle = D3D12Util::GetCpuHandle(pDescHeap, static_cast<INT>(EDescriptors::EU_Output), descSize);

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

	// Create the vertex buffer SRV
	auto geo = mGeometries["sphere"].get();

	D3D12_SHADER_RESOURCE_VIEW_DESC vertexSrvDesc = {};
	vertexSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	vertexSrvDesc.Format = DXGI_FORMAT_UNKNOWN;
	vertexSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_NONE;
	vertexSrvDesc.Buffer.StructureByteStride = sizeof(Vertex);
	vertexSrvDesc.Buffer.FirstElement = 0;
	vertexSrvDesc.Buffer.NumElements = static_cast<UINT>(geo->VertexBufferCPU->GetBufferSize() / sizeof(Vertex));
	vertexSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	md3dDevice->CreateShaderResourceView(
		geo->VertexBufferGPU.Get(), 
		&vertexSrvDesc, 
		D3D12Util::GetCpuHandle(pDescHeap, static_cast<INT>(EDescriptors::ES_Vertices), descSize)
	);

	D3D12_SHADER_RESOURCE_VIEW_DESC indexSrvDesc = {};
	indexSrvDesc.ViewDimension = D3D12_SRV_DIMENSION_BUFFER;
	indexSrvDesc.Format = DXGI_FORMAT_R32_TYPELESS;
	indexSrvDesc.Buffer.Flags = D3D12_BUFFER_SRV_FLAG_RAW;
	indexSrvDesc.Buffer.StructureByteStride = 0;
	indexSrvDesc.Buffer.FirstElement = 0;
	indexSrvDesc.Buffer.NumElements = static_cast<UINT>(geo->IndexBufferCPU->GetBufferSize() / sizeof(std::uint32_t));
	indexSrvDesc.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;

	md3dDevice->CreateShaderResourceView(
		geo->IndexBufferGPU.Get(),
		&indexSrvDesc,
		D3D12Util::GetCpuHandle(pDescHeap, static_cast<INT>(EDescriptors::ES_Indices), descSize)
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
	{
		auto vs = mShaderManager->GetShader("defaultVS");
		auto ps = mShaderManager->GetShader("defaultPS");
		defaultPsoDesc.VS = {
			reinterpret_cast<BYTE*>(vs->GetBufferPointer()),
			vs->GetBufferSize()
		};
		defaultPsoDesc.PS = {
			reinterpret_cast<BYTE*>(ps->GetBufferPointer()),
			ps->GetBufferSize()
		};
	}
	defaultPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	defaultPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	defaultPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	defaultPsoDesc.SampleMask = UINT_MAX;
	defaultPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;
	defaultPsoDesc.NumRenderTargets = 1;
	defaultPsoDesc.RTVFormats[0] = BackBufferFormat;
	defaultPsoDesc.SampleDesc.Count = 1;
	defaultPsoDesc.SampleDesc.Quality = 0;
	defaultPsoDesc.DSVFormat = DepthStencilFormat;
	CheckHResult(md3dDevice->CreateGraphicsPipelineState(&defaultPsoDesc, IID_PPV_ARGS(&mPSOs["default"])));

	D3D12_GRAPHICS_PIPELINE_STATE_DESC gizmoPsoDesc;
	ZeroMemory(&gizmoPsoDesc, sizeof(D3D12_GRAPHICS_PIPELINE_STATE_DESC));
	gizmoPsoDesc.InputLayout = { nullptr, 0 };
	gizmoPsoDesc.pRootSignature = mRootSignatures["debug"].Get();
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
	gizmoPsoDesc.RasterizerState = CD3DX12_RASTERIZER_DESC(D3D12_DEFAULT);
	gizmoPsoDesc.BlendState = CD3DX12_BLEND_DESC(D3D12_DEFAULT);
	gizmoPsoDesc.DepthStencilState = CD3DX12_DEPTH_STENCIL_DESC(D3D12_DEFAULT);
	gizmoPsoDesc.DepthStencilState.DepthEnable = FALSE;
	gizmoPsoDesc.SampleMask = UINT_MAX;
	gizmoPsoDesc.PrimitiveTopologyType = D3D12_PRIMITIVE_TOPOLOGY_TYPE_LINE;
	gizmoPsoDesc.NumRenderTargets = 1;
	gizmoPsoDesc.RTVFormats[0] = BackBufferFormat;
	gizmoPsoDesc.SampleDesc.Count = 1;
	gizmoPsoDesc.SampleDesc.Quality = 0;
	gizmoPsoDesc.DSVFormat = DXGI_FORMAT_UNKNOWN;
	CheckHResult(md3dDevice->CreateGraphicsPipelineState(&gizmoPsoDesc, IID_PPV_ARGS(&mPSOs["gizmo"])));

	return true;
}

bool Renderer::BuildRenderItems() {
	static UINT index = 0;

	{
		auto sphereRitem = std::make_unique<RenderItem>();
		sphereRitem->World = MathHelper::Identity4x4();
		sphereRitem->ObjCBIndex = index++;
		sphereRitem->Geo = mGeometries["sphere"].get();
		sphereRitem->Mat = mMaterials["defaultMat"].get();
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
		gridRitem->ObjCBIndex = index++;
		gridRitem->Geo = mGeometries["grid"].get();
		gridRitem->Mat = mMaterials["defaultMat"].get();
		gridRitem->PrimitiveType = D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST;
		gridRitem->IndexCount = gridRitem->Geo->DrawArgs["grid"].IndexCount;
		gridRitem->StartIndexLocation = gridRitem->Geo->DrawArgs["grid"].StartIndexLocation;
		gridRitem->BaseVertexLocation = gridRitem->Geo->DrawArgs["grid"].BaseVertexLocation;
		mRitems[ERenderTypes::EOpaque].push_back(gridRitem.get());
		mAllRitems.push_back(std::move(gridRitem));
	}

	return true;
}

bool Renderer::BuildDXRRootSignatures() {
	//
	// Global root signature
	//
	{
		CD3DX12_DESCRIPTOR_RANGE ranges[2];
		ranges[0].Init(D3D12_DESCRIPTOR_RANGE_TYPE_UAV, 1, 0); // 1 output texture
		ranges[1].Init(D3D12_DESCRIPTOR_RANGE_TYPE_SRV, 2, 1); // 2 static vertex and index buffers

		CD3DX12_ROOT_PARAMETER params[static_cast<int>(EGlobalRootSignatureParams::Count)];
		params[static_cast<int>(EGlobalRootSignatureParams::EOutput)].InitAsDescriptorTable(1, &ranges[0]);
		params[static_cast<int>(EGlobalRootSignatureParams::EAccelerationStructure)].InitAsShaderResourceView(0);
		params[static_cast<int>(EGlobalRootSignatureParams::EPassCB)].InitAsConstantBufferView(0);
		params[static_cast<int>(EGlobalRootSignatureParams::EGeometryBuffers)].InitAsDescriptorTable(1, &ranges[1]);

		CD3DX12_ROOT_SIGNATURE_DESC globalRootSignatureDesc(_countof(params), params);
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice.Get(), globalRootSignatureDesc, mRootSignatures["dxr_global"].GetAddressOf()));
	}
	//
	// Local root signature
	//
	{
		CD3DX12_ROOT_PARAMETER params[static_cast<int>(ELocalRootSignatureParams::Count)];
		params[static_cast<int>(ELocalRootSignatureParams::EObjectCB)].InitAsConstants(D3D12Util::CalcNumUintValues<MaterialConstants>(), 1);

		CD3DX12_ROOT_SIGNATURE_DESC localRootSignatureDesc(_countof(params), params);
		localRootSignatureDesc.Flags = D3D12_ROOT_SIGNATURE_FLAG_LOCAL_ROOT_SIGNATURE;
		CheckIsValid(D3D12Util::CreateRootSignature(md3dDevice.Get(), localRootSignatureDesc, mRootSignatures["dxr_local"].GetAddressOf()));
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
		instanceDesc.InstanceID = 0;
		instanceDesc.InstanceContributionToHitGroupIndex = 0;
		instanceDesc.InstanceMask = 0xFF;
		instanceDesc.Transform[0][0] = instanceDesc.Transform[1][1] = instanceDesc.Transform[2][2] = 1.0f;
		instanceDesc.AccelerationStructure = mBLASs["sphere"]->Result->GetGPUVirtualAddress();
		instanceDesc.Flags = D3D12_RAYTRACING_INSTANCE_FLAG_TRIANGLE_FRONT_COUNTERCLOCKWISE;
		instanceDescs.push_back(instanceDesc);
	}
	{
		D3D12_RAYTRACING_INSTANCE_DESC instanceDesc = {};
		instanceDesc.InstanceID = 1;
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
		auto& matCB = mCurrFrameResource->MaterialCB;

		struct RootArguments {
			MaterialConstants MatCB;
		} rootArguments;

		rootArguments.MatCB.DiffuseAlbedo = mMaterials["defaultMat"]->DiffuseAlbedo;
		rootArguments.MatCB.FresnelR0 = mMaterials["defaultMat"]->FresnelR0;
		rootArguments.MatCB.Roughness = mMaterials["defaultMat"]->Roughness;
		rootArguments.MatCB.MatTransform = mMaterials["defaultMat"]->MatTransform;

		hitGroupTable.push_back(ShaderRecord(hitGroupShaderIdentifier, shaderIdentifierSize, &rootArguments, sizeof(rootArguments)));
	}
	mShaderTables["hitGroup"] = hitGroupTable.GetResource();

	return true;
}

bool Renderer::UpdateObjectCB(const GameTimer& gt) {
	auto& currObjectCB = mCurrFrameResource->ObjectCB;
	for (auto& e : mAllRitems) {
		// Only update the cbuffer data if the constants have changed.  
		// This needs to be tracked per frame resource.
		if (e->NumFramesDirty > 0) {
			XMMATRIX world = XMLoadFloat4x4(&e->World);
			XMMATRIX texTransform = XMLoadFloat4x4(&e->TexTransform);

			ObjectConstants objConstants;
			XMStoreFloat4x4(&objConstants.World, XMMatrixTranspose(world));
			XMStoreFloat4x4(&objConstants.TexTransform, XMMatrixTranspose(texTransform));

			currObjectCB.CopyData(e->ObjCBIndex, objConstants);

			// Next FrameResource need to be updated too.
			e->NumFramesDirty--;
		}
	}

	return true;
}

bool Renderer::UpdatePassCB(const GameTimer& gt) {
	XMMATRIX view = mCamera->GetViewMatrix();
	XMMATRIX proj = mCamera->GetProjectionMatrix();
	XMMATRIX viewProj = XMMatrixMultiply(view, proj);

	XMMATRIX invView = XMMatrixInverse(&XMMatrixDeterminant(view), view);
	XMMATRIX invProj = XMMatrixInverse(&XMMatrixDeterminant(proj), proj);
	XMMATRIX invViewProj = XMMatrixInverse(&XMMatrixDeterminant(viewProj), viewProj);

	XMStoreFloat4x4(&mMainPassCB->View, XMMatrixTranspose(view));
	XMStoreFloat4x4(&mMainPassCB->InvView, XMMatrixTranspose(invView));
	XMStoreFloat4x4(&mMainPassCB->Proj, XMMatrixTranspose(proj));
	XMStoreFloat4x4(&mMainPassCB->InvProj, XMMatrixTranspose(invProj));
	XMStoreFloat4x4(&mMainPassCB->ViewProj, XMMatrixTranspose(viewProj));
	XMStoreFloat4x4(&mMainPassCB->InvViewProj, XMMatrixTranspose(invViewProj));
	mMainPassCB->EyePosW = mCamera->GetCameraPosition();
	mMainPassCB->AmbientLight = { 0.3f, 0.3f, 0.42f, 1.0f };
	mMainPassCB->Lights[0].Direction = { 0.57735f, -0.57735f, 0.57735f };
	mMainPassCB->Lights[0].Strength = { 0.6f, 0.6f, 0.6f };

	auto& currPassCB = mCurrFrameResource->PassCB;
	currPassCB.CopyData(0, *mMainPassCB);

	return true;
}

bool Renderer::UpdateMaterialCB(const GameTimer& gt) {
	auto& currMaterialCB = mCurrFrameResource->MaterialCB;
	for (auto& e : mMaterials) {
		// Only update the cbuffer data if the constants have changed.  If the cbuffer
		// data changes, it needs to be updated for each FrameResource.
		Material* mat = e.second.get();
		if (mat->NumFramesDirty > 0) {
			XMMATRIX matTransform = XMLoadFloat4x4(&mat->MatTransform);

			MaterialConstants matConsts;
			matConsts.DiffuseAlbedo = mat->DiffuseAlbedo;
			matConsts.FresnelR0 = mat->FresnelR0;
			matConsts.Roughness = mat->Roughness;
			XMStoreFloat4x4(&matConsts.MatTransform, XMMatrixTranspose(matTransform));

			currMaterialCB.CopyData(mat->MatCBIndex, matConsts);

			// Next FrameResource need to be updated too.
			mat->NumFramesDirty--;
		}
	}

	return true;
}

bool Renderer::UpdateDebugPassCB(const GameTimer& gt) {
	XMMATRIX view = mCamera->GetViewMatrix(true);
	XMMATRIX proj = mCamera->GetProjectionMatrix();
	XMMATRIX viewProj = XMMatrixMultiply(view, proj);

	XMStoreFloat4x4(&mDebugPassCB->ViewProj, XMMatrixTranspose(viewProj));

	auto& currPassCB = mCurrFrameResource->DebugPassCB;
	currPassCB.CopyData(0, *mDebugPassCB);

	return true;
}

bool Renderer::Rasterize() {
	CheckHResult(mCommandList->Reset(mCurrFrameResource->CmdListAlloc.Get(), mPSOs["default"].Get()));

	mCommandList->SetGraphicsRootSignature(mRootSignatures["raster"].Get());

	ID3D12DescriptorHeap* descriptorHeaps[] = { mDescriptorHeap.Get() };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	mCommandList->RSSetViewports(1, &mScreenViewport);
	mCommandList->RSSetScissorRects(1, &mScissorRect);

	const auto pCurrBackBuffer = CurrentBackBuffer();
	mCommandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			pCurrBackBuffer,
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_RENDER_TARGET)
	);

	auto pCurrBackBufferView = CurrentBackBufferView();
	mCommandList->ClearRenderTargetView(pCurrBackBufferView, Colors::AliceBlue, 0, nullptr);
	mCommandList->OMSetRenderTargets(1, &pCurrBackBufferView, true, &DepthStencilView());
	mCommandList->ClearDepthStencilView(
		DepthStencilView(),
		D3D12_CLEAR_FLAG_DEPTH | D3D12_CLEAR_FLAG_STENCIL,
		1.0f,
		0,
		0,
		nullptr
	);

	const auto& passCB = mCurrFrameResource->PassCB;
	mCommandList->SetGraphicsRootConstantBufferView(static_cast<UINT>(ERasterRootSignatureParams::EPassCB), passCB.Resource()->GetGPUVirtualAddress());

	CheckIsValid(DrawRenderItems(mRitems[ERenderTypes::EOpaque]));

	mCommandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			pCurrBackBuffer,
			D3D12_RESOURCE_STATE_RENDER_TARGET,
			D3D12_RESOURCE_STATE_PRESENT)
	);

	CheckHResult(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	return true;
}

bool Renderer::Raytrace() {
	CheckHResult(mCommandList->Reset(mCurrFrameResource->CmdListAlloc.Get(), nullptr));

	mCommandList->SetComputeRootSignature(mRootSignatures["dxr_global"].Get());

	auto& passCB = mCurrFrameResource->PassCB;
	mCommandList->SetComputeRootConstantBufferView(static_cast<UINT>(EGlobalRootSignatureParams::EPassCB), passCB.Resource()->GetGPUVirtualAddress());
	mCommandList->SetComputeRootShaderResourceView(static_cast<UINT>(EGlobalRootSignatureParams::EAccelerationStructure), mTLAS->Result->GetGPUVirtualAddress());

	const auto pDescHeap = mDescriptorHeap.Get();
	ID3D12DescriptorHeap* descriptorHeaps[] = { pDescHeap };
	mCommandList->SetDescriptorHeaps(_countof(descriptorHeaps), descriptorHeaps);

	UINT cbvSrvUavDescriptorSize = GetCbvSrvUavDescriptorSize();

	mCommandList->SetComputeRootDescriptorTable(
		static_cast<UINT>(EGlobalRootSignatureParams::EOutput),
		D3D12Util::GetGpuHandle(pDescHeap, static_cast<INT>(EDescriptors::EU_Output) + mCurrFrameResourceIndex, cbvSrvUavDescriptorSize)
	);
	mCommandList->SetComputeRootDescriptorTable(
		static_cast<UINT>(EGlobalRootSignatureParams::EGeometryBuffers),
		D3D12Util::GetGpuHandle(pDescHeap, static_cast<INT>(EDescriptors::ES_Vertices), cbvSrvUavDescriptorSize)
	);

	D3D12_DISPATCH_RAYS_DESC dispatchDesc = {};
	const auto& hitGroup = mShaderTables["hitGroup"];
	const auto& miss = mShaderTables["miss"];
	const auto& rayGen = mShaderTables["rayGen"];
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

	const auto pOutput = mDXROutputs[mCurrFrameResourceIndex].Get();
	mCommandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			pOutput,
			D3D12_RESOURCE_STATE_COMMON,
			D3D12_RESOURCE_STATE_COPY_SOURCE)
	);

	mCommandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			CurrentBackBuffer(),
			D3D12_RESOURCE_STATE_PRESENT,
			D3D12_RESOURCE_STATE_COPY_DEST)
	);

	mCommandList->CopyResource(CurrentBackBuffer(), pOutput);

	mCommandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			CurrentBackBuffer(),
			D3D12_RESOURCE_STATE_COPY_DEST,
			D3D12_RESOURCE_STATE_PRESENT)
	);

	mCommandList->ResourceBarrier(
		1,
		&CD3DX12_RESOURCE_BARRIER::Transition(
			pOutput,
			D3D12_RESOURCE_STATE_COPY_SOURCE,
			D3D12_RESOURCE_STATE_COMMON)
	);

	CheckHResult(mCommandList->Close());
	ID3D12CommandList* cmdsLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdsLists), cmdsLists);

	return true;
}

bool Renderer::DrawDebugLayer() {
	CheckHResult(mCommandList->Reset(mCurrFrameResource->CmdListAlloc.Get(), mPSOs["gizmo"].Get()));

	mCommandList->SetGraphicsRootSignature(mRootSignatures["debug"].Get());

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

	const auto& debugPassCB = mCurrFrameResource->DebugPassCB;
	mCommandList->SetGraphicsRootConstantBufferView(static_cast<UINT>(EDebugRootSignatureParams::EPassCB), debugPassCB.Resource()->GetGPUVirtualAddress());

	mCommandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_LINELIST);
	mCommandList->DrawInstanced(2, 3, 0, 0);

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

	ID3D12DescriptorHeap* descriptorHeaps[] = { mDescriptorHeap.Get() };
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

bool Renderer::DrawRenderItems(const std::vector<RenderItem*>& ritems) {
	UINT objCBByteSize = D3D12Util::CalcConstantBufferByteSize(sizeof(ObjectConstants));
	UINT matCBByteSize = D3D12Util::CalcConstantBufferByteSize(sizeof(MaterialConstants));

	auto& objectCB = mCurrFrameResource->ObjectCB;
	auto& matCB = mCurrFrameResource->MaterialCB;

	// For each render item...
	for (size_t i = 0; i < ritems.size(); ++i) {
		auto& ri = ritems[i];

		mCommandList->IASetVertexBuffers(0, 1, &ri->Geo->VertexBufferView());
		mCommandList->IASetIndexBuffer(&ri->Geo->IndexBufferView());
		mCommandList->IASetPrimitiveTopology(ri->PrimitiveType);

		D3D12_GPU_VIRTUAL_ADDRESS objCBAddress = objectCB.Resource()->GetGPUVirtualAddress() + ri->ObjCBIndex * objCBByteSize;
		D3D12_GPU_VIRTUAL_ADDRESS matCBAddress = matCB.Resource()->GetGPUVirtualAddress() + ri->Mat->MatCBIndex * matCBByteSize;

		mCommandList->SetGraphicsRootConstantBufferView(static_cast<UINT>(ERasterRootSignatureParams::EObjectCB), objCBAddress);
		mCommandList->SetGraphicsRootConstantBufferView(static_cast<UINT>(ERasterRootSignatureParams::EMatCB), matCBAddress);

		mCommandList->DrawIndexedInstanced(ri->IndexCount, 1, ri->StartIndexLocation, ri->BaseVertexLocation, 0);
	}

	return true;
}