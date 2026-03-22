#pragma once
#include <FFGLSDK.h>
#include <FFGLEffect.h>
#include <ffglex/FFGLScreenQuad.h>
#include <SpoutLibrary.h>
#include <string>

using namespace ffglqs;

// Internal Spout receiver helper — one instance per shared texture input.
struct SpoutIn
{
	SPOUTLIBRARY* sp  = nullptr;
	GLuint        tex = 0;
	unsigned int  w   = 0;
	unsigned int  h   = 0;
	std::string   name;

	bool receive( const std::string& senderName );
	void release();
};

class IRComposite : public Effect
{
public:
	IRComposite();

	void     Clean() override;
	FFResult Render( ProcessOpenGLStruct* pGL ) override;

private:
	SpoutIn              spIR;
	GLuint               program = 0;
	ffglex::FFGLScreenQuad quad;
	bool                 glReady = false;

	GLuint compileProgram( const char* vert, const char* frag );
};
