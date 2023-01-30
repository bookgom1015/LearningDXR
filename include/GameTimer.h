#pragma once

class GameTimer {
public:
	enum LimitFrameRate {
		ELimitFrameRateNone,
		ELimitFrameRate30f,
		ELimitFrameRate60f,
		ELimitFrameRate120f,
		ELimitFrameRate144f,
		ELimitFrameRate244f
	};

public:
	GameTimer();

public:
	float TotalTime() const; // in seconds
	float DeltaTime() const; // in seconds

	void Reset(); // Call before message loop.
	void Start(); // Call when unpaused.
	void Stop();  // Call when paused.
	void Tick();  // Call every frame.

	float GetLimitFrameRate() const;
	void SetLimitFrameRate(LimitFrameRate type);

private:
	LimitFrameRate mLimitFrameRate;

	double mSecondsPerCount;
	double mDeltaTime;

	__int64 mBaseTime;
	__int64 mPausedTime;
	__int64 mStopTime;
	__int64 mPrevTime;
	__int64 mCurrTime;

	bool mStopped;

	const float mFrameTime30f = 1.0f / 30.0f;
	const float mFrameTime60f = 1.0f / 60.0f;
	const float mFrameTime120f = 1.0f / 120.0f;
	const float mFrameTime144f = 1.0f / 144.0f;
	const float mFrameTime244f = 1.0f / 244.0f;
};