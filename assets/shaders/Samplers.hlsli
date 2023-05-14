#ifndef __SAMPLERS_HLSLI__
#define __SAMPLERS_HLSLI__

SamplerState gsamPointWrap			: register(s0);
SamplerState gsamPointClamp			: register(s1);
SamplerState gsamLinearWrap			: register(s2);
SamplerState gsamLinearClamp		: register(s3);
SamplerState gsamAnisotropicWrap	: register(s4);
SamplerState gsamAnisotropicClamp	: register(s5);
SamplerState gsamAnisotropicBorder	: register(s6);
SamplerState gsamDepthMap			: register(s7);
SamplerComparisonState gsamShadow	: register(s8);
SamplerState gsamPointMirror		: register(s9);
SamplerState gsamLinearMirror		: register(s10);

#endif // __SAMPLERS_HLSLI__