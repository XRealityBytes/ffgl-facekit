#pragma once

#include <glm/glm.hpp>
#include <string>
#include <vector>

#include "FaceMorphTargets.h"

enum FaceModelFormat
{
	FaceModelFormat_Auto = 0,
	FaceModelFormat_IctFaceKitObj = 1,
	FaceModelFormat_Gltf = 2
};

struct FaceModelComponent
{
	std::string               name;
	std::vector<unsigned int> levelFaces[3];
	glm::vec3                 boundsMin = glm::vec3( 0.f );
	glm::vec3                 boundsMax = glm::vec3( 0.f );
	glm::vec3                 boundsCenter = glm::vec3( 0.f );
	bool                      hasBounds = false;
};

struct FaceModelData
{
	std::vector<glm::vec3>              basePositions;
	std::vector<unsigned int>           indices;
	std::vector<float>                  submeshIds;
	std::vector<FaceModelComponent>     components;
	std::vector<std::vector<glm::vec3>> expressionDeltas;
	std::vector<std::vector<glm::vec3>> identityDeltas;
	std::vector<unsigned int>           levelFaces[3];
	std::vector<std::string>            warnings;
	std::string                         sourcePath;
	std::string                         sourceFormat;
	std::string                         statusText;
	glm::vec3                           boundsMin = glm::vec3( 0.f );
	glm::vec3                           boundsMax = glm::vec3( 0.f );
	glm::vec3                           boundsCenter = glm::vec3( 0.f );
	glm::vec3                           focusPoint = glm::vec3( 0.f );
	float                               boundingRadius = 0.f;
	int                                 mappedExpressionCount = 0;
	bool                                supportsIdentity = false;
	bool                                hasBounds = false;
	bool                                hasFocusPoint = false;

	bool IsValid() const { return !basePositions.empty() && !indices.empty(); }
};

const char* FaceModelFormatToString( FaceModelFormat format );
bool        LoadFaceModel( const std::string& path, FaceModelFormat requestedFormat,
                           FaceModelData& outData, std::string& outError );
