/* Copyright (c) 2018-2019, NVIDIA CORPORATION. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *  * Neither the name of NVIDIA CORPORATION nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
 * PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY
 * OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __CLOSESTHIT_HLSL__
#define __CLOSESTHIT_HLSL__

#ifndef NUM_DIR_LIGHTS
	#define NUM_DIR_LIGHTS 1
#endif

#ifndef NUM_POINT_LIGHTS
	#define NUM_POINT_LIGHTS 0
#endif

#ifndef NUM_SPOT_LIGHTS
	#define NUM_SPOT_LIGHTS 0
#endif

#include "DxrCommon.hlsli"

[shader("closesthit")]
void ClosestHit(inout RayPayload payload, in Attributes attr) {
	float3 hitPosition = HitWorldPosition();

	// Get the base index of the triangle's first 32 bit index.
	uint indexSizeInBytes = 4;
	uint indicesPerTriangle = 3;
	uint triangleIndexStride = indicesPerTriangle * indexSizeInBytes;
	uint baseIndex = PrimitiveIndex() * triangleIndexStride;

	uint instID = InstanceID();

	ObjectData objData = gObjects[instID];

	// Load up 3 32 bit indices for the triangle.
	const uint3 indices = Load3x32BitIndices(baseIndex, objData.GeometryIndex);

	// Retrieve corresponding vertex normals for the triangle vertices.
	float3 vertexNormals[3] = {
		gVertices[objData.GeometryIndex][indices[0]].NormalW,
		gVertices[objData.GeometryIndex][indices[1]].NormalW,
		gVertices[objData.GeometryIndex][indices[2]].NormalW
	};

	// Compute the triangle's normal.
	// This is redundant and done for illustration purposes 
	// as all the per-vertex normals are the same and match triangle's normal in this sample. 
	float3 triangleNormal = normalize(HitAttribute(vertexNormals, attr));

	float3 toEyeW = normalize(gEyePosW - hitPosition);
	
	MaterialData matData = gMaterials[objData.MaterialIndex];

	float4 ambient = gAmbientLight * matData.DiffuseAlbedo;

	float shiness = 1.0f - matData.Roughness;
	Material mat = { matData.DiffuseAlbedo, matData.FresnelR0, shiness };

	float3 shadowFactor = 1.0f;
	float4 directLight = ComputeLighting(gLights, mat, hitPosition, triangleNormal, toEyeW, shadowFactor);

	float4 color = ambient + directLight;
	color.a = matData.DiffuseAlbedo.a;
	
	payload.Color = color;
	//payload.Color = float4(triangleNormal, 1.0f);
}

#endif // __CLOSESTHIT_HLSL__