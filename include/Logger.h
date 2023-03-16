#pragma once

#include <mutex>
#include <sstream>
#include <string>
#include <vector>
#include <Windows.h>

#ifndef FileLineStr
#define FileLineStr __FILE__ << "; line: " << __LINE__ << "; "
#endif

#ifndef FileLineWStr
#define FileLineWStr __FILE__ << L"; line: " << __LINE__ << L"; "
#endif

#ifndef Log
#define Log(x, ...)												\
	{															\
		std::vector<std::string> texts = { x, __VA_ARGS__ };	\
		std::stringstream _sstream;								\
																\
		for (const auto& text : texts)							\
			_sstream << text;									\
																\
		Logger::LogFunc(_sstream.str());						\
	}
#endif

#ifndef Logln
#define Logln(x, ...)											\
	{															\
		std::vector<std::string> texts = { x, __VA_ARGS__ };	\
		std::stringstream _sstream;								\
																\
		for (const auto& text : texts)							\
			_sstream << text;									\
		_sstream << '\n';										\
																\
		Logger::LogFunc(_sstream.str());						\
	}
#endif

#ifndef WLog
#define WLog(x, ...)											\
	{															\
		std::vector<std::wstring> texts = { x, __VA_ARGS__ };	\
		std::wstringstream _wsstream;							\
																\
		for (const auto& text : texts)							\
			_wsstream << text;									\
																\
		Logger::LogFunc(_wsstream.str());						\
	}
#endif

#ifndef WLogln
#define WLogln(x, ...)											\
	{															\
		std::vector<std::wstring> texts = { x, __VA_ARGS__ };	\
		std::wstringstream _wsstream;							\
																\
		for (const auto& text : texts)							\
			_wsstream << text;									\
		_wsstream << L'\n';										\
																\
		Logger::LogFunc(_wsstream.str());						\
	}
#endif

#ifndef Err
#define Err(x, ...)												\
	{															\
		std::vector<std::string> texts = { x, __VA_ARGS__ };	\
		std::stringstream _sstream;								\
																\
		_sstream << "[Error] " << FileLineStr;					\
		for (const auto& text : texts)							\
			_sstream << text;									\
																\
		Logger::LogFunc(_sstream.str());						\
	}
#endif

#ifndef WErr
#define WErr(x, ...)											\
	{															\
		std::vector<std::wstring> texts = { x, __VA_ARGS__ };	\
		std::wstringstream _wsstream;							\
																\
		_wsstream << L"[Error] " << FileLineWStr;				\
		for (const auto& text : texts)							\
			_wsstream << text;									\
																\
		Logger::LogFunc(_wsstream.str());						\
	}
#endif

#ifndef Errln
#define Errln(x, ...)											\
	{															\
		std::vector<std::string> texts = { x, __VA_ARGS__ };	\
		std::stringstream _sstream;								\
																\
		_sstream << "[Error] " << FileLineStr;					\
		for (const auto& text : texts)							\
			_sstream << text;									\
		_sstream << '\n';										\
																\
		Logger::LogFunc(_sstream.str());						\
	}
#endif

#ifndef WErrln
#define WErrln(x, ...)											\
	{															\
		std::vector<std::wstring> texts = { x, __VA_ARGS__ };	\
		std::wstringstream _wsstream;							\
																\
		_wsstream << L"[Error] " << FileLineWStr;				\
		for (const auto& text : texts)							\
			_wsstream << text;									\
		_wsstream << L'\n';										\
																\
		Logger::LogFunc(_wsstream.str());						\
	}
#endif


#ifndef ReturnFalse
#define ReturnFalse(__msg)	\
	{						\
		WErrln(__msg);		\
		return false;		\
	}
#endif

#ifndef CheckIsValid
#define CheckIsValid(__statement)			\
	{										\
		try {								\
			bool __result = __statement;	\
			if (!__result) {				\
				WErrln(L"");				\
				return false;				\
			}								\
		}									\
		catch (const std::exception& e) {	\
			Errln(e.what());				\
			return false;					\
		}									\
	}
#endif

#ifndef CheckHResult
#define CheckHResult(__statement)							\
	{														\
		try {												\
			HRESULT __result = __statement;					\
			if (FAILED(__result)) {							\
				auto errCode = GetLastError();				\
				std::wstringstream wsstream;				\
				wsstream << "0x" << std::hex << errCode;	\
				WErrln(wsstream.str());						\
				return false;								\
			}												\
		}													\
		catch (const std::exception& e) {					\
			Errln(e.what());								\
			return false;									\
		}													\
	}
#endif

#ifndef BreakIfInvalid
#define BreakIfInvalid(__statement)		\
	{									\
		bool __result = (__statement);	\
		if (!__result) {				\
			WErrln(L"");				\
			break;						\
		}								\
	}
#endif

namespace Logger {
	class LogHelper {
	public:
		static HANDLE ghLogFile;

		static std::mutex gLogFileMutex;
	};

	inline void LogFunc(const std::string& text) {
		std::wstring wstr;
		wstr.assign(text.begin(), text.end());

		DWORD writtenBytes = 0;

		LogHelper::gLogFileMutex.lock();

		WriteFile(
			LogHelper::ghLogFile,
			wstr.c_str(),
			static_cast<DWORD>(wstr.length() * sizeof(wchar_t)),
			&writtenBytes,
			NULL
		);

		LogHelper::gLogFileMutex.unlock();
	}

	inline void LogFunc(const std::wstring& text) {
		DWORD writtenBytes = 0;

		LogHelper::gLogFileMutex.lock();

		WriteFile(
			LogHelper::ghLogFile,
			text.c_str(),
			static_cast<DWORD>(text.length() * sizeof(wchar_t)),
			&writtenBytes,
			NULL
		);

		LogHelper::gLogFileMutex.unlock();
	}

	inline void SetTextToWnd(HWND hWnd, LPCWSTR newText) {
		SetWindowText(hWnd, newText);
	}

	inline void AppendTextToWnd(HWND hWnd, LPCWSTR newText) {
		int finalLength = GetWindowTextLength(hWnd) + lstrlen(newText) + 1;
		wchar_t* buf = reinterpret_cast<wchar_t*>(std::malloc(finalLength * sizeof(wchar_t)));

		GetWindowText(hWnd, buf, finalLength);

		wcscat_s(buf, finalLength, newText);

		SetWindowText(hWnd, buf);

		std::free(buf);
	}
};