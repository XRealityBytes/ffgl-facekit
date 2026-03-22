#pragma once
#include <FFGLSDK.h>
#include <FFGLSource.h>
#include <FFGLParamOption.h>
#include <SpoutLibrary.h>
#include <string>
#include <vector>

using namespace ffglqs;

// Internal Spout receiver helper — one instance per shared texture input.
struct SpoutIn
{
	SPOUTLIBRARY* sp  = nullptr;
	GLuint        tex = 0;
	unsigned int  w   = 0;
	unsigned int  h   = 0;
	std::string   name;

	// Call every frame with the desired sender name.
	// Returns true when a valid texture is ready.
	bool receive( const std::string& senderName );
	void release();
};

class DepthRGBCloud : public Source
{
public:
	DepthRGBCloud();

	void     Clean() override;
	FFResult Render( ProcessOpenGLStruct* pGL ) override;

private:
	SpoutIn spDepth;
	SpoutIn spRGB;

	// Sender enumeration (separate lightweight instance, no GL needed)
	SPOUTLIBRARY*            spEnum    = nullptr;
	int                      enumFrame = 0;
	std::vector<std::string> senderList{ "(none)" }; // mirrors current dropdown options
	void refreshSenders(); // called every ~60 frames

	// Grid VBO — one vertex per depth pixel, stores UV coords
	GLuint gridVAO   = 0;
	GLuint gridVBO   = 0;
	int    gridCount = 0;
	int    lastGridW = 0;
	int    lastGridH = 0;

	// Own FBO with depth buffer
	GLuint fbo      = 0;
	GLuint colorTex = 0;
	GLuint depthRbo = 0;
	int    fboW     = 0;
	int    fboH     = 0;

	GLuint program = 0;
	bool   glReady = false;

	void   buildGrid( int w, int h );
	void   unloadGrid();
	void   ensureFBO( int w, int h );
	GLuint compileProgram( const char* vert, const char* frag );
};
