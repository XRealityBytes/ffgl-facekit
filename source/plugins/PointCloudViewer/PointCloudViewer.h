#pragma once
#include <FFGLSDK.h>
#include <FFGLSource.h>
#include <FFGLParamText.h>
#include <FFGLParamTrigger.h>
#include <string>
#include <vector>

using namespace ffglqs;

class PointCloudViewer : public Source
{
public:
	PointCloudViewer();

	void     Clean() override;
	FFResult Render( ProcessOpenGLStruct* pGL ) override;

private:
	struct PointVertex { float x, y, z, r, g, b; };

	GLuint program  = 0;
	GLuint vao      = 0;
	GLuint vbo      = 0;
	GLuint fbo      = 0;
	GLuint colorTex = 0;
	GLuint depthRbo = 0;
	int    fboW     = 0;
	int    fboH     = 0;
	int    pointCount = 0;
	bool   glReady  = false;

	// Bounding box computed at load time — used to remap clip params and colour gradients
	float bboxMinZ = 0.f, bboxMaxZ = 1.f;
	float bboxMinY = 0.f, bboxMaxY = 1.f;

	bool   loadPLY( const std::string& path );
	void   unloadCloud();
	GLuint compileProgram( const char* vert, const char* frag );
	void   ensureFBO( int w, int h );
};
