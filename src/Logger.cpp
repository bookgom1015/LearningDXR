#include "Logger.h"

HANDLE Logger::LogHelper::ghLogFile = CreateFile(
	L"./log.txt",
	GENERIC_WRITE,
	FILE_SHARE_WRITE,
	NULL,
	CREATE_ALWAYS,
	FILE_ATTRIBUTE_NORMAL,
	NULL
);

std::mutex Logger::LogHelper::gLogFileMutex;