#pragma once
#include <vector>

#include "FFGLParamText.h"

namespace ffglqs
{
class ParamFile : public ParamText
{
public:
	static std::shared_ptr< ParamFile > create( std::string name, std::vector<std::string> supportedExtensions );
	static std::shared_ptr< ParamFile > create( std::string name, std::vector<std::string> supportedExtensions,
	                                            std::string text );

	ParamFile( std::string name, std::vector<std::string> supportedExtensions );
	ParamFile( std::string name, std::vector<std::string> supportedExtensions, std::string text );

	std::vector<std::string> supportedExtensions;
};

}//End namespace ffglqs
