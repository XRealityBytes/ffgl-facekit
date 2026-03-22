#include "FaceMorphTargets.h"

#include <algorithm>
#include <cctype>

const char* const FACEKIT_EXPRESSION_NAMES[NUM_EXPRESSIONS] = {
	"browDown_L",       "browDown_R",       "browInnerUp_L",    "browInnerUp_R",
	"browOuterUp_L",    "browOuterUp_R",    "cheekPuff_L",      "cheekPuff_R",
	"cheekSquint_L",    "cheekSquint_R",    "eyeBlink_L",       "eyeBlink_R",
	"eyeLookDown_L",    "eyeLookDown_R",    "eyeLookIn_L",      "eyeLookIn_R",
	"eyeLookOut_L",     "eyeLookOut_R",     "eyeLookUp_L",      "eyeLookUp_R",
	"eyeSquint_L",      "eyeSquint_R",      "eyeWide_L",        "eyeWide_R",
	"jawForward",       "jawLeft",          "jawOpen",          "jawRight",
	"mouthClose",       "mouthDimple_L",    "mouthDimple_R",    "mouthFrown_L",
	"mouthFrown_R",     "mouthFunnel",      "mouthLeft",        "mouthLowerDown_L",
	"mouthLowerDown_R", "mouthPress_L",     "mouthPress_R",     "mouthPucker",
	"mouthRight",       "mouthRollLower",   "mouthRollUpper",   "mouthShrugLower",
	"mouthShrugUpper",  "mouthSmile_L",     "mouthSmile_R",     "mouthStretch_L",
	"mouthStretch_R",   "mouthUpperUp_L",   "mouthUpperUp_R",   "noseSneer_L",
	"noseSneer_R"
};

namespace
{
struct PairAlias
{
	const char* name;
	int         leftIndex;
	int         rightIndex;
};

const PairAlias PAIR_ALIASES[] = {
	{ "browdown",      0,  1  },
	{ "browinnerup",   2,  3  },
	{ "browouterup",   4,  5  },
	{ "cheekpuff",     6,  7  },
	{ "cheeksquint",   8,  9  },
	{ "eyeblink",      10, 11 },
	{ "eyelookdown",   12, 13 },
	{ "eyelookin",     14, 15 },
	{ "eyelookout",    16, 17 },
	{ "eyelookup",     18, 19 },
	{ "eyesquint",     20, 21 },
	{ "eyewide",       22, 23 },
	{ "mouthdimple",   29, 30 },
	{ "mouthfrown",    31, 32 },
	{ "mouthlowerdown",35, 36 },
	{ "mouthpress",    37, 38 },
	{ "mouthsmile",    45, 46 },
	{ "mouthstretch",  47, 48 },
	{ "mouthupperup",  49, 50 },
	{ "nosesneer",     51, 52 },
};
}

const char* GetFaceKitExpressionName( int index )
{
	if( index < 0 || index >= NUM_EXPRESSIONS )
		return "";
	return FACEKIT_EXPRESSION_NAMES[index];
}

std::string NormalizeMorphTargetName( const std::string& name )
{
	std::string lowered;
	lowered.reserve( name.size() );
	for( size_t i = 0; i < name.size(); ++i )
		lowered.push_back( (char)std::tolower( (unsigned char)name[i] ) );

	auto replaceAll = []( std::string& input, const std::string& from, const std::string& to )
	{
		size_t pos = 0;
		while( ( pos = input.find( from, pos ) ) != std::string::npos )
		{
			input.replace( pos, from.size(), to );
			pos += to.size();
		}
	};

	replaceAll( lowered, "left",  "l" );
	replaceAll( lowered, "right", "r" );

	std::string normalized;
	normalized.reserve( lowered.size() );
	for( size_t i = 0; i < lowered.size(); ++i )
	{
		const unsigned char ch = (unsigned char)lowered[i];
		if( std::isalnum( ch ) )
			normalized.push_back( (char)ch );
	}
	return normalized;
}

MorphTargetMatch ResolveMorphTargetName( const std::string& name )
{
	MorphTargetMatch match;
	const std::string normalized = NormalizeMorphTargetName( name );
	if( normalized.empty() )
		return match;

	for( int i = 0; i < NUM_EXPRESSIONS; ++i )
	{
		if( normalized == NormalizeMorphTargetName( FACEKIT_EXPRESSION_NAMES[i] ) )
		{
			match.indices.push_back( i );
			match.score = 3;
			return match;
		}
	}

	for( size_t i = 0; i < sizeof( PAIR_ALIASES ) / sizeof( PAIR_ALIASES[0] ); ++i )
	{
		if( normalized == PAIR_ALIASES[i].name )
		{
			match.indices.push_back( PAIR_ALIASES[i].leftIndex );
			match.indices.push_back( PAIR_ALIASES[i].rightIndex );
			match.score = 1;
			return match;
		}
	}

	return match;
}
