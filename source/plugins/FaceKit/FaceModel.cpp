#define TINYOBJLOADER_IMPLEMENTATION
#include "FaceModel.h"

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-function"
#endif
#include <tiny_obj_loader.h>
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#include <nlohmann/json.hpp>

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>

using nlohmann::json;

namespace
{
static const int ICT_TOTAL_VERTS = 26719;
static const int NUM_LEVELS = 3;

struct SubMeshRange
{
	const char* name;
	int         vertStart;
	int         vertEnd;
};

struct MeshInstance
{
	int         meshIndex = -1;
	int         nodeIndex = -1;
	glm::mat4   transform = glm::mat4( 1.f );
	std::string nodeName;
};

const SubMeshRange ICT_SUBMESH_RANGES[17] = {
	{ "Face",             0,     9408  },
	{ "Head and Neck",    9409,  11247 },
	{ "Mouth Socket",     11248, 13293 },
	{ "Eye Socket L",     13294, 13677 },
	{ "Eye Socket R",     13678, 14061 },
	{ "Gums and Tongue",  14062, 17038 },
	{ "Teeth",            17039, 21450 },
	{ "Eyeball L",        21451, 23020 },
	{ "Eyeball R",        23021, 24590 },
	{ "Lacrimal L",       24591, 24794 },
	{ "Lacrimal R",       24795, 24998 },
	{ "Eye Blend L",      24999, 25022 },
	{ "Eye Blend R",      25023, 25046 },
	{ "Eye Occlusion L",  25047, 25198 },
	{ "Eye Occlusion R",  25199, 25350 },
	{ "Eyelashes L",      25351, 26034 },
	{ "Eyelashes R",      26035, 26718 },
};

const bool ICT_LEVEL_INCLUDE[NUM_LEVELS][17] = {
	{ true, true, true,false,false,false,false,false,false,false,false,false,false,false,false,false,false },
	{ true, true, true, true, true,false,false, true, true, true, true, true, true, true, true,false,false },
	{ true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true, true }
};

enum
{
	GLTF_COMPONENT_UNSIGNED_BYTE  = 5121,
	GLTF_COMPONENT_UNSIGNED_SHORT = 5123,
	GLTF_COMPONENT_UNSIGNED_INT   = 5125,
	GLTF_COMPONENT_FLOAT          = 5126,
	GLTF_MODE_TRIANGLES           = 4
};

unsigned int ReadU32le( const unsigned char* p )
{
	return (unsigned int)p[0] |
	       ( (unsigned int)p[1] << 8 ) |
	       ( (unsigned int)p[2] << 16 ) |
	       ( (unsigned int)p[3] << 24 );
}

std::string ToLower( const std::string& value )
{
	std::string out = value;
	std::transform( out.begin(), out.end(), out.begin(),
	                []( unsigned char c ) { return (char)std::tolower( c ); } );
	return out;
}

std::string TrimWhitespace( const std::string& value )
{
	size_t start = 0;
	while( start < value.size() && std::isspace( (unsigned char)value[start] ) )
		++start;

	size_t end = value.size();
	while( end > start && std::isspace( (unsigned char)value[end - 1] ) )
		--end;

	return value.substr( start, end - start );
}

std::string TrimTrailingSeparators( const std::string& path )
{
	std::string trimmed = path;
	while( !trimmed.empty() && ( trimmed.back() == '/' || trimmed.back() == '\\' ) )
		trimmed.pop_back();
	return trimmed;
}

std::string FileExtension( const std::string& path )
{
	const size_t slash = path.find_last_of( "/\\" );
	const size_t dot   = path.find_last_of( '.' );
	if( dot == std::string::npos || ( slash != std::string::npos && dot < slash ) )
		return "";
	return ToLower( path.substr( dot ) );
}

std::string DirectoryName( const std::string& path )
{
	const size_t slash = path.find_last_of( "/\\" );
	if( slash == std::string::npos )
		return "";
	return path.substr( 0, slash );
}

std::string JoinPath( const std::string& base, const std::string& leaf )
{
	if( base.empty() )
		return leaf;
	const char sep = ( base.find( '\\' ) != std::string::npos ) ? '\\' : '/';
	if( base.back() == '/' || base.back() == '\\' )
		return base + leaf;
	return base + sep + leaf;
}

std::string PreviewNames( const std::vector<std::string>& names, size_t maxCount )
{
	std::ostringstream ss;
	for( size_t i = 0; i < names.size() && i < maxCount; ++i )
	{
		if( i > 0 ) ss << ", ";
		ss << names[i];
	}
	if( names.size() > maxCount )
		ss << " ...";
	return ss.str();
}

std::string MakeUniqueComponentName( const std::string& candidate, const std::string& fallback,
                                     std::map<std::string, int>& counts )
{
	std::string baseName = TrimWhitespace( candidate );
	if( baseName.empty() )
		baseName = fallback;
	if( baseName.empty() )
		baseName = "Component";

	const std::string key = ToLower( baseName );
	int& count = counts[key];
	++count;
	if( count == 1 )
		return baseName;
	return baseName + " (" + std::to_string( count ) + ")";
}

int VertexSubMesh( int vertIdx )
{
	for( int i = 0; i < 17; ++i )
	{
		if( vertIdx >= ICT_SUBMESH_RANGES[i].vertStart && vertIdx <= ICT_SUBMESH_RANGES[i].vertEnd )
			return i;
	}
	return 0;
}

int TriangleSubMesh( unsigned int v0, unsigned int v1, unsigned int v2 )
{
	return std::max( std::max( VertexSubMesh( (int)v0 ), VertexSubMesh( (int)v1 ) ),
	                 VertexSubMesh( (int)v2 ) );
}

void BuildIctLevelFaces( const std::vector<unsigned int>& allFaces,
                         std::vector<FaceModelComponent>& components,
                         std::vector<unsigned int> levelFaces[NUM_LEVELS] )
{
	components.clear();
	components.resize( 17 );
	for( int level = 0; level < NUM_LEVELS; ++level )
	{
		levelFaces[level].clear();
		levelFaces[level].reserve( allFaces.size() );
	}
	for( int componentIndex = 0; componentIndex < 17; ++componentIndex )
	{
		components[componentIndex].name = ICT_SUBMESH_RANGES[componentIndex].name;
	}

	for( size_t triangle = 0; triangle + 2 < allFaces.size(); triangle += 3 )
	{
		const unsigned int v0 = allFaces[triangle + 0];
		const unsigned int v1 = allFaces[triangle + 1];
		const unsigned int v2 = allFaces[triangle + 2];
		const int componentIndex = TriangleSubMesh( v0, v1, v2 );
		for( int level = 0; level < NUM_LEVELS; ++level )
		{
			if( ICT_LEVEL_INCLUDE[level][componentIndex] )
			{
				levelFaces[level].push_back( v0 );
				levelFaces[level].push_back( v1 );
				levelFaces[level].push_back( v2 );
				components[componentIndex].levelFaces[level].push_back( v0 );
				components[componentIndex].levelFaces[level].push_back( v1 );
				components[componentIndex].levelFaces[level].push_back( v2 );
			}
		}
	}
}

bool LoadObjPositions( const std::string& path, std::vector<glm::vec3>& outPositions )
{
	tinyobj::ObjReaderConfig config;
	config.triangulate  = false;
	config.vertex_color = false;

	tinyobj::ObjReader reader;
	if( !reader.ParseFromFile( path, config ) )
		return false;

	const tinyobj::attrib_t& attrib = reader.GetAttrib();
	const int vertexCount = (int)( attrib.vertices.size() / 3 );
	outPositions.resize( vertexCount );
	for( int i = 0; i < vertexCount; ++i )
	{
		outPositions[i] = glm::vec3(
			attrib.vertices[i * 3 + 0],
			attrib.vertices[i * 3 + 1],
			attrib.vertices[i * 3 + 2]
		);
	}
	return true;
}

void ComputeModelBounds( FaceModelData& outData )
{
	if( outData.basePositions.empty() )
	{
		outData.boundsMin = glm::vec3( 0.f );
		outData.boundsMax = glm::vec3( 0.f );
		outData.boundsCenter = glm::vec3( 0.f );
		outData.boundingRadius = 0.f;
		outData.hasBounds = false;
		return;
	}

	glm::vec3 boundsMin = outData.basePositions[0];
	glm::vec3 boundsMax = outData.basePositions[0];
	for( size_t i = 1; i < outData.basePositions.size(); ++i )
	{
		boundsMin = glm::min( boundsMin, outData.basePositions[i] );
		boundsMax = glm::max( boundsMax, outData.basePositions[i] );
	}

	const glm::vec3 center = 0.5f * ( boundsMin + boundsMax );
	float radiusSquared = 0.f;
	for( size_t i = 0; i < outData.basePositions.size(); ++i )
	{
		const glm::vec3 delta = outData.basePositions[i] - center;
		radiusSquared = std::max( radiusSquared, glm::dot( delta, delta ) );
	}

	outData.boundsMin = boundsMin;
	outData.boundsMax = boundsMax;
	outData.boundsCenter = center;
	outData.boundingRadius = std::sqrt( radiusSquared );
	outData.hasBounds = true;
}

bool ComputeBoundsFromPositions( const std::vector<glm::vec3>& positions,
                                 glm::vec3& outMin, glm::vec3& outMax, glm::vec3& outCenter )
{
	if( positions.empty() )
		return false;

	outMin = positions[0];
	outMax = positions[0];
	for( size_t i = 1; i < positions.size(); ++i )
	{
		outMin = glm::min( outMin, positions[i] );
		outMax = glm::max( outMax, positions[i] );
	}
	outCenter = 0.5f * ( outMin + outMax );
	return true;
}

void SetComponentBounds( FaceModelComponent& component, const std::vector<glm::vec3>& positions )
{
	component.hasBounds = ComputeBoundsFromPositions( positions, component.boundsMin,
	                                                  component.boundsMax, component.boundsCenter );
}

void SetComponentBoundsFromVertexRange( FaceModelComponent& component, const std::vector<glm::vec3>& positions,
                                        int startIndex, int endIndex )
{
	if( positions.empty() || startIndex < 0 || endIndex < startIndex || startIndex >= (int)positions.size() )
	{
		component.hasBounds = false;
		return;
	}

	const int safeEnd = std::min( endIndex, (int)positions.size() - 1 );
	component.boundsMin = positions[startIndex];
	component.boundsMax = positions[startIndex];
	for( int index = startIndex + 1; index <= safeEnd; ++index )
	{
		component.boundsMin = glm::min( component.boundsMin, positions[index] );
		component.boundsMax = glm::max( component.boundsMax, positions[index] );
	}
	component.boundsCenter = 0.5f * ( component.boundsMin + component.boundsMax );
	component.hasBounds = true;
}

int FocusPriorityForComponentName( const std::string& name )
{
	const std::string lowered = ToLower( name );
	if( lowered.find( "face" ) != std::string::npos )
		return 3;
	if( lowered.find( "skin" ) != std::string::npos )
		return 2;
	if( lowered.find( "head" ) != std::string::npos &&
	    lowered.find( "backhead" ) == std::string::npos &&
	    lowered.find( "back head" ) == std::string::npos )
		return 1;
	return 0;
}

glm::vec3 ComputeComponentFocusPoint( const FaceModelComponent& component, int priority )
{
	glm::vec3 focus = component.boundsCenter;
	if( !component.hasBounds )
		return focus;

	const glm::vec3 size = component.boundsMax - component.boundsMin;
	if( priority >= 3 )
		focus.y = component.boundsMin.y + size.y * 0.42f;
	else if( priority >= 1 )
		focus.y = component.boundsMin.y + size.y * 0.46f;

	return focus;
}

void ComputePreferredFocusPoint( FaceModelData& outData )
{
	int bestPriority = 0;
	glm::vec3 focusSum( 0.f );
	int focusCount = 0;

	for( size_t i = 0; i < outData.components.size(); ++i )
	{
		const FaceModelComponent& component = outData.components[i];
		if( !component.hasBounds )
			continue;

		const int priority = FocusPriorityForComponentName( component.name );
		if( priority <= 0 )
			continue;

		if( priority > bestPriority )
		{
			bestPriority = priority;
			focusSum = ComputeComponentFocusPoint( component, priority );
			focusCount = 1;
		}
		else if( priority == bestPriority )
		{
			focusSum += ComputeComponentFocusPoint( component, priority );
			++focusCount;
		}
	}

	if( focusCount > 0 )
	{
		outData.focusPoint = focusSum / (float)focusCount;
		outData.hasFocusPoint = true;
		return;
	}

	if( outData.hasBounds )
	{
		outData.focusPoint = outData.boundsCenter;
		outData.hasFocusPoint = true;
	}
	else
	{
		outData.focusPoint = glm::vec3( 0.f );
		outData.hasFocusPoint = false;
	}
}

bool LoadIctFaceKitObjModel( const std::string& rawPath, FaceModelData& outData, std::string& outError )
{
	const std::string normalizedPath = TrimTrailingSeparators( rawPath );
	const std::string extension      = FileExtension( normalizedPath );
	const std::string modelDir       = ( extension == ".obj" ) ? DirectoryName( normalizedPath ) : normalizedPath;
	const std::string basePath       = JoinPath( modelDir, "generic_neutral_mesh.obj" );

	tinyobj::ObjReaderConfig config;
	config.triangulate  = true;
	config.vertex_color = false;

	tinyobj::ObjReader reader;
	if( !reader.ParseFromFile( basePath, config ) )
	{
		outError = "Failed to load ICT FaceKit base OBJ: " + basePath;
		return false;
	}

	const tinyobj::attrib_t& attrib = reader.GetAttrib();
	const int vertexCount = (int)( attrib.vertices.size() / 3 );
	if( vertexCount != ICT_TOTAL_VERTS )
	{
		outError = "Unexpected ICT vertex count " + std::to_string( vertexCount ) +
		           " (expected " + std::to_string( ICT_TOTAL_VERTS ) + ")";
		return false;
	}

	outData = FaceModelData();
	outData.sourcePath   = modelDir;
	outData.sourceFormat = "ICT FaceKit OBJ";
	outData.basePositions.resize( vertexCount );
	outData.submeshIds.resize( vertexCount );
	outData.expressionDeltas.assign( NUM_EXPRESSIONS, std::vector<glm::vec3>( vertexCount, glm::vec3( 0.f ) ) );
	outData.identityDeltas.assign( NUM_IDENTITIES,  std::vector<glm::vec3>( vertexCount, glm::vec3( 0.f ) ) );

	for( int i = 0; i < vertexCount; ++i )
	{
		outData.basePositions[i] = glm::vec3(
			attrib.vertices[i * 3 + 0],
			attrib.vertices[i * 3 + 1],
			attrib.vertices[i * 3 + 2]
		);
		outData.submeshIds[i] = (float)VertexSubMesh( i );
	}

	for( size_t shapeIndex = 0; shapeIndex < reader.GetShapes().size(); ++shapeIndex )
	{
		const tinyobj::shape_t& shape = reader.GetShapes()[shapeIndex];
		for( size_t i = 0; i < shape.mesh.indices.size(); ++i )
			outData.indices.push_back( (unsigned int)shape.mesh.indices[i].vertex_index );
	}

	BuildIctLevelFaces( outData.indices, outData.components, outData.levelFaces );
	ComputeModelBounds( outData );
	for( int componentIndex = 0; componentIndex < 17 && componentIndex < (int)outData.components.size(); ++componentIndex )
	{
		SetComponentBoundsFromVertexRange( outData.components[componentIndex], outData.basePositions,
		                                  ICT_SUBMESH_RANGES[componentIndex].vertStart,
		                                  ICT_SUBMESH_RANGES[componentIndex].vertEnd );
	}
	ComputePreferredFocusPoint( outData );

	std::vector<std::string> missingExpressions;
	for( int expressionIndex = 0; expressionIndex < NUM_EXPRESSIONS; ++expressionIndex )
	{
		const std::string morphPath = JoinPath( modelDir, std::string( GetFaceKitExpressionName( expressionIndex ) ) + ".obj" );
		std::vector<glm::vec3> morphPositions;
		if( !LoadObjPositions( morphPath, morphPositions ) || (int)morphPositions.size() != vertexCount )
		{
			missingExpressions.push_back( GetFaceKitExpressionName( expressionIndex ) );
			continue;
		}

		for( int vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex )
			outData.expressionDeltas[expressionIndex][vertexIndex] = morphPositions[vertexIndex] - outData.basePositions[vertexIndex];
	}

	std::vector<std::string> missingIdentities;
	int loadedIdentityCount = 0;
	for( int identityIndex = 0; identityIndex < NUM_IDENTITIES; ++identityIndex )
	{
		char name[32];
		snprintf( name, sizeof( name ), "identity%03d", identityIndex );
		const std::string morphPath = JoinPath( modelDir, name );
		std::vector<glm::vec3> morphPositions;
		if( !LoadObjPositions( morphPath + ".obj", morphPositions ) || (int)morphPositions.size() != vertexCount )
		{
			missingIdentities.push_back( name );
			continue;
		}

		++loadedIdentityCount;
		for( int vertexIndex = 0; vertexIndex < vertexCount; ++vertexIndex )
			outData.identityDeltas[identityIndex][vertexIndex] = morphPositions[vertexIndex] - outData.basePositions[vertexIndex];
	}

	outData.mappedExpressionCount = NUM_EXPRESSIONS - (int)missingExpressions.size();
	outData.supportsIdentity      = loadedIdentityCount > 0;
	outData.statusText = "ICT FaceKit OBJ | " + std::to_string( vertexCount ) + " verts | " +
	                     std::to_string( outData.mappedExpressionCount ) + "/" + std::to_string( NUM_EXPRESSIONS ) +
	                     " blendshapes | " + std::to_string( loadedIdentityCount ) + "/" +
	                     std::to_string( NUM_IDENTITIES ) + " identity morphs | " +
	                     std::to_string( outData.components.size() ) + " components";

	if( !missingExpressions.empty() )
	{
		outData.warnings.push_back(
			"Missing " + std::to_string( missingExpressions.size() ) + " ICT expression morphs: " +
			PreviewNames( missingExpressions, 6 )
		);
	}
	if( !outData.supportsIdentity )
	{
		outData.warnings.push_back( "ICT identity morphs were not found; /facekit/identity messages will be ignored." );
	}
	else if( !missingIdentities.empty() )
	{
		outData.warnings.push_back(
			"Missing " + std::to_string( missingIdentities.size() ) + " ICT identity morphs: " +
			PreviewNames( missingIdentities, 4 )
		);
	}

	return true;
}

bool ReadBinaryFile( const std::string& path, std::vector<unsigned char>& outBytes )
{
	std::ifstream file( path.c_str(), std::ios::binary );
	if( !file.is_open() )
		return false;

	file.seekg( 0, std::ios::end );
	const std::streampos size = file.tellg();
	file.seekg( 0, std::ios::beg );
	if( size <= 0 )
	{
		outBytes.clear();
		return true;
	}

	outBytes.resize( (size_t)size );
	file.read( (char*)outBytes.data(), (std::streamsize)size );
	return file.good() || file.eof();
}

std::string ReadTextFile( const std::string& path )
{
	std::ifstream file( path.c_str(), std::ios::binary );
	if( !file.is_open() )
		return "";
	std::ostringstream ss;
	ss << file.rdbuf();
	return ss.str();
}

bool DecodeBase64( const std::string& input, std::vector<unsigned char>& outBytes )
{
	static const int DECODE_TABLE[256] = {
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,62,-1,-1,-1,63,
		52,53,54,55,56,57,58,59,60,61,-1,-1,-1,-1,-1,-1,
		-1, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9,10,11,12,13,14,
		15,16,17,18,19,20,21,22,23,24,25,-1,-1,-1,-1,-1,
		-1,26,27,28,29,30,31,32,33,34,35,36,37,38,39,40,
		41,42,43,44,45,46,47,48,49,50,51,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,
		-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1,-1
	};

	outBytes.clear();
	int val = 0;
	int bits = -8;
	for( size_t i = 0; i < input.size(); ++i )
	{
		const unsigned char c = (unsigned char)input[i];
		if( c == '=' ) break;
		if( std::isspace( c ) ) continue;
		const int decoded = DECODE_TABLE[c];
		if( decoded < 0 ) return false;
		val = ( val << 6 ) + decoded;
		bits += 6;
		if( bits >= 0 )
		{
			outBytes.push_back( (unsigned char)( ( val >> bits ) & 0xFF ) );
			bits -= 8;
		}
	}
	return true;
}

bool LoadDataUri( const std::string& uri, std::vector<unsigned char>& outBytes )
{
	const size_t comma = uri.find( ',' );
	if( comma == std::string::npos )
		return false;
	const std::string header  = uri.substr( 0, comma );
	const std::string payload = uri.substr( comma + 1 );
	if( header.find( ";base64" ) == std::string::npos )
		return false;
	return DecodeBase64( payload, outBytes );
}

glm::mat4 ReadNodeTransform( const json& node )
{
	if( node.contains( "matrix" ) && node["matrix"].is_array() && node["matrix"].size() == 16 )
	{
		glm::mat4 matrix( 1.f );
		for( int i = 0; i < 16; ++i )
			matrix[i / 4][i % 4] = node["matrix"][i].get<float>();
		return matrix;
	}

	glm::vec3 translation( 0.f );
	if( node.contains( "translation" ) && node["translation"].is_array() && node["translation"].size() == 3 )
	{
		translation = glm::vec3(
			node["translation"][0].get<float>(),
			node["translation"][1].get<float>(),
			node["translation"][2].get<float>()
		);
	}

	glm::quat rotation( 1.f, 0.f, 0.f, 0.f );
	if( node.contains( "rotation" ) && node["rotation"].is_array() && node["rotation"].size() == 4 )
	{
		rotation = glm::quat(
			node["rotation"][3].get<float>(),
			node["rotation"][0].get<float>(),
			node["rotation"][1].get<float>(),
			node["rotation"][2].get<float>()
		);
	}

	glm::vec3 scale( 1.f );
	if( node.contains( "scale" ) && node["scale"].is_array() && node["scale"].size() == 3 )
	{
		scale = glm::vec3(
			node["scale"][0].get<float>(),
			node["scale"][1].get<float>(),
			node["scale"][2].get<float>()
		);
	}

	const glm::mat4 translationMatrix = glm::translate( glm::mat4( 1.f ), translation );
	const glm::mat4 rotationMatrix    = glm::mat4_cast( rotation );
	const glm::mat4 scaleMatrix       = glm::scale( glm::mat4( 1.f ), scale );
	return translationMatrix * rotationMatrix * scaleMatrix;
}

std::string ReadNamedJsonValue( const json& value )
{
	if( !value.is_object() || !value.contains( "name" ) || !value["name"].is_string() )
		return "";
	return TrimWhitespace( value["name"].get<std::string>() );
}

std::string ReadMaterialName( const json& doc, const json& primitive )
{
	if( !primitive.contains( "material" ) || !doc.contains( "materials" ) || !doc["materials"].is_array() )
		return "";

	const int materialIndex = primitive["material"].get<int>();
	if( materialIndex < 0 || materialIndex >= (int)doc["materials"].size() )
		return "";
	return ReadNamedJsonValue( doc["materials"][materialIndex] );
}

void CollectMeshInstancesRecursive( const json& doc, int nodeIndex, const glm::mat4& parentTransform,
                                    std::vector<MeshInstance>& outInstances )
{
	if( !doc.contains( "nodes" ) || !doc["nodes"].is_array() )
		return;
	if( nodeIndex < 0 || nodeIndex >= (int)doc["nodes"].size() )
		return;

	const json& node = doc["nodes"][nodeIndex];
	const glm::mat4 worldTransform = parentTransform * ReadNodeTransform( node );

	if( node.contains( "mesh" ) && doc.contains( "meshes" ) && doc["meshes"].is_array() )
	{
		const int meshIndex = node["mesh"].get<int>();
		if( meshIndex >= 0 && meshIndex < (int)doc["meshes"].size() )
		{
			MeshInstance instance;
			instance.meshIndex = meshIndex;
			instance.nodeIndex = nodeIndex;
			instance.transform = worldTransform;
			instance.nodeName  = ReadNamedJsonValue( node );
			outInstances.push_back( instance );
		}
	}

	if( node.contains( "children" ) && node["children"].is_array() )
	{
		for( size_t i = 0; i < node["children"].size(); ++i )
			CollectMeshInstancesRecursive( doc, node["children"][i].get<int>(), worldTransform, outInstances );
	}
}

bool CollectMeshInstances( const json& doc, std::vector<MeshInstance>& outInstances )
{
	outInstances.clear();

	if( doc.contains( "scenes" ) && doc["scenes"].is_array() && doc.contains( "scene" ) )
	{
		const int sceneIndex = doc["scene"].get<int>();
		if( sceneIndex >= 0 && sceneIndex < (int)doc["scenes"].size() )
		{
			const json& scene = doc["scenes"][sceneIndex];
			if( scene.contains( "nodes" ) && scene["nodes"].is_array() )
			{
				for( size_t i = 0; i < scene["nodes"].size(); ++i )
					CollectMeshInstancesRecursive( doc, scene["nodes"][i].get<int>(), glm::mat4( 1.f ), outInstances );
			}
		}
	}

	if( outInstances.empty() && doc.contains( "nodes" ) && doc["nodes"].is_array() )
	{
		for( size_t i = 0; i < doc["nodes"].size(); ++i )
			CollectMeshInstancesRecursive( doc, (int)i, glm::mat4( 1.f ), outInstances );
	}

	if( outInstances.empty() && doc.contains( "meshes" ) && doc["meshes"].is_array() )
	{
		for( size_t i = 0; i < doc["meshes"].size(); ++i )
		{
			MeshInstance instance;
			instance.meshIndex = (int)i;
			outInstances.push_back( instance );
		}
	}

	return !outInstances.empty();
}

std::vector<std::string> ReadMeshTargetNames( const json& mesh )
{
	std::vector<std::string> targetNames;
	if( mesh.contains( "extras" ) && mesh["extras"].is_object() &&
	    mesh["extras"].contains( "targetNames" ) && mesh["extras"]["targetNames"].is_array() )
	{
		for( size_t i = 0; i < mesh["extras"]["targetNames"].size(); ++i )
			targetNames.push_back( mesh["extras"]["targetNames"][i].get<std::string>() );
	}
	else if( mesh.contains( "targetNames" ) && mesh["targetNames"].is_array() )
	{
		for( size_t i = 0; i < mesh["targetNames"].size(); ++i )
			targetNames.push_back( mesh["targetNames"][i].get<std::string>() );
	}
	return targetNames;
}

std::string BuildGltfComponentName( const json& doc, const MeshInstance& instance,
                                    const json& mesh, int meshIndex,
                                    const json& primitive, size_t primitiveIndex, size_t primitiveCount )
{
	const std::string meshName = ReadNamedJsonValue( mesh );
	const std::string materialName = ReadMaterialName( doc, primitive );

	std::string name = instance.nodeName;
	if( name.empty() )
		name = meshName;
	if( name.empty() && !materialName.empty() && primitiveCount == 1 )
		name = materialName;
	if( name.empty() )
		name = "Mesh " + std::to_string( meshIndex + 1 );

	if( primitiveCount > 1 )
	{
		if( !materialName.empty() && materialName != name )
			name += " / " + materialName;
		else
			name += " / Part " + std::to_string( primitiveIndex + 1 );
	}

	return name;
}

bool LoadGltfDocument( const std::string& path, json& outJson, std::vector<unsigned char>& outGlbBin, std::string& outError )
{
	const std::string extension = FileExtension( path );
	if( extension == ".glb" )
	{
		std::vector<unsigned char> bytes;
		if( !ReadBinaryFile( path, bytes ) || bytes.size() < 20 )
		{
			outError = "Failed to read GLB file: " + path;
			return false;
		}

		const unsigned int magic   = ReadU32le( bytes.data() + 0 );
		const unsigned int version = ReadU32le( bytes.data() + 4 );
		if( magic != 0x46546C67 || version != 2 )
		{
			outError = "Unsupported GLB header in " + path;
			return false;
		}

		size_t offset = 12;
		std::string jsonChunk;
		outGlbBin.clear();
		while( offset + 8 <= bytes.size() )
		{
			const unsigned int chunkLength = ReadU32le( bytes.data() + offset + 0 );
			const unsigned int chunkType   = ReadU32le( bytes.data() + offset + 4 );
			offset += 8;
			if( offset + chunkLength > bytes.size() )
			{
				outError = "GLB chunk exceeds file size in " + path;
				return false;
			}

			if( chunkType == 0x4E4F534A )
				jsonChunk.assign( (const char*)bytes.data() + offset, (const char*)bytes.data() + offset + chunkLength );
			else if( chunkType == 0x004E4942 )
				outGlbBin.assign( bytes.begin() + offset, bytes.begin() + offset + chunkLength );

			offset += chunkLength;
		}

		if( jsonChunk.empty() )
		{
			outError = "GLB JSON chunk not found in " + path;
			return false;
		}
		while( !jsonChunk.empty() && jsonChunk.back() == '\0' )
			jsonChunk.pop_back();

		try
		{
			outJson = json::parse( jsonChunk );
		}
		catch( const std::exception& e )
		{
			outError = std::string( "Failed to parse GLB JSON: " ) + e.what();
			return false;
		}
		return true;
	}

	const std::string text = ReadTextFile( path );
	if( text.empty() )
	{
		outError = "Failed to read glTF file: " + path;
		return false;
	}

	try
	{
		outJson = json::parse( text );
	}
	catch( const std::exception& e )
	{
		outError = std::string( "Failed to parse glTF JSON: " ) + e.what();
		return false;
	}

	outGlbBin.clear();
	return true;
}

bool LoadGltfBuffers( const json& doc, const std::vector<unsigned char>& glbBin, const std::string& baseDir,
                      std::vector<std::vector<unsigned char>>& outBuffers, std::string& outError )
{
	if( !doc.contains( "buffers" ) || !doc["buffers"].is_array() )
	{
		outError = "glTF document does not contain buffers.";
		return false;
	}

	outBuffers.clear();
	outBuffers.resize( doc["buffers"].size() );

	for( size_t i = 0; i < doc["buffers"].size(); ++i )
	{
		const json& buffer = doc["buffers"][i];
		if( buffer.contains( "uri" ) )
		{
			const std::string uri = buffer["uri"].get<std::string>();
			if( uri.find( "data:" ) == 0 )
			{
				if( !LoadDataUri( uri, outBuffers[i] ) )
				{
					outError = "Unsupported glTF data URI buffer.";
					return false;
				}
			}
			else
			{
				if( !ReadBinaryFile( JoinPath( baseDir, uri ), outBuffers[i] ) )
				{
					outError = "Failed to read glTF buffer: " + uri;
					return false;
				}
			}
		}
		else
		{
			if( glbBin.empty() )
			{
				outError = "glTF buffer has no URI and no GLB BIN chunk is available.";
				return false;
			}
			outBuffers[i] = glbBin;
		}
	}
	return true;
}

int AccessorTypeComponentCount( const std::string& type )
{
	if( type == "SCALAR" ) return 1;
	if( type == "VEC2" )   return 2;
	if( type == "VEC3" )   return 3;
	if( type == "VEC4" )   return 4;
	return 0;
}

size_t ComponentTypeSize( int componentType )
{
	switch( componentType )
	{
	case GLTF_COMPONENT_UNSIGNED_BYTE:  return 1;
	case GLTF_COMPONENT_UNSIGNED_SHORT: return 2;
	case GLTF_COMPONENT_UNSIGNED_INT:   return 4;
	case GLTF_COMPONENT_FLOAT:          return 4;
	default:                            return 0;
	}
}

bool ResolveAccessorView( const json& doc, const std::vector<std::vector<unsigned char>>& buffers, int accessorIndex,
                          int expectedComponentType, const char* expectedType,
                          const unsigned char*& outData, size_t& outStride, size_t& outCount, std::string& outError )
{
	if( !doc.contains( "accessors" ) || !doc["accessors"].is_array() ||
	    accessorIndex < 0 || accessorIndex >= (int)doc["accessors"].size() )
	{
		outError = "Invalid glTF accessor index.";
		return false;
	}

	const json& accessor = doc["accessors"][accessorIndex];
	if( accessor.value( "componentType", 0 ) != expectedComponentType )
	{
		outError = "Unexpected glTF accessor component type.";
		return false;
	}
	if( accessor.value( "type", std::string() ) != expectedType )
	{
		outError = "Unexpected glTF accessor type.";
		return false;
	}
	if( !accessor.contains( "bufferView" ) )
	{
		outError = "glTF accessor is missing a bufferView.";
		return false;
	}

	const int bufferViewIndex = accessor["bufferView"].get<int>();
	if( !doc.contains( "bufferViews" ) || !doc["bufferViews"].is_array() ||
	    bufferViewIndex < 0 || bufferViewIndex >= (int)doc["bufferViews"].size() )
	{
		outError = "Invalid glTF bufferView index.";
		return false;
	}

	const json& bufferView = doc["bufferViews"][bufferViewIndex];
	const int bufferIndex = bufferView.value( "buffer", -1 );
	if( bufferIndex < 0 || bufferIndex >= (int)buffers.size() )
	{
		outError = "Invalid glTF buffer reference.";
		return false;
	}

	const int componentCount = AccessorTypeComponentCount( expectedType );
	const size_t componentSize = ComponentTypeSize( expectedComponentType );
	const size_t elementSize = (size_t)componentCount * componentSize;
	const size_t viewOffset  = (size_t)bufferView.value( "byteOffset", 0 );
	const size_t accessorOffset = (size_t)accessor.value( "byteOffset", 0 );
	const size_t stride = (size_t)bufferView.value( "byteStride", (int)elementSize );
	const size_t absoluteOffset = viewOffset + accessorOffset;
	const std::vector<unsigned char>& bufferData = buffers[bufferIndex];
	const size_t requiredSize = outCount > 0 ? ( outCount - 1 ) * stride + elementSize : 0;
	if( absoluteOffset > bufferData.size() || stride < elementSize ||
	    absoluteOffset + requiredSize > bufferData.size() )
	{
		outError = "glTF accessor offset exceeds buffer size.";
		return false;
	}

	outData   = bufferData.data() + absoluteOffset;
	outStride = stride;
	outCount  = (size_t)accessor.value( "count", 0 );
	return true;
}

bool ReadSparseIndices( const json& doc, const std::vector<std::vector<unsigned char>>& buffers,
                        const json& sparse, std::vector<unsigned int>& outValues, std::string& outError )
{
	if( !sparse.is_object() )
	{
		outError = "Invalid glTF sparse accessor.";
		return false;
	}

	const size_t count = (size_t)sparse.value( "count", 0 );
	outValues.resize( count );
	if( count == 0 )
		return true;

	if( !sparse.contains( "indices" ) || !sparse["indices"].is_object() )
	{
		outError = "glTF sparse accessor is missing indices.";
		return false;
	}

	const json& indices = sparse["indices"];
	const int bufferViewIndex = indices.value( "bufferView", -1 );
	if( !doc.contains( "bufferViews" ) || !doc["bufferViews"].is_array() ||
	    bufferViewIndex < 0 || bufferViewIndex >= (int)doc["bufferViews"].size() )
	{
		outError = "Invalid glTF sparse index bufferView.";
		return false;
	}

	const json& bufferView = doc["bufferViews"][bufferViewIndex];
	const int bufferIndex = bufferView.value( "buffer", -1 );
	if( bufferIndex < 0 || bufferIndex >= (int)buffers.size() )
	{
		outError = "Invalid glTF sparse index buffer.";
		return false;
	}

	const int componentType = indices.value( "componentType", 0 );
	const size_t componentSize = ComponentTypeSize( componentType );
	if( componentSize == 0 || componentType == GLTF_COMPONENT_FLOAT )
	{
		outError = "Unsupported glTF sparse index component type.";
		return false;
	}

	const size_t stride = (size_t)bufferView.value( "byteStride", (int)componentSize );
	const size_t offset = (size_t)bufferView.value( "byteOffset", 0 ) + (size_t)indices.value( "byteOffset", 0 );
	const size_t requiredSize = ( count - 1 ) * stride + componentSize;
	const std::vector<unsigned char>& bufferData = buffers[bufferIndex];
	if( offset > bufferData.size() || stride < componentSize || offset + requiredSize > bufferData.size() )
	{
		outError = "glTF sparse index accessor exceeds buffer size.";
		return false;
	}

	const unsigned char* data = bufferData.data() + offset;
	for( size_t i = 0; i < count; ++i )
	{
		if( componentType == GLTF_COMPONENT_UNSIGNED_BYTE )
			outValues[i] = data[i * stride];
		else if( componentType == GLTF_COMPONENT_UNSIGNED_SHORT )
		{
			unsigned short value = 0;
			memcpy( &value, data + i * stride, sizeof( value ) );
			outValues[i] = (unsigned int)value;
		}
		else
		{
			unsigned int value = 0;
			memcpy( &value, data + i * stride, sizeof( value ) );
			outValues[i] = value;
		}
	}

	return true;
}

bool ReadSparseVec3Values( const json& doc, const std::vector<std::vector<unsigned char>>& buffers,
                           const json& sparse, std::vector<glm::vec3>& outValues, std::string& outError )
{
	if( !sparse.is_object() )
	{
		outError = "Invalid glTF sparse accessor.";
		return false;
	}

	const size_t count = (size_t)sparse.value( "count", 0 );
	outValues.resize( count );
	if( count == 0 )
		return true;

	if( !sparse.contains( "values" ) || !sparse["values"].is_object() )
	{
		outError = "glTF sparse accessor is missing values.";
		return false;
	}

	const json& values = sparse["values"];
	const int bufferViewIndex = values.value( "bufferView", -1 );
	if( !doc.contains( "bufferViews" ) || !doc["bufferViews"].is_array() ||
	    bufferViewIndex < 0 || bufferViewIndex >= (int)doc["bufferViews"].size() )
	{
		outError = "Invalid glTF sparse value bufferView.";
		return false;
	}

	const json& bufferView = doc["bufferViews"][bufferViewIndex];
	const int bufferIndex = bufferView.value( "buffer", -1 );
	if( bufferIndex < 0 || bufferIndex >= (int)buffers.size() )
	{
		outError = "Invalid glTF sparse value buffer.";
		return false;
	}

	const size_t elementSize = sizeof( float ) * 3;
	const size_t stride = (size_t)bufferView.value( "byteStride", (int)elementSize );
	const size_t offset = (size_t)bufferView.value( "byteOffset", 0 ) + (size_t)values.value( "byteOffset", 0 );
	const size_t requiredSize = ( count - 1 ) * stride + elementSize;
	const std::vector<unsigned char>& bufferData = buffers[bufferIndex];
	if( offset > bufferData.size() || stride < elementSize || offset + requiredSize > bufferData.size() )
	{
		outError = "glTF sparse value accessor exceeds buffer size.";
		return false;
	}

	const unsigned char* data = bufferData.data() + offset;
	for( size_t i = 0; i < count; ++i )
	{
		float xyz[3] = {};
		memcpy( xyz, data + i * stride, sizeof( xyz ) );
		outValues[i] = glm::vec3( xyz[0], xyz[1], xyz[2] );
	}

	return true;
}

bool ReadAccessorVec3( const json& doc, const std::vector<std::vector<unsigned char>>& buffers,
                       int accessorIndex, std::vector<glm::vec3>& outValues, std::string& outError )
{
	if( !doc.contains( "accessors" ) || !doc["accessors"].is_array() ||
	    accessorIndex < 0 || accessorIndex >= (int)doc["accessors"].size() )
	{
		outError = "Invalid glTF accessor index.";
		return false;
	}

	const json& accessor = doc["accessors"][accessorIndex];
	if( accessor.value( "componentType", 0 ) != GLTF_COMPONENT_FLOAT )
	{
		outError = "Unexpected glTF accessor component type.";
		return false;
	}
	if( accessor.value( "type", std::string() ) != "VEC3" )
	{
		outError = "Unexpected glTF accessor type.";
		return false;
	}

	const size_t count = (size_t)accessor.value( "count", 0 );
	outValues.assign( count, glm::vec3( 0.f ) );
	if( count == 0 )
		return true;

	if( accessor.contains( "bufferView" ) )
	{
		const unsigned char* data = nullptr;
		size_t stride = 0;
		size_t denseCount = 0;
		if( !ResolveAccessorView( doc, buffers, accessorIndex, GLTF_COMPONENT_FLOAT, "VEC3",
		                          data, stride, denseCount, outError ) )
			return false;

		outValues.resize( denseCount );
		for( size_t i = 0; i < denseCount; ++i )
		{
			float xyz[3] = {};
			memcpy( xyz, data + i * stride, sizeof( xyz ) );
			outValues[i] = glm::vec3( xyz[0], xyz[1], xyz[2] );
		}
	}
	else if( !accessor.contains( "sparse" ) )
	{
		outError = "glTF accessor is missing a bufferView.";
		return false;
	}

	if( accessor.contains( "sparse" ) )
	{
		std::vector<unsigned int> sparseIndices;
		std::vector<glm::vec3> sparseValues;
		if( !ReadSparseIndices( doc, buffers, accessor["sparse"], sparseIndices, outError ) )
			return false;
		if( !ReadSparseVec3Values( doc, buffers, accessor["sparse"], sparseValues, outError ) )
			return false;
		if( sparseIndices.size() != sparseValues.size() )
		{
			outError = "glTF sparse accessor index/value count mismatch.";
			return false;
		}

		for( size_t i = 0; i < sparseIndices.size(); ++i )
		{
			if( sparseIndices[i] >= outValues.size() )
			{
				outError = "glTF sparse accessor index exceeds element count.";
				return false;
			}
			outValues[sparseIndices[i]] = sparseValues[i];
		}
	}

	return true;
}

bool ReadAccessorIndices( const json& doc, const std::vector<std::vector<unsigned char>>& buffers,
                          int accessorIndex, std::vector<unsigned int>& outValues, std::string& outError )
{
	if( !doc.contains( "accessors" ) || !doc["accessors"].is_array() ||
	    accessorIndex < 0 || accessorIndex >= (int)doc["accessors"].size() )
	{
		outError = "Invalid glTF index accessor.";
		return false;
	}

	const json& accessor = doc["accessors"][accessorIndex];
	if( accessor.contains( "sparse" ) )
	{
		outError = "Sparse glTF index accessors are not supported.";
		return false;
	}
	if( accessor.value( "type", std::string() ) != "SCALAR" )
	{
		outError = "glTF index accessor is not scalar.";
		return false;
	}
	if( !accessor.contains( "bufferView" ) )
	{
		outError = "glTF index accessor is missing a bufferView.";
		return false;
	}

	const int bufferViewIndex = accessor["bufferView"].get<int>();
	if( !doc.contains( "bufferViews" ) || !doc["bufferViews"].is_array() ||
	    bufferViewIndex < 0 || bufferViewIndex >= (int)doc["bufferViews"].size() )
	{
		outError = "Invalid glTF index bufferView.";
		return false;
	}

	const json& bufferView = doc["bufferViews"][bufferViewIndex];
	const int bufferIndex = bufferView.value( "buffer", -1 );
	if( bufferIndex < 0 || bufferIndex >= (int)buffers.size() )
	{
		outError = "Invalid glTF index buffer.";
		return false;
	}

	const int componentType = accessor.value( "componentType", 0 );
	const size_t componentSize = ComponentTypeSize( componentType );
	if( componentSize == 0 || componentType == GLTF_COMPONENT_FLOAT )
	{
		outError = "Unsupported glTF index component type.";
		return false;
	}

	const size_t count  = (size_t)accessor.value( "count", 0 );
	const size_t stride = (size_t)bufferView.value( "byteStride", (int)componentSize );
	const size_t offset = (size_t)bufferView.value( "byteOffset", 0 ) + (size_t)accessor.value( "byteOffset", 0 );
	const std::vector<unsigned char>& bufferData = buffers[bufferIndex];
	const size_t requiredSize = count > 0 ? ( count - 1 ) * stride + componentSize : 0;
	if( offset > bufferData.size() || stride < componentSize || offset + requiredSize > bufferData.size() )
	{
		outError = "glTF index accessor offset exceeds buffer size.";
		return false;
	}

	outValues.resize( count );
	const unsigned char* data = bufferData.data() + offset;
	for( size_t i = 0; i < count; ++i )
	{
		if( componentType == GLTF_COMPONENT_UNSIGNED_BYTE )
			outValues[i] = data[i * stride];
		else if( componentType == GLTF_COMPONENT_UNSIGNED_SHORT )
		{
			unsigned short value = 0;
			memcpy( &value, data + i * stride, sizeof( value ) );
			outValues[i] = (unsigned int)value;
		}
		else
		{
			unsigned int value = 0;
			memcpy( &value, data + i * stride, sizeof( value ) );
			outValues[i] = value;
		}
	}
	return true;
}

void ApplyTransformToPositions( std::vector<glm::vec3>& values, const glm::mat4& transform )
{
	for( size_t i = 0; i < values.size(); ++i )
		values[i] = glm::vec3( transform * glm::vec4( values[i], 1.f ) );
}

void ApplyTransformToDeltas( std::vector<glm::vec3>& values, const glm::mat3& transform )
{
	for( size_t i = 0; i < values.size(); ++i )
		values[i] = transform * values[i];
}

bool LoadGltfModel( const std::string& path, FaceModelData& outData, std::string& outError )
{
	json doc;
	std::vector<unsigned char> glbBin;
	if( !LoadGltfDocument( path, doc, glbBin, outError ) )
		return false;

	std::vector<std::vector<unsigned char>> buffers;
	if( !LoadGltfBuffers( doc, glbBin, DirectoryName( path ), buffers, outError ) )
		return false;

	std::vector<MeshInstance> meshInstances;
	if( !CollectMeshInstances( doc, meshInstances ) )
	{
		outError = "No glTF meshes were found.";
		return false;
	}

	outData = FaceModelData();
	outData.sourcePath   = path;
	outData.sourceFormat = "glTF / GLB";
	outData.expressionDeltas.assign( NUM_EXPRESSIONS, std::vector<glm::vec3>() );

	bool slotMapped[NUM_EXPRESSIONS] = {};
	bool anyPrimitiveLoaded          = false;
	bool anyMorphTargets             = false;
	bool anyMorphTargetNames         = false;
	bool warnedShortTargetNames      = false;
	std::map<std::string, int> componentNameCounts;

	for( size_t meshInstanceIndex = 0; meshInstanceIndex < meshInstances.size(); ++meshInstanceIndex )
	{
		const MeshInstance& instance = meshInstances[meshInstanceIndex];
		if( !doc.contains( "meshes" ) || !doc["meshes"].is_array() ||
		    instance.meshIndex < 0 || instance.meshIndex >= (int)doc["meshes"].size() )
			continue;

		const json& mesh = doc["meshes"][instance.meshIndex];
		if( !mesh.contains( "primitives" ) || !mesh["primitives"].is_array() || mesh["primitives"].empty() )
		{
			outData.warnings.push_back( "Skipped an empty glTF mesh." );
			continue;
		}

		const std::vector<std::string> targetNames = ReadMeshTargetNames( mesh );
		const glm::mat3 deltaTransform = glm::mat3( instance.transform );

		for( size_t primitiveIndex = 0; primitiveIndex < mesh["primitives"].size(); ++primitiveIndex )
		{
			const json& primitive = mesh["primitives"][primitiveIndex];
			if( primitive.value( "mode", GLTF_MODE_TRIANGLES ) != GLTF_MODE_TRIANGLES )
			{
				outData.warnings.push_back( "Skipped a non-triangle glTF primitive." );
				continue;
			}
			if( !primitive.contains( "attributes" ) || !primitive["attributes"].is_object() ||
			    !primitive["attributes"].contains( "POSITION" ) )
			{
				outData.warnings.push_back( "Skipped a glTF primitive without POSITION data." );
				continue;
			}

			std::vector<glm::vec3> positions;
			if( !ReadAccessorVec3( doc, buffers, primitive["attributes"]["POSITION"].get<int>(), positions, outError ) )
				return false;
			if( positions.empty() )
				continue;

			ApplyTransformToPositions( positions, instance.transform );

			std::vector<unsigned int> indices;
			if( primitive.contains( "indices" ) )
			{
				if( !ReadAccessorIndices( doc, buffers, primitive["indices"].get<int>(), indices, outError ) )
					return false;
			}
			else
			{
				indices.resize( positions.size() );
				for( size_t i = 0; i < positions.size(); ++i )
					indices[i] = (unsigned int)i;
			}

			if( indices.empty() )
				continue;

			anyPrimitiveLoaded = true;

			const std::string defaultComponentName =
				"Mesh " + std::to_string( instance.meshIndex + 1 ) + " / Part " + std::to_string( primitiveIndex + 1 );
			FaceModelComponent component;
			component.name = MakeUniqueComponentName(
				BuildGltfComponentName( doc, instance, mesh, instance.meshIndex, primitive,
				                        primitiveIndex, mesh["primitives"].size() ),
				defaultComponentName, componentNameCounts
			);
			SetComponentBounds( component, positions );
			const size_t componentIndex = outData.components.size();
			outData.components.push_back( component );

			const size_t vertexOffset = outData.basePositions.size();
			outData.basePositions.insert( outData.basePositions.end(), positions.begin(), positions.end() );
			outData.submeshIds.insert( outData.submeshIds.end(), positions.size(), (float)componentIndex );
			for( int expressionIndex = 0; expressionIndex < NUM_EXPRESSIONS; ++expressionIndex )
				outData.expressionDeltas[expressionIndex].resize( outData.basePositions.size(), glm::vec3( 0.f ) );

			for( size_t i = 0; i < indices.size(); ++i )
			{
				const unsigned int absoluteIndex = (unsigned int)( indices[i] + vertexOffset );
				outData.indices.push_back( absoluteIndex );
				for( int level = 0; level < NUM_LEVELS; ++level )
					outData.components[componentIndex].levelFaces[level].push_back( absoluteIndex );
			}

			const size_t targetCount = primitive.contains( "targets" ) && primitive["targets"].is_array()
			                         ? primitive["targets"].size()
			                         : 0;
			if( targetCount == 0 )
				continue;

			anyMorphTargets = true;
			if( targetNames.empty() )
				continue;
			anyMorphTargetNames = true;
			if( targetNames.size() < targetCount && !warnedShortTargetNames )
			{
				outData.warnings.push_back(
					"glTF mesh targetNames is shorter than the primitive target list; unmapped targets were skipped."
				);
				warnedShortTargetNames = true;
			}

			std::vector<MorphTargetMatch> matches( targetCount );
			std::vector<int> bestTargetForSlot( NUM_EXPRESSIONS, -1 );
			std::vector<int> bestScoreForSlot( NUM_EXPRESSIONS, -1 );

			for( size_t targetIndex = 0; targetIndex < targetCount && targetIndex < targetNames.size(); ++targetIndex )
			{
				matches[targetIndex] = ResolveMorphTargetName( targetNames[targetIndex] );
				for( size_t j = 0; j < matches[targetIndex].indices.size(); ++j )
				{
					const int slot = matches[targetIndex].indices[j];
					if( matches[targetIndex].score > bestScoreForSlot[slot] )
					{
						bestScoreForSlot[slot] = matches[targetIndex].score;
						bestTargetForSlot[slot] = (int)targetIndex;
					}
				}
			}

			std::map<int, std::vector<glm::vec3>> targetCache;
			for( int slot = 0; slot < NUM_EXPRESSIONS; ++slot )
			{
				const int targetIndex = bestTargetForSlot[slot];
				if( targetIndex < 0 || targetIndex >= (int)targetCount )
					continue;

				if( !primitive["targets"][targetIndex].is_object() ||
				    !primitive["targets"][targetIndex].contains( "POSITION" ) )
					continue;

				std::vector<glm::vec3>& deltas = targetCache[targetIndex];
				if( deltas.empty() )
				{
					if( !ReadAccessorVec3( doc, buffers, primitive["targets"][targetIndex]["POSITION"].get<int>(), deltas, outError ) )
						return false;
					if( deltas.size() != positions.size() )
					{
						outError = "glTF morph target vertex count does not match base primitive.";
						return false;
					}
					ApplyTransformToDeltas( deltas, deltaTransform );
				}

				slotMapped[slot] = true;
				for( size_t vertex = 0; vertex < deltas.size(); ++vertex )
					outData.expressionDeltas[slot][vertexOffset + vertex] = deltas[vertex];
			}
		}
	}

	if( !anyPrimitiveLoaded || outData.basePositions.empty() || outData.indices.empty() )
	{
		outError = "No triangle primitives could be loaded from the glTF scene.";
		return false;
	}

	for( int level = 0; level < NUM_LEVELS; ++level )
		outData.levelFaces[level] = outData.indices;

	ComputeModelBounds( outData );
	ComputePreferredFocusPoint( outData );

	outData.identityDeltas.assign(
		NUM_IDENTITIES,
		std::vector<glm::vec3>( outData.basePositions.size(), glm::vec3( 0.f ) )
	);
	outData.supportsIdentity = false;

	std::vector<std::string> missingMorphs;
	for( int slot = 0; slot < NUM_EXPRESSIONS; ++slot )
	{
		if( slotMapped[slot] )
			++outData.mappedExpressionCount;
		else
			missingMorphs.push_back( GetFaceKitExpressionName( slot ) );
	}

	outData.statusText = "glTF / GLB | " + std::to_string( outData.basePositions.size() ) + " verts | " +
	                     std::to_string( outData.mappedExpressionCount ) + "/" + std::to_string( NUM_EXPRESSIONS ) +
	                     " blendshapes mapped | " + std::to_string( outData.components.size() ) + " components";

	if( anyMorphTargets && !anyMorphTargetNames )
	{
		outData.warnings.push_back(
			"glTF morph targets are present but mesh extras.targetNames is missing, so OSC blendshape mapping is unavailable."
		);
	}
	else if( !missingMorphs.empty() && anyMorphTargetNames )
	{
		outData.warnings.push_back(
			"Missing " + std::to_string( missingMorphs.size() ) + " canonical blendshapes in glTF: " +
			PreviewNames( missingMorphs, 6 )
		);
	}
	else if( !anyMorphTargets )
	{
		outData.warnings.push_back(
			"glTF mesh has no morph targets; OSC blendshape messages will not deform this model."
		);
	}
	outData.warnings.push_back( "glTF identity morphs are not supported; /facekit/identity messages are ignored." );

	return true;
}
}

const char* FaceModelFormatToString( FaceModelFormat format )
{
	switch( format )
	{
	case FaceModelFormat_IctFaceKitObj: return "ICT FaceKit OBJ";
	case FaceModelFormat_Gltf:          return "glTF / GLB";
	default:                           return "Auto";
	}
}

bool LoadFaceModel( const std::string& path, FaceModelFormat requestedFormat,
                    FaceModelData& outData, std::string& outError )
{
	outData = FaceModelData();
	outError.clear();

	const std::string trimmed = TrimTrailingSeparators( path );
	if( trimmed.empty() )
	{
		outError = "No model path set.";
		return false;
	}

	FaceModelFormat resolvedFormat = requestedFormat;
	if( resolvedFormat == FaceModelFormat_Auto )
	{
		const std::string extension = FileExtension( trimmed );
		if( extension == ".gltf" || extension == ".glb" )
			resolvedFormat = FaceModelFormat_Gltf;
		else
			resolvedFormat = FaceModelFormat_IctFaceKitObj;
	}

	if( resolvedFormat == FaceModelFormat_Gltf )
		return LoadGltfModel( trimmed, outData, outError );
	return LoadIctFaceKitObjModel( trimmed, outData, outError );
}
