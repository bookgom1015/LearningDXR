#include "GameTimer.h"

#include <Windows.h>

GameTimer::GameTimer()
	: mLimitFrameRate(LimitFrameRate::ELimitFrameRateNone),
	mSecondsPerCount(0.0),
	mDeltaTime(-1.0),
	mBaseTime(0),
	mPausedTime(0),
	mPrevTime(0),
	mCurrTime(0),
	mStopped(false) {
	__int64 countsPerSec;
	QueryPerformanceFrequency(reinterpret_cast<LARGE_INTEGER*>(&countsPerSec));
	mSecondsPerCount = 1.0 / static_cast<double>(countsPerSec);
}

// Returns the total time elapsed since Reset() was called, NOT counting any
// time when the clock is stopped.
float GameTimer::TotalTime() const {
	// If we are stopped, do not count the time that has passed since we stopped.
	// Moreover, if we previously already had a pause, the distance 
	// mStopTime - mBaseTime includes paused time, which we do not want to count.
	// To correct this, we can subtract the paused time from mStopTime:  
	//
	//                     |<--paused time-->|
	// ----*---------------*-----------------*------------*------------*------> time
	//  mBaseTime       mStopTime        startTime     mStopTime    mCurrTime
	if (mStopped)
		return static_cast<float>(((mStopTime - mPausedTime) - mBaseTime) * mSecondsPerCount);
	// The distance mCurrTime - mBaseTime includes paused time,
	// which we do not want to count.  To correct this, we can subtract 
	// the paused time from mCurrTime:  
	//
	//  (mCurrTime - mPausedTime) - mBaseTime 
	//
	//                     |<--paused time-->|
	// ----*---------------*-----------------*------------*------> time
	//  mBaseTime       mStopTime        startTime     mCurrTime	
	else
		return static_cast<float>(((mCurrTime - mPausedTime) - mBaseTime) * mSecondsPerCount);
}

float GameTimer::DeltaTime() const {
	switch (mLimitFrameRate) {
	case ELimitFrameRateNone:
		return static_cast<float>(mDeltaTime);
	case ELimitFrameRate30f:
		return mFrameTime30f;
	case ELimitFrameRate60f:
		return mFrameTime60f;
	case ELimitFrameRate120f:
		return mFrameTime120f;
	case ELimitFrameRate144f:
		return mFrameTime144f;
	case ELimitFrameRate244f:
		return mFrameTime244f;
	default:
		return 0.0f;
	}
}

void GameTimer::Reset() {
	__int64 currTime;
	QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&currTime));

	mBaseTime = currTime;
	mPrevTime = currTime;
	mStopTime = 0;
	mStopped = false;
}

void GameTimer::Start() {
	__int64 startTime;
	QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&startTime));

	// Accumulate the time elapsed between stop and start pairs.
	//
	//                     |<-------d------->|
	// ----*---------------*-----------------*------------> time
	//  mBaseTime       mStopTime        startTime     

	if (mStopped) {
		mPausedTime += (startTime - mStopTime);

		mPrevTime = startTime;
		mStopTime = 0;
		mStopped = false;
	}
}

void GameTimer::Stop() {
	if (!mStopped) {
		__int64 currTime;
		QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&currTime));

		mStopTime = currTime;
		mStopped = true;
	}
}

void GameTimer::Tick() {
	if (mStopped) {
		mDeltaTime = 0.0;
		return;
	}

	__int64 currTime;
	QueryPerformanceCounter(reinterpret_cast<LARGE_INTEGER*>(&currTime));
	mCurrTime = currTime;

	// Time difference between this frame and the previous.
	mDeltaTime = (mCurrTime - mPrevTime) * mSecondsPerCount;

	// Prepare for next frame.
	mPrevTime = mCurrTime;

	// Force nonnegative.  The DXSDK's CDXUTTimer mentions that if the 
	// processor goes into a power save mode or we get shuffled to another
	// processor, then mDeltaTime can be negative.
	if (mDeltaTime < 0.0)
		mDeltaTime = 0.0;
}

float GameTimer::GetLimitFrameRate() const {
	switch (mLimitFrameRate) {
	case ELimitFrameRateNone:
		return 0.0f;
	case ELimitFrameRate30f:
		return mFrameTime30f;
	case ELimitFrameRate60f:
		return mFrameTime60f;
	case ELimitFrameRate120f:
		return mFrameTime120f;
	case ELimitFrameRate144f:
		return mFrameTime144f;
	case ELimitFrameRate244f:
		return mFrameTime244f;
	default:
		return 0.0f;
	}
}

void GameTimer::SetLimitFrameRate(LimitFrameRate type) {
	mLimitFrameRate = type;
}