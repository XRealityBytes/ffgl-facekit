#include "FFGLParamFile.h"

namespace ffglqs
{
std::shared_ptr< ParamFile > ParamFile::create( std::string name, std::vector<std::string> supportedExtensions )
{
	return create( std::move( name ), std::move( supportedExtensions ), "" );
}

std::shared_ptr< ParamFile > ParamFile::create( std::string name, std::vector<std::string> supportedExtensions,
                                                std::string text )
{
	return std::make_shared< ParamFile >( std::move( name ), std::move( supportedExtensions ), std::move( text ) );
}

ParamFile::ParamFile( std::string name, std::vector<std::string> supportedExtensions ) :
	ParamFile( std::move( name ), std::move( supportedExtensions ), "" )
{
}

ParamFile::ParamFile( std::string name, std::vector<std::string> supportedExtensions, std::string text ) :
	ParamText( std::move( name ), std::move( text ) ),
	supportedExtensions( std::move( supportedExtensions ) )
{
	type = FF_TYPE_FILE;
}

}//End namespace ffglqs
