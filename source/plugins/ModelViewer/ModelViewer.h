#pragma once
#include <FFGLSDK.h>
#include <FFGLSource.h>
#include <FFGLParamText.h>
#include <FFGLParamTrigger.h>
#include <string>
#include <vector>

using namespace ffglqs;

// ─────────────────────────────────────────────
//  Interleaved vertex layout sent to the GPU
// ─────────────────────────────────────────────
struct Vertex
{
    float position[3];
    float normal[3];
    float uv[2];
};

// ─────────────────────────────────────────────
//  Plugin class  —  v1.3
// ─────────────────────────────────────────────
class ModelViewer : public Source
{
public:
    ModelViewer();

    void     Clean() override;
    FFResult Render( ProcessOpenGLStruct* inputTextures ) override;

private:
    // ── GL resources ──────────────────────────
    GLuint vao          = 0;
    GLuint vbo          = 0;
    GLuint ebo          = 0;
    GLuint solidProgram = 0;
    GLuint wireProgram  = 0;

    // Own FBO with depth buffer
    GLuint fbo          = 0;
    GLuint colorTex     = 0;
    GLuint depthRbo     = 0;
    int    fboWidth     = 0;
    int    fboHeight    = 0;

    int    indexCount   = 0;
    float  elapsedTime  = 0.0f;
    bool   glReady      = false;
    std::string lastLoadedPath;  

    // ── Helpers ───────────────────────────────
    bool   loadOBJ( const std::string& path );
    void   unloadMesh();                        // frees VAO/VBO/EBO only
    GLuint compileProgram( const char* vert, const char* frag );
    void   ensureFBO( int w, int h );
};
