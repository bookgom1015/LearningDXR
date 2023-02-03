#include "Application.h"
#include "Logger.h"
#include "Camera.h"

#include <windowsx.h>

#include <imgui.h>
#include <backends/imgui_impl_win32.h>

// Forward declare message handler from imgui_impl_win32.cpp
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE prevInstance, PSTR cmdLine, int showCmd) {
	try {
		Application app;

		if (!app.Initialize()) {
			//ShowRemovedReason(app.GetRenderer());
			WLogln(L"Error Occured");
			return -1;
		}

		auto result = app.RunLoop();
		if (FAILED(result)) {
			//ShowRemovedReason(app.GetRenderer());
			WLogln(L"Error Occured");
			return static_cast<int>(result);
		}

		app.CleanUp();

		WLogln(L"The game has been succeesfully cleaned up");

		return static_cast<int>(result);
	}
	catch (const std::exception& e) {
		std::wstringstream wsstream;
		wsstream << e.what();
		WErrln(wsstream.str());
		WLogln(L"Catched");
		return -1;
	}
}

namespace {
	LRESULT CALLBACK MainWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		// Forward hwnd on because we can get messages (e.g., WM_CREATE)
		// before CreateWindow returns, and thus before mhMainWnd is valid
		return Application::GetApp()->MsgProc(hwnd, msg, wParam, lParam);
	}

	const UINT InitClientWidth = 800;
	const UINT InitClientHeight = 600;

	const LPCWSTR RasterCaption = L"DXR Application - Rasterization";
	const LPCWSTR RaytraceCaption = L"DXR Application - Raytracing";
}

Application* Application::sApp = nullptr;

Application::Application() {
	sApp = this;
	mhInst = nullptr;
	mhMainWnd = nullptr;
	bIsCleanedUp = false;
	bAppPaused = false;
	bMinimized = false;
	bMaximized = false;
	bResizing = false;
	bFullscreenState = false;
	bMouseLeftButtonDowned = false;

	mCamera = std::make_unique<Camera>();
	mRenderer = std::make_unique<Renderer>();

	mGameState = EGameStates::EGS_Play;
}

Application::~Application() {
	if (!bIsCleanedUp)
		CleanUp();
}

bool Application::Initialize() {
	CheckIsValid(InitMainWindow());
	CheckIsValid(mCamera->Initialize(InitClientWidth, InitClientHeight, 0.25f * DirectX::XM_PI))
	CheckIsValid(mRenderer->Initialize(mhMainWnd, InitClientWidth, InitClientHeight));

	mRenderer->SetCamera(mCamera.get());

	return true;
}

HRESULT Application::RunLoop() {
	MSG msg = { 0 };

	mTimer.Reset();

	while (msg.message != WM_QUIT) {
		// If there are Window messages then process them
		if (PeekMessage(&msg, 0, 0, 0, PM_REMOVE)) {
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
		// Otherwise, do animation/game stuff
		else {
			mTimer.Tick();

			if (!bAppPaused) {
				BreakIfInvalid(Update(mTimer));
				BreakIfInvalid(Draw());
			}
			else {
				Sleep(100);
			}
		}
	}

	return static_cast<HRESULT>(msg.wParam);
}

void Application::CleanUp() {
	if (mRenderer != nullptr)
		mRenderer->CleanUp();

	bIsCleanedUp = true;
}

bool Application::InitMainWindow() {
	WNDCLASS wc;
	wc.style = CS_HREDRAW | CS_VREDRAW;
	wc.lpfnWndProc = MainWndProc;
	wc.cbClsExtra = 0;
	wc.cbWndExtra = 0;
	wc.hInstance = mhInst;
	wc.hIcon = LoadIcon(0, IDI_APPLICATION);
	wc.hCursor = LoadCursor(0, IDC_ARROW);
	wc.hbrBackground = static_cast<HBRUSH>(GetStockObject(NULL_BRUSH));
	wc.lpszMenuName = 0;
	wc.lpszClassName = L"MainWnd";
	if (!RegisterClass(&wc))  ReturnFalse(L"Failed to register window class");

	// Compute window rectangle dimensions based on requested client area dimensions.
	RECT R = { 0, 0, static_cast<LONG>(InitClientWidth), static_cast<LONG>(InitClientHeight) };
	AdjustWindowRect(&R, WS_OVERLAPPEDWINDOW, false);
	int width = R.right - R.left;
	int height = R.bottom - R.top;

	mPrimaryMonitorWidth = GetSystemMetrics(SM_CXSCREEN);
	mPrimaryMonitorHeight = GetSystemMetrics(SM_CYSCREEN);

	int clientPosX = static_cast<int>((mPrimaryMonitorWidth - InitClientWidth) * 0.5f);
	int clientPosY = static_cast<int>((mPrimaryMonitorHeight - InitClientHeight) * 0.5f);

	mhMainWnd = CreateWindow(
		L"MainWnd", RasterCaption,
		WS_OVERLAPPEDWINDOW, 
		clientPosX, clientPosY, 
		width, height, 
		0, 0, mhInst, 0);
	if (!mhMainWnd) ReturnFalse(L"Failed to create window");

	ShowWindow(mhMainWnd, SW_SHOW);
	UpdateWindow(mhMainWnd);

	return true;
}

void Application::OnResize(UINT width, UINT height) {
	mCamera->OnResize(width, height);
	mRenderer->OnResize(width, height);
}

bool Application::Update(const GameTimer& gt) {
	CheckIsValid(UpdateGame(gt));
	CheckIsValid(mRenderer->Update(gt));

	return true;
}

bool Application::Draw() {
	CheckIsValid(mRenderer->Draw());

	return true;
}

bool Application::UpdateGame(const GameTimer& gt) {
	if (mGameState == EGS_Play) CheckIsValid(mCamera->Update(gt));

	return true;
}

void Application::OnMouseDown(WPARAM state, int x, int y) {
	bMouseLeftButtonDowned = true;
	mPrevMousePosX = x;
	mPrevMousePosY = y;
}

void Application::OnMouseUp(WPARAM state, int x, int y) {
	bMouseLeftButtonDowned = false;
}

void Application::OnMouseMove(WPARAM state, int x, int y) {
	static const float Speed = 0.01f;

	if (bMouseLeftButtonDowned) {
		int deltaX = mPrevMousePosX - x;
		int deltaY = y - mPrevMousePosY;

		mCamera->AddPhi(deltaX * Speed);
		mCamera->AddTheta(deltaY * Speed);

		mPrevMousePosX = x;
		mPrevMousePosY = y;
	}
}

void Application::OnScroll(bool up) {
	static const float Speed = 0.1f;
	static const float InvSpeed = -Speed;

	mCamera->AddRadius(up ? InvSpeed : Speed);
}

void Application::OnKeyboardInput(UINT msg, WPARAM wParam, LPARAM lParam) {
	if (msg == WM_KEYDOWN) {
		switch (wParam) {
		case VK_SPACE:
		{
			bool state = mRenderer->GetRenderType();
			mRenderer->SetRenderType(!state);
			SetWindowText(mhMainWnd, state ? RasterCaption : RaytraceCaption);
		}
			break;
		case VK_TAB:
			if (mGameState == EGameStates::EGS_Play) {
				mGameState = EGameStates::EGS_UI;
				mRenderer->DisplayImGui(true);
			}
			else {
				mGameState = EGameStates::EGS_Play;
				mRenderer->DisplayImGui(false);
			}
			break;
		case VK_RIGHT:
			mCamera->AddPhi(0.05f);
			break;
		case VK_LEFT:
			mCamera->AddPhi(-0.05f);
			break;
		case VK_UP:
			mCamera->AddTheta(-0.05f);
			break;
		case VK_DOWN:
			mCamera->AddTheta(0.05f);
			break;
		}
	}
	else {

	}
}

Application* Application::GetApp() {
	return sApp;
}

Renderer* Application::GetRenderer() {
	return mRenderer.get();
}

LRESULT Application::MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
	static UINT width = InitClientWidth;
	static UINT height = InitClientHeight;

	if ((mGameState == EGameStates::EGS_UI) && ImGui_ImplWin32_WndProcHandler(hwnd, msg, wParam, lParam)) return 0;

	switch (msg) {
	// WM_ACTIVATE is sent when the window is activated or deactivated.  
	// We pause the game when the window is deactivated and unpause it 
	// when it becomes active.
	case WM_ACTIVATE:
		if (LOWORD(wParam) == WA_INACTIVE) {
			mTimer.Stop();
			bAppPaused = true;
		}
		else {
			mTimer.Start();
			bAppPaused = false;
		}
		return 0;

	// WM_SIZE is sent when the user resizes the window.  
	case WM_SIZE:
	{
		// Save the new client area dimensions.
		width = LOWORD(lParam);
		height = HIWORD(lParam);
		if (mRenderer->Initialized()) {
			if (wParam == SIZE_MINIMIZED) {
				bAppPaused = true;
				bMinimized = true;
				bMaximized = false;
			}
			else if (wParam == SIZE_MAXIMIZED) {
				bAppPaused = false;
				bMinimized = false;
				bMaximized = true;
				OnResize(width, height);
			}
			else if (wParam == SIZE_RESTORED) {
				// Restoring from minimized state?
				if (bMinimized) {
					bAppPaused = false;
					bMinimized = false;
					OnResize(width, height);
				}

				// Restoring from maximized state?
				else if (bMaximized) {
					bAppPaused = false;
					bMaximized = false;
					OnResize(width, height);
				}
				// If user is dragging the resize bars, we do not resize 
				// the buffers here because as the user continuously 
				// drags the resize bars, a stream of WM_SIZE messages are
				// sent to the window, and it would be pointless (and slow)
				// to resize for each WM_SIZE message received from dragging
				// the resize bars.  So instead, we reset after the user is 
				// done resizing the window and releases the resize bars, which 
				// sends a WM_EXITSIZEMOVE message.
				else if (bResizing) {
				
				}
				// API call such as SetWindowPos or mSwapChain->SetFullscreenState.
				else { 
					OnResize(width, height);
				}
			}
		}
		return 0;
	}

	// WM_EXITSIZEMOVE is sent when the user grabs the resize bars.
	case WM_ENTERSIZEMOVE:
		mTimer.Stop();
		bAppPaused = true;
		bResizing = true;
		return 0;

	// WM_EXITSIZEMOVE is sent when the user releases the resize bars.
	// Here we reset everything based on the new window dimensions.
	case WM_EXITSIZEMOVE:
		mTimer.Start();
		bAppPaused = false;
		bResizing = false;
		OnResize(width, height);
		return 0;

	// WM_DESTROY is sent when the window is being destroyed.
	case WM_DESTROY:
		PostQuitMessage(0);
		return 0;

	// The WM_MENUCHAR message is sent when a menu is active and the user presses 
	// a key that does not correspond to any mnemonic or accelerator key. 
	case WM_MENUCHAR:
		// Don't beep when we alt-enter.
		return MAKELRESULT(0, MNC_CLOSE);

	// Catch this message so to prevent the window from becoming too small.
	case WM_GETMINMAXINFO:
		return 0;

	case WM_LBUTTONDOWN:
	case WM_MBUTTONDOWN:
	case WM_RBUTTONDOWN:
		if (mGameState == EGameStates::EGS_Play) OnMouseDown(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;

	case WM_LBUTTONUP:
	case WM_MBUTTONUP:
	case WM_RBUTTONUP:
		if (mGameState == EGameStates::EGS_Play) OnMouseUp(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;

	case WM_MOUSEMOVE:
		OnMouseMove(wParam, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam));
		return 0;

	case WM_MOUSEWHEEL:
	{
		const auto wheel = static_cast<SHORT>(HIWORD(wParam));
		OnScroll(wheel > 0);
		return 0;
	}

	case WM_KEYUP:
		OnKeyboardInput(WM_KEYUP, wParam, lParam);
		return 0;

	case WM_KEYDOWN:
		OnKeyboardInput(WM_KEYDOWN, wParam, lParam);
		return 0;
	}

	return DefWindowProc(hwnd, msg, wParam, lParam);
}