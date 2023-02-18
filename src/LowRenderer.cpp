#include "LowRenderer.h"
#include "Logger.h"
#include "RenderMacros.h"

using namespace Microsoft::WRL;

namespace {
	void LogAdapters(IDXGIFactory4* pFactory) {
		UINT i = 0;
		IDXGIAdapter* adapter = nullptr;
		std::vector<IDXGIAdapter*> adapterList;
		while (pFactory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND) {
			DXGI_ADAPTER_DESC desc;
			adapter->GetDesc(&desc);
			
			std::wstring text = L"***Adapter: ";
			text += desc.Description + L'\n';
			WLogln(text.c_str());

			adapterList.push_back(adapter);
			++i;
		}

#ifndef SKIP_LOG_OUTPUTS
		for (size_t i = 0; i < adapterList.size(); ++i) {
			LogAdapterOutputs(adapterList[i]);
			ReleaseCom(adapterList[i]);
		}
#endif
	}

	void LogOutputDisplayModes(IDXGIOutput* pOutput, DXGI_FORMAT format) {
		UINT count = 0;
		UINT flags = 0;

		// Call with nullptr to get list count.
		pOutput->GetDisplayModeList(format, flags, &count, nullptr);

		std::vector<DXGI_MODE_DESC> modelList(count);
		pOutput->GetDisplayModeList(format, flags, &count, &modelList[0]);

		for (auto& x : modelList) {
			UINT n = x.RefreshRate.Numerator;
			UINT d = x.RefreshRate.Denominator;
			std::wstring text =
				L"        Width = " + std::to_wstring(x.Width) + L'\n' +
				L"        Height = " + std::to_wstring(x.Height) + L'\n' +
				L"        Refresh = " + std::to_wstring(n) + L'/' + std::to_wstring(d) + L'\n';
			WLogln(text);
		}
	}

	void LogAdapterOutputs(IDXGIAdapter* pAapter, DXGI_FORMAT format) {
		UINT i = 0;
		IDXGIOutput* output = nullptr;
		while (pAapter->EnumOutputs(i, &output) != DXGI_ERROR_NOT_FOUND) {
			DXGI_OUTPUT_DESC desc;
			output->GetDesc(&desc);

			std::wstring text = L"***Output: ";
			text += desc.DeviceName + L'\n';
			WLogln(text);

			LogOutputDisplayModes(output, format);

			ReleaseCom(output);
			++i;
		}
	}

	void D3D12MessageCallback(
			D3D12_MESSAGE_CATEGORY category,
			D3D12_MESSAGE_SEVERITY severity,
			D3D12_MESSAGE_ID id,
			LPCSTR pDescription,
			void *pContext) {
		std::string str(pDescription);

		std::string sevStr;
		switch (severity) {
		case D3D12_MESSAGE_SEVERITY_CORRUPTION:
			sevStr = "Corruption";
			break;
		case D3D12_MESSAGE_SEVERITY_ERROR:
			sevStr = "Error";
			break;
		case D3D12_MESSAGE_SEVERITY_WARNING:
			sevStr = "Warning";
			break;
		case D3D12_MESSAGE_SEVERITY_INFO:
			return;
			sevStr = "Info";
			break;
		case D3D12_MESSAGE_SEVERITY_MESSAGE:
			return;
			sevStr = "Message";
			break;
		}

		std::stringstream sstream;
		sstream << '[' << sevStr << "] " << pDescription;

		Logln(sstream.str());
	}
}

LowRenderer::LowRenderer() {
	mRefreshRate = 0;
	mClientWidth = 0;
	mClientHeight = 0;
	mRtvDescriptorSize = 0;
	mDsvDescriptorSize = 0;
	mCbvSrvUavDescriptorSize = 0;
	mCurrentFence = 0;
	mDXGIFactoryFlags = 0;
	mCurrBackBuffer = 0;
	mCallbakCookie = 0x01010101;
}

LowRenderer::~LowRenderer() {
	if (!bIsCleanedUp)
		CleanUp();
}

bool LowRenderer::Initialize(HWND hMainWnd, UINT width, UINT height) {
	mhMainWnd = hMainWnd;
	mClientWidth = width;
	mClientHeight = height;

	CheckIsValid(InitDirect3D());
	CheckIsValid(OnResize());

	return true;
}

void LowRenderer::CleanUp() {
	if (mInfoQueue != nullptr) {
		if (FAILED(mInfoQueue->UnregisterMessageCallback(mCallbakCookie))) {
			WLogln(L"Failed to unregister message call-back");
		}
	}

	if (md3dDevice != nullptr) {
		if (!FlushCommandQueue())
			WLogln(L"Failed to flush command queue during cleaning up");
	}

	bIsCleanedUp = true;
}

bool LowRenderer::OnResize(UINT width, UINT height) {
	mClientWidth = width;
	mClientHeight = height;

	CheckIsValid(OnResize());

	return true;
}

bool LowRenderer::FlushCommandQueue() {
	// Advance the fence value to mark commands up to this fence point.
	++mCurrentFence;

	// Add an instruction to the command queue to set a new fence point.
	// Because we are on the GPU timeline, the new fence point won't be set until the GPU finishes
	// processing all the commands prior to this Signal().
	CheckHResult(mCommandQueue->Signal(mFence.Get(), mCurrentFence));

	// Wait until the GPU has compledted commands up to this fence point.
	if (mFence->GetCompletedValue() < mCurrentFence) {
		HANDLE eventHandle = CreateEventEx(nullptr, false, false, EVENT_ALL_ACCESS);

		// Fire event when GPU hits current fence.
		CheckHResult(mFence->SetEventOnCompletion(mCurrentFence, eventHandle));

		// Wait until the GPU hits current fence.
		WaitForSingleObject(eventHandle, INFINITE);
		CloseHandle(eventHandle);
	}

	return true;
}

ID3D12Resource* LowRenderer::BackBuffer(int index) const {
	return mSwapChainBuffer[index].Get();
}

ID3D12Resource* LowRenderer::CurrentBackBuffer() const {
	return mSwapChainBuffer[mCurrBackBuffer].Get();
}

D3D12_CPU_DESCRIPTOR_HANDLE LowRenderer::CurrentBackBufferView() const {
	return CD3DX12_CPU_DESCRIPTOR_HANDLE(mRtvHeap->GetCPUDescriptorHandleForHeapStart(), mCurrBackBuffer, mRtvDescriptorSize);
}

D3D12_CPU_DESCRIPTOR_HANDLE LowRenderer::DepthStencilView() const {
	return mDsvHeap->GetCPUDescriptorHandleForHeapStart();
}

HRESULT LowRenderer::GetDeviceRemovedReason() const {
	return md3dDevice->GetDeviceRemovedReason();
}

bool LowRenderer::CreateRtvAndDsvDescriptorHeaps() {
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

UINT64 LowRenderer::IncCurrentFence() {
	return ++mCurrentFence;
}

void LowRenderer::NextBackBuffer() {
	mCurrBackBuffer = (mCurrBackBuffer + 1) % SwapChainBufferCount;
}

bool LowRenderer::InitDirect3D() {
#ifdef _DEBUG
	if (SUCCEEDED(D3D12GetDebugInterface(IID_PPV_ARGS(&mDebugController)))) {
		mDebugController->EnableDebugLayer();
		mDXGIFactoryFlags |= DXGI_CREATE_FACTORY_DEBUG;
	}
#endif

	CheckHResult(CreateDXGIFactory2(mDXGIFactoryFlags, IID_PPV_ARGS(&mdxgiFactory)));

	Adapters adapters;
	SortAdapters(adapters);
	
	bool found = false;
	for (auto begin = adapters.rbegin(), end = adapters.rend(); begin != end; ++begin) {
		auto adapter = begin->second;

		// Try to create hardware device.
		HRESULT hr = D3D12CreateDevice(
			adapter,
			D3D_FEATURE_LEVEL_12_1,
			__uuidof(ID3D12Device5),
			static_cast<void**>(&md3dDevice)
		);

		if (SUCCEEDED(hr)) {
			found = true;
#ifdef _DEBUG
			DXGI_ADAPTER_DESC desc;
			adapter->GetDesc(&desc);
			WLogln(desc.Description, L" is selected");
#endif
			break;
		}
	}

	if (!found) ReturnFalse(L"There are no adapters that support the required features");
	
	// Checks that device supports ray-tracing.
	D3D12_FEATURE_DATA_D3D12_OPTIONS5 ops = {};
	HRESULT featureSupport = md3dDevice->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5, &ops, sizeof(ops));

	if (FAILED(featureSupport) || ops.RaytracingTier < D3D12_RAYTRACING_TIER_1_0)
		ReturnFalse(L"Device or driver does not support ray-tracing");

	CheckHResult(md3dDevice->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&mFence)));

	mRtvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
	mDsvDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_DSV);
	mCbvSrvUavDescriptorSize = md3dDevice->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);

	CheckIsValid(CreateDebugObjects());
	CheckIsValid(CreateCommandObjects());
	CheckIsValid(CreateSwapChain());
	CheckIsValid(CreateRtvAndDsvDescriptorHeaps());

	return true;
}

bool LowRenderer::OnResize() {
	if (!md3dDevice) ReturnFalse(L"md3dDevice is invalid");
	if (!mSwapChain) ReturnFalse(L"mSwapChain is invalid");
	if (!mDirectCmdListAlloc) ReturnFalse(L"mDirectCmdListAlloc is invalid");

	// Flush before changing any resources.
	CheckIsValid(FlushCommandQueue());

	CheckHResult(mCommandList->Reset(mDirectCmdListAlloc.Get(), nullptr));

	// Resize the previous resources we will be creating.
	for (int i = 0; i < SwapChainBufferCount; ++i)
		mSwapChainBuffer[i].Reset();

	// Resize the swap chain.
	CheckHResult(mSwapChain->ResizeBuffers(
		SwapChainBufferCount,
		mClientWidth,
		mClientHeight,
		BackBufferFormat,
		DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH)
	);

	mCurrBackBuffer = 0;

	CD3DX12_CPU_DESCRIPTOR_HANDLE rtvHeapHandle(mRtvHeap->GetCPUDescriptorHandleForHeapStart());
	for (UINT i = 0; i < SwapChainBufferCount; ++i) {
		CheckHResult(mSwapChain->GetBuffer(i, IID_PPV_ARGS(&mSwapChainBuffer[i])));
		mSwapChainBuffer[i]->SetName(L"BackBuffer");

		md3dDevice->CreateRenderTargetView(mSwapChainBuffer[i].Get(), nullptr, rtvHeapHandle);
		rtvHeapHandle.Offset(1, mRtvDescriptorSize);
	}

	// Create the depth/stencil buffer and view.
	D3D12_RESOURCE_DESC depthStencilDesc;
	depthStencilDesc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
	depthStencilDesc.Alignment = 0;
	depthStencilDesc.Width = mClientWidth;
	depthStencilDesc.Height = mClientHeight;
	depthStencilDesc.DepthOrArraySize = 1;
	depthStencilDesc.MipLevels = 1;
	depthStencilDesc.Format = DepthStencilFormat;
	depthStencilDesc.SampleDesc.Count = 1;
	depthStencilDesc.SampleDesc.Quality = 0;
	depthStencilDesc.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
	depthStencilDesc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;

	D3D12_CLEAR_VALUE optClear;
	optClear.Format = DepthStencilFormat;
	optClear.DepthStencil.Depth = 1.0f;
	optClear.DepthStencil.Stencil = 0;

	CheckHResult(md3dDevice->CreateCommittedResource(
		&CD3DX12_HEAP_PROPERTIES(D3D12_HEAP_TYPE_DEFAULT),
		D3D12_HEAP_FLAG_NONE,
		&depthStencilDesc,
		D3D12_RESOURCE_STATE_COMMON,
		&optClear,
		IID_PPV_ARGS(mDepthStencilBuffer.GetAddressOf())
	));
	mDepthStencilBuffer->SetName(L"DepthStencilBuffer");

	// Create descriptor to mip level 0 of entire resource using the format of the resource.
	md3dDevice->CreateDepthStencilView(mDepthStencilBuffer.Get(), nullptr, DepthStencilView());

	// Transition the resource from its initial state to be used as a depth buffer.
	mCommandList->ResourceBarrier(1, &CD3DX12_RESOURCE_BARRIER::Transition(mDepthStencilBuffer.Get(),
		D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_DEPTH_READ));

	// Execute the resize commands.
	CheckHResult(mCommandList->Close());

	ID3D12CommandList* cmdLists[] = { mCommandList.Get() };
	mCommandQueue->ExecuteCommandLists(_countof(cmdLists), cmdLists);

	// Wait until resize is complete.
	CheckIsValid(FlushCommandQueue());

	// Update the viewport
	mScreenViewport.TopLeftX = 0;
	mScreenViewport.TopLeftY = 0;
	mScreenViewport.Width = static_cast<float>(mClientWidth);
	mScreenViewport.Height = static_cast<float>(mClientHeight);
	mScreenViewport.MinDepth = 0.0f;
	mScreenViewport.MaxDepth = 1.0f;

	mScissorRect = { 0, 0, static_cast<LONG>(mClientWidth), static_cast<LONG>(mClientHeight) };

	return true;
}

void LowRenderer::SortAdapters(Adapters& map) {
	UINT i = 0;
	IDXGIAdapter* adapter = nullptr;

#if _DEBUG
	WLogln(L"Adapters:");
	std::wstringstream wsstream;
#endif

	while (mdxgiFactory->EnumAdapters(i, &adapter) != DXGI_ERROR_NOT_FOUND) {
		DXGI_ADAPTER_DESC desc;
		adapter->GetDesc(&desc);

#if _DEBUG
		wsstream << L"\t " << desc.Description << std::endl;
#endif

		size_t score = desc.DedicatedSystemMemory + desc.DedicatedVideoMemory + desc.SharedSystemMemory;

		map.insert(std::make_pair(score, adapter));
		++i;
	}

#if _DEBUG
	WLog(wsstream.str());
#endif
}

bool LowRenderer::CreateDebugObjects() {
	CheckHResult(md3dDevice->QueryInterface(IID_PPV_ARGS(&mInfoQueue)));

	CheckHResult(mInfoQueue->RegisterMessageCallback(D3D12MessageCallback, D3D12_MESSAGE_CALLBACK_IGNORE_FILTERS, NULL, &mCallbakCookie));

	return true;
}

bool LowRenderer::CreateCommandObjects() {
	D3D12_COMMAND_QUEUE_DESC queueDesc = {};
	queueDesc.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
	queueDesc.Flags = D3D12_COMMAND_QUEUE_FLAG_NONE;
	CheckHResult(md3dDevice->CreateCommandQueue(&queueDesc, IID_PPV_ARGS(&mCommandQueue)));

	CheckHResult(md3dDevice->CreateCommandAllocator(
		D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(mDirectCmdListAlloc.GetAddressOf()))
	);

	CheckHResult(md3dDevice->CreateCommandList(
		0,
		D3D12_COMMAND_LIST_TYPE_DIRECT,
		mDirectCmdListAlloc.Get(),	// Associated command allocator
		nullptr,					// Initial PipelineStateObject
		IID_PPV_ARGS(mCommandList.GetAddressOf())
	));
	mCommandList->SetName(L"SingleCommandList");

	// Start off in a closed state.  This is because the first time we refer 
	// to the command list we will Reset it, and it needs to be closed before
	// calling Reset.
	mCommandList->Close();

	return true;
}

bool LowRenderer::CreateSwapChain() {
	// Release the previous swapchain we will be recreating.
	mSwapChain.Reset();

	DXGI_SWAP_CHAIN_DESC sd;
	sd.BufferDesc.Width = mClientWidth;
	sd.BufferDesc.Height = mClientHeight;
	sd.BufferDesc.RefreshRate.Numerator = mRefreshRate;
	sd.BufferDesc.RefreshRate.Denominator = 1;
	sd.BufferDesc.Format = BackBufferFormat;
	sd.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	sd.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	sd.SampleDesc.Count = 1;
	sd.SampleDesc.Quality = 0;
	sd.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	sd.BufferCount = SwapChainBufferCount;
	sd.OutputWindow = mhMainWnd;
	sd.Windowed = true;
	sd.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	sd.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;

	// Note: Swap chain uses queue to perfrom flush.
	CheckHResult(mdxgiFactory->CreateSwapChain(mCommandQueue.Get(), &sd, mSwapChain.GetAddressOf()));

	return true;
}