#pragma once

#include "Renderer.h"

class Camera;

class Application {
public:
	enum EGameStates {
		EGS_Play,
		EGS_UI
	};

public:
	Application();
	virtual ~Application();

private:
	Application(const Application& ref) = delete;
	Application(Application&& rval) = delete;
	Application& operator=(const Application& ref) = delete;
	Application& operator=(Application&& rval) = delete;

public:
	bool Initialize();
	HRESULT RunLoop();
	void CleanUp();

	static Application* GetApp();
	Renderer* GetRenderer();

	LRESULT MsgProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

private:
	bool InitMainWindow();
	void OnResize(UINT width, UINT height);

	bool Update(const GameTimer& gt);
	bool Draw();

	bool UpdateGame(const GameTimer& gt);

	void OnMouseDown(WPARAM state, int x, int y);
	void OnMouseUp(WPARAM state, int x, int y);
	void OnMouseMove(WPARAM state, int x, int y);
	void OnScroll(bool up);

	void OnKeyboardInput(UINT msg, WPARAM wParam, LPARAM lParam);

private:
	static Application* sApp;

	bool bIsCleanedUp;
	
	HINSTANCE mhInst;			// Application instance handle
	HWND mhMainWnd;				// Main window handle
	bool bAppPaused;			// Is the application paused?
	bool bMinimized;			// Is the application minimized?
	bool bMaximized;			// Is the application maximized?
	bool bResizing;				// Are the resize bars being dragged?
	bool bFullscreenState;		// Fullscreen enabled
	bool bMouseLeftButtonDowned;
	
	int mPrevMousePosX;
	int mPrevMousePosY;

	std::unique_ptr<Renderer> mRenderer;
	std::unique_ptr<Camera> mCamera;

	GameTimer mTimer;

	UINT mPrimaryMonitorWidth;
	UINT mPrimaryMonitorHeight;

	EGameStates mGameState;
};