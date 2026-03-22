#pragma once

#include <string>
#include <vector>

static const int NUM_EXPRESSIONS = 53;
static const int NUM_IDENTITIES  = 100;

struct MorphTargetMatch
{
	std::vector<int> indices;
	int              score = 0;
};

extern const char* const FACEKIT_EXPRESSION_NAMES[NUM_EXPRESSIONS];

const char*        GetFaceKitExpressionName( int index );
std::string        NormalizeMorphTargetName( const std::string& name );
MorphTargetMatch   ResolveMorphTargetName( const std::string& name );
