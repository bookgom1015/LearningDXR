#pragma once

#pragma comment(lib, "dxguid.lib")
#pragma comment(lib, "dxgi.lib")
#pragma comment(lib, "d3d12.lib")
#pragma comment(lib, "d3dcompiler.lib")
#pragma comment(lib, "dxcompiler.lib")

#include "d3dx12.h"

#include <DirectXColors.h>
#include <DirectXMath.h>
#include <DirectXPackedVector.h>
#include <DirectXCollision.h>

#include <dxgi1_6.h>
#include <dxc/dxcapi.h>
#include <dxc/Support/dxcapi.use.h>

#include <algorithm>
#include <map>
#include <string>
#include <vector>
#include <wrl.h>

#define SKIP_LOG_OUTPUTS

class LowRenderer {
private:
	using Adapters = std::multimap<size_t, IDXGIAdapter*>;

protected:
	LowRenderer();

private:
	LowRenderer(const LowRenderer& ref) = delete;
	LowRenderer(LowRenderer&& rval) = delete;
	LowRenderer& operator=(const LowRenderer& ref) = delete;
	LowRenderer& operator=(LowRenderer&& rval) = delete;

public:
	virtual ~LowRenderer();

public:
	virtual bool Initialize(HWND hMainWnd, UINT width, UINT height);
	virtual void CleanUp();

	virtual bool OnResize(UINT width, UINT height);

	bool FlushCommandQueue();

	ID3D12Resource* BackBuffer(int index) const;
	ID3D12Resource* CurrentBackBuffer() const;
	D3D12_CPU_DESCRIPTOR_HANDLE CurrentBackBufferView() const;
	D3D12_CPU_DESCRIPTOR_HANDLE DepthStencilView() const;

	HRESULT GetDeviceRemovedReason() const;

protected:
	virtual bool CreateRtvAndDsvDescriptorHeaps();

	__forceinline constexpr float GetAspectRatio() const;
	__forceinline constexpr UINT GetClientWidth() const;
	__forceinline constexpr UINT GetClientHeight() const;

	__forceinline constexpr UINT GetRtvDescriptorSize() const;
	__forceinline constexpr UINT GetDsvDescriptorSize() const;
	__forceinline constexpr UINT GetCbvSrvUavDescriptorSize() const;

	UINT64 IncCurrentFence();
	__forceinline constexpr UINT64 GetCurrentFence() const;

	void NextBackBuffer();

private:
	bool InitDirect3D();

	bool OnResize();

	void SortAdapters(Adapters& map);

	bool CreateDebugObjects();
	bool CreateCommandObjects();
	bool CreateSwapChain();

private:
	bool bIsCleanedUp = false;

	UINT mRefreshRate;

	UINT mClientWidth;
	UINT mClientHeight;

	UINT mRtvDescriptorSize;
	UINT mDsvDescriptorSize;
	UINT mCbvSrvUavDescriptorSize;

	UINT64 mCurrentFence;

	UINT mDXGIFactoryFlags;

	int mCurrBackBuffer;

protected:
	static const int SwapChainBufferCount = 2;

	HWND mhMainWnd;

	static const D3D_DRIVER_TYPE D3DDriverType = D3D_DRIVER_TYPE_HARDWARE;
	static const DXGI_FORMAT BackBufferFormat = DXGI_FORMAT_R8G8B8A8_UNORM;
	static const DXGI_FORMAT DepthStencilFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	Microsoft::WRL::ComPtr<ID3D12InfoQueue1> mInfoQueue;
	
	Microsoft::WRL::ComPtr<IDXGIFactory4> mdxgiFactory;
	Microsoft::WRL::ComPtr<ID3D12Device5> md3dDevice;

	Microsoft::WRL::ComPtr<IDXGISwapChain> mSwapChain;
	Microsoft::WRL::ComPtr<ID3D12Resource> mSwapChainBuffer[SwapChainBufferCount];
	Microsoft::WRL::ComPtr<ID3D12Resource> mDepthStencilBuffer;

	Microsoft::WRL::ComPtr<ID3D12CommandQueue> mCommandQueue;
	Microsoft::WRL::ComPtr<ID3D12CommandAllocator> mDirectCmdListAlloc;		
	Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList4> mCommandList;

	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mRtvHeap;
	Microsoft::WRL::ComPtr<ID3D12DescriptorHeap> mDsvHeap;

	Microsoft::WRL::ComPtr<ID3D12Fence> mFence;

	Microsoft::WRL::ComPtr<ID3D12Debug> mDebugController;
	DWORD mCallbakCookie;

	D3D12_VIEWPORT mScreenViewport;
	D3D12_RECT mScissorRect;
};

#include "LowRenderer.inl"