#define NOMINMAX
#include <windows.h>
#include "ModelViewer.h"
#include <FFGLParamText.h>
#include <FFGLParamTrigger.h>
#include <FFGLParamOption.h>

// ── TinyOBJ (single-header, place in deps/) ─────────────────────────────────
#define TINYOBJLOADER_IMPLEMENTATION
#include "tiny_obj_loader.h"

// ── GLM (header-only, place in deps/glm/) ───────────────────────────────────
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cmath>
#include <cstring>

// ════════════════════════════════════════════════════════════════════════════
//  GLSL SHADERS
// ════════════════════════════════════════════════════════════════════════════

static const char* kVertSrc = R"GLSL(
#version 410 core

layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
layout(location = 2) in vec2 aUV;

uniform mat4 uModel;
uniform mat4 uView;
uniform mat4 uProjection;
uniform mat3 uNormalMatrix;

uniform float uExplodeAmount;
uniform float uWaveAmplitude;
uniform float uWaveFrequency;
uniform float uWaveSpeed;
uniform float uTime;

out vec3 vWorldPos;
out vec3 vNormal;
out vec2 vUV;

void main()
{
    vec3 pos = aPosition;

    if (uWaveAmplitude > 0.0)
        pos.y += sin(pos.x * uWaveFrequency + uTime * uWaveSpeed) * uWaveAmplitude;

    if (uExplodeAmount > 0.0)
        pos += aNormal * uExplodeAmount;

    vec4 worldPos4 = uModel * vec4(pos, 1.0);
    vWorldPos = worldPos4.xyz;
    vNormal   = normalize(uNormalMatrix * aNormal);
    vUV       = aUV;

    gl_Position = uProjection * uView * worldPos4;
}
)GLSL";

static const char* kFragSrc = R"GLSL(
#version 410 core

in vec3 vWorldPos;
in vec3 vNormal;
in vec2 vUV;

uniform vec3  uBaseColor;
uniform float uAmbient;
uniform float uSpecular;
uniform float uShininess;
uniform vec3  uLightDir;
uniform vec3  uLightColor;
uniform vec3  uCameraPos;
uniform float uFresnelIntensity;
uniform vec3  uFresnelColor;
uniform int   uShadingMode;
uniform vec3  uBgColor;
uniform float uBgOpacity;

out vec4 fragColor;

vec3 phong(vec3 N, vec3 L, vec3 V, vec3 baseCol)
{
    vec3 ambient  = uAmbient * baseCol;
    float diff    = max(dot(N, L), 0.0);
    vec3 diffuse  = diff * uLightColor * baseCol;
    vec3 H        = normalize(L + V);
    float spec    = pow(max(dot(N, H), 0.0), uShininess);
    vec3 specular = uSpecular * spec * uLightColor;
    return ambient + diffuse + specular;
}

void main()
{
    vec3 N = normalize(vNormal);
    vec3 V = normalize(uCameraPos - vWorldPos);
    vec3 L = normalize(uLightDir);
    vec3 col = uBaseColor;

    if (uShadingMode == 0) // Phong — fresnel only works in this mode
    {
        col = phong(N, L, V, uBaseColor);
        if (uFresnelIntensity > 0.0)
        {
            float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0) * uFresnelIntensity;
            col += rim * uFresnelColor;
        }
    }
    else if (uShadingMode == 1) // Flat
    {
        vec3 flatN = normalize(cross(dFdx(vWorldPos), dFdy(vWorldPos)));
        col = phong(flatN, L, V, uBaseColor);
    }
    else if (uShadingMode == 2) // Normals
    {
        col = N * 0.5 + 0.5;
    }
    // uShadingMode == 3 : Unlit — col stays as uBaseColor

    fragColor = vec4(col, 1.0);
}
)GLSL";

static const char* kWireVertSrc = R"GLSL(
#version 410 core
layout(location = 0) in vec3 aPosition;
layout(location = 1) in vec3 aNormal;
uniform mat4  uModel;
uniform mat4  uView;
uniform mat4  uProjection;
uniform float uExplodeAmount;
void main()
{
    vec3 pos    = aPosition + aNormal * uExplodeAmount;
    gl_Position = uProjection * uView * uModel * vec4(pos, 1.0);
}
)GLSL";

static const char* kWireFragSrc = R"GLSL(
#version 410 core
uniform vec3  uWireColor;
uniform float uWireMix;
out vec4 fragColor;
void main() { fragColor = vec4(uWireColor, uWireMix); }
)GLSL";

// ════════════════════════════════════════════════════════════════════════════
//  FFGL PLUGIN REGISTRATION  —  v1.2
// ════════════════════════════════════════════════════════════════════════════

static PluginInstance p = Source::CreatePlugin<ModelViewer>( {
    "MV01",
    "Model Viewer"
} );

// ════════════════════════════════════════════════════════════════════════════
//  CONSTRUCTOR
// ════════════════════════════════════════════════════════════════════════════

ModelViewer::ModelViewer()
{
    SetFragmentShader( "void main() { fragColor = vec4(0.0); }" );

    // ── File loading ───────────────────────────────────────────────────────
    AddParam( ParamText::create( "objPath",
        "C:/Users/Jimin/Documents/Resolume Arena/Extra Effects/icosahedron.obj" ) );
    AddParam( ParamTrigger::Create( "loadModel" ) );

    // ── Transform ─────────────────────────────────────────────────────────
    AddParam( Param::Create( "rotateX",    0.5f ) );
    AddParam( Param::Create( "rotateY",    0.5f ) );
    AddParam( Param::Create( "rotateZ",    0.5f ) );
    AddParam( Param::Create( "scale",      0.2f ) );
    AddParam( Param::Create( "translateX", 0.5f ) );
    AddParam( Param::Create( "translateY", 0.5f ) );

    // ── Camera ────────────────────────────────────────────────────────────
    AddParam( Param::Create( "fov",     0.45f ) );
    AddParam( Param::Create( "camDist", 0.22f ) );
    AddParam( Param::Create( "orbitX",  0.5f  ) );
    AddParam( Param::Create( "orbitY",  0.5f  ) );

    // ── Lighting ──────────────────────────────────────────────────────────
    AddParam( Param::Create( "lightDirX", 0.75f ) );
    AddParam( Param::Create( "lightDirY", 1.0f  ) );
    AddParam( Param::Create( "lightR",    1.0f  ) );
    AddParam( Param::Create( "lightG",    1.0f  ) );
    AddParam( Param::Create( "lightB",    1.0f  ) );
    AddParam( Param::Create( "ambient",   0.2f  ) );
    AddParam( Param::Create( "specular",  0.5f  ) );
    AddParam( Param::Create( "shininess", 0.24f ) );

    // ── Material ──────────────────────────────────────────────────────────
    AddParam( Param::Create( "baseR",     1.0f ) );
    AddParam( Param::Create( "baseG",     1.0f ) );
    AddParam( Param::Create( "baseB",     1.0f ) );
    AddParam( Param::Create( "wireframe", 0.0f ) );
    AddParam( Param::Create( "wireR",     0.0f ) );
    AddParam( Param::Create( "wireG",     1.0f ) );
    AddParam( Param::Create( "wireB",     0.0f ) );
    AddParam( Param::Create( "fresnel",   0.0f ) );
    AddParam( Param::Create( "fresnelR",  0.0f ) );
    AddParam( Param::Create( "fresnelG",  0.5f ) );
    AddParam( Param::Create( "fresnelB",  1.0f ) );

    // ── Vertex FX ─────────────────────────────────────────────────────────
    AddParam( Param::Create( "explode",   0.0f  ) );
    AddParam( Param::Create( "waveAmp",   0.0f  ) );
    AddParam( Param::Create( "waveFreq",  0.25f ) );
    AddParam( Param::Create( "waveSpeed", 0.2f  ) );

    // ── Shading mode dropdown ──────────────────────────────────────────────
    // Note: Fresnel rim only visible in Phong mode
    AddParam( ParamOption::Create( "shadingMode", {
        { "Phong",   0.f },
        { "Flat",    1.f },
        { "Normals", 2.f },
        { "Unlit",   3.f }
    } ) );

    // ── Blend mode dropdown ────────────────────────────────────────────────
    AddParam( ParamOption::Create( "blendMode", {
        { "Normal",   0.f },
        { "Additive", 1.f }
    } ) );

    // ── Output ────────────────────────────────────────────────────────────
    AddParam( Param::Create( "bgOpacity", 0.0f ) );
    AddParam( Param::Create( "bgR",       0.0f ) );
    AddParam( Param::Create( "bgG",       0.0f ) );
    AddParam( Param::Create( "bgB",       0.0f ) );
}

// ════════════════════════════════════════════════════════════════════════════
//  UNLOAD MESH
// ════════════════════════════════════════════════════════════════════════════

void ModelViewer::unloadMesh()
{
    if ( vao ) { glDeleteVertexArrays( 1, &vao ); vao = 0; }
    if ( vbo ) { glDeleteBuffers(      1, &vbo ); vbo = 0; }
    if ( ebo ) { glDeleteBuffers(      1, &ebo ); ebo = 0; }
    indexCount = 0;
}

// ════════════════════════════════════════════════════════════════════════════
//  LOAD OBJ
// ════════════════════════════════════════════════════════════════════════════

bool ModelViewer::loadOBJ( const std::string& path )
{
    tinyobj::attrib_t                attrib;
    std::vector<tinyobj::shape_t>    shapes;
    std::vector<tinyobj::material_t> materials;
    std::string warn, err;

    if ( !tinyobj::LoadObj( &attrib, &shapes, &materials, &warn, &err, path.c_str() ) )
        return false;

    std::vector<Vertex>       vertices;
    std::vector<unsigned int> indices;

    for ( auto& shape : shapes )
    {
        for ( auto& idx : shape.mesh.indices )
        {
            Vertex v{};
            v.position[0] = attrib.vertices[3 * idx.vertex_index + 0];
            v.position[1] = attrib.vertices[3 * idx.vertex_index + 1];
            v.position[2] = attrib.vertices[3 * idx.vertex_index + 2];

            if ( idx.normal_index >= 0 )
            {
                v.normal[0] = attrib.normals[3 * idx.normal_index + 0];
                v.normal[1] = attrib.normals[3 * idx.normal_index + 1];
                v.normal[2] = attrib.normals[3 * idx.normal_index + 2];
            }

            if ( idx.texcoord_index >= 0 )
            {
                v.uv[0] = attrib.texcoords[2 * idx.texcoord_index + 0];
                v.uv[1] = attrib.texcoords[2 * idx.texcoord_index + 1];
            }

            indices.push_back( static_cast<unsigned int>( vertices.size() ) );
            vertices.push_back( v );
        }
    }

    if ( vertices.empty() )
        return false;

    indexCount = static_cast<int>( indices.size() );

    glGenVertexArrays( 1, &vao );
    glGenBuffers( 1, &vbo );
    glGenBuffers( 1, &ebo );

    glBindVertexArray( vao );

    glBindBuffer( GL_ARRAY_BUFFER, vbo );
    glBufferData( GL_ARRAY_BUFFER, vertices.size() * sizeof(Vertex), vertices.data(), GL_STATIC_DRAW );

    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, ebo );
    glBufferData( GL_ELEMENT_ARRAY_BUFFER, indices.size() * sizeof(unsigned int), indices.data(), GL_STATIC_DRAW );

    glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, position) );
    glEnableVertexAttribArray( 0 );
    glVertexAttribPointer( 1, 3, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, normal) );
    glEnableVertexAttribArray( 1 );
    glVertexAttribPointer( 2, 2, GL_FLOAT, GL_FALSE, sizeof(Vertex), (void*)offsetof(Vertex, uv) );
    glEnableVertexAttribArray( 2 );

    glBindVertexArray( 0 );
    glBindBuffer( GL_ARRAY_BUFFER, 0 );
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );

    return true;
}

// ════════════════════════════════════════════════════════════════════════════
//  ENSURE FBO
// ════════════════════════════════════════════════════════════════════════════

void ModelViewer::ensureFBO( int w, int h )
{
    if ( w == fboWidth && h == fboHeight )
        return;

    if ( fbo      ) glDeleteFramebuffers(  1, &fbo      );
    if ( colorTex ) glDeleteTextures(      1, &colorTex );
    if ( depthRbo ) glDeleteRenderbuffers( 1, &depthRbo );

    fboWidth  = w;
    fboHeight = h;

    glGenTextures( 1, &colorTex );
    glBindTexture( GL_TEXTURE_2D, colorTex );
    glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
    glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
    glBindTexture( GL_TEXTURE_2D, 0 );

    glGenRenderbuffers( 1, &depthRbo );
    glBindRenderbuffer( GL_RENDERBUFFER, depthRbo );
    glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h );
    glBindRenderbuffer( GL_RENDERBUFFER, 0 );

    glGenFramebuffers( 1, &fbo );
    glBindFramebuffer( GL_FRAMEBUFFER, fbo );
    glFramebufferTexture2D(    GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,   colorTex, 0 );
    glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,  GL_RENDERBUFFER, depthRbo    );
    glBindFramebuffer( GL_FRAMEBUFFER, 0 );
}

// ════════════════════════════════════════════════════════════════════════════
//  RENDER — called every frame
// ════════════════════════════════════════════════════════════════════════════

static float remap( float v, float lo, float hi ) { return lo + v * ( hi - lo ); }

FFResult ModelViewer::Render( ProcessOpenGLStruct* inputTextures )
{
    // ── Lazy GL initialisation on first frame ──────────────────────────────
    if ( !glReady )
    {
        while ( glGetError() != GL_NO_ERROR ) {}

        solidProgram = compileProgram( kVertSrc,     kFragSrc     );
        wireProgram  = compileProgram( kWireVertSrc, kWireFragSrc );

        if ( !solidProgram || !wireProgram )
            return FF_FAIL;

        glReady = true;
        while ( glGetError() != GL_NO_ERROR ) {}
    }

    // ── Check load trigger or auto-load ───────────────────────────────────
    auto textParam    = std::dynamic_pointer_cast<ParamText>( GetParam( "objPath" ) );
    auto triggerParam = std::dynamic_pointer_cast<ParamTrigger>( GetParam( "loadModel" ) );

    bool triggered = triggerParam && triggerParam->GetValue() >= 1.0f;
    if ( triggered )
        triggerParam->Consume();

    bool autoLoad = textParam && !textParam->text.empty() && !vao;

    if ( textParam && ( triggered || autoLoad ) && !textParam->text.empty() )
    {
        unloadMesh();
        loadOBJ( textParam->text );
        lastLoadedPath = textParam->text;
    }

    // ── If no mesh loaded yet, output a clear frame ────────────────────────
    if ( !vao )
    {
        GLint viewport[4];
        glGetIntegerv( GL_VIEWPORT, viewport );
        ensureFBO( viewport[2], viewport[3] );
        glBindFramebuffer( GL_FRAMEBUFFER, fbo );
        glClearColor( 0.f, 0.f, 0.f, 0.f );
        glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );
        glBindFramebuffer( GL_READ_FRAMEBUFFER, fbo );
        glBindFramebuffer( GL_DRAW_FRAMEBUFFER, inputTextures->HostFBO );
        glBlitFramebuffer( 0, 0, viewport[2], viewport[3],
                           0, 0, viewport[2], viewport[3],
                           GL_COLOR_BUFFER_BIT, GL_NEAREST );
        glBindFramebuffer( GL_FRAMEBUFFER, inputTextures->HostFBO );
        return FF_SUCCESS;
    }

    elapsedTime += 1.0f / 60.0f;

    GLint viewport[4];
    glGetIntegerv( GL_VIEWPORT, viewport );
    const int W = viewport[2];
    const int H = viewport[3];
    ensureFBO( W, H );

    // ── Read and remap parameters ──────────────────────────────────────────
    float rotX    = remap( GetParam("rotateX"   )->GetValue(), -180.f, 180.f );
    float rotY    = remap( GetParam("rotateY"   )->GetValue(), -180.f, 180.f );
    float rotZ    = remap( GetParam("rotateZ"   )->GetValue(), -180.f, 180.f );
    float scale   = remap( GetParam("scale"     )->GetValue(),   0.01f,  5.f );
    float transX  = remap( GetParam("translateX")->GetValue(),  -2.f,   2.f  );
    float transY  = remap( GetParam("translateY")->GetValue(),  -2.f,   2.f  );

    float fov     = remap( GetParam("fov"    )->GetValue(),  10.f, 120.f );
    float camDist = remap( GetParam("camDist")->GetValue(),  0.5f,  20.f );
    float orbitX  = remap( GetParam("orbitX" )->GetValue(), -90.f,  90.f );
    float orbitY  = remap( GetParam("orbitY" )->GetValue(),-180.f, 180.f );

    float lightDX  = remap( GetParam("lightDirX")->GetValue(), -1.f, 1.f );
    float lightDY  = remap( GetParam("lightDirY")->GetValue(), -1.f, 1.f );
    float lightR   = GetParam("lightR")->GetValue();
    float lightG   = GetParam("lightG")->GetValue();
    float lightB   = GetParam("lightB")->GetValue();
    float ambient  = GetParam("ambient" )->GetValue();
    float specular = GetParam("specular")->GetValue();
    float shiny    = remap( GetParam("shininess")->GetValue(), 1.f, 128.f );

    float baseR    = GetParam("baseR")->GetValue();
    float baseG    = GetParam("baseG")->GetValue();
    float baseB    = GetParam("baseB")->GetValue();
    float wireMix  = GetParam("wireframe")->GetValue();
    float wireR    = GetParam("wireR")->GetValue();
    float wireG    = GetParam("wireG")->GetValue();
    float wireB    = GetParam("wireB")->GetValue();
    float fresnel  = remap( GetParam("fresnel")->GetValue(), 0.f, 3.f );
    float fresnelR = GetParam("fresnelR")->GetValue();
    float fresnelG = GetParam("fresnelG")->GetValue();
    float fresnelB = GetParam("fresnelB")->GetValue();

    float explode  = remap( GetParam("explode"  )->GetValue(), 0.f,  2.f  );
    float waveAmp  = GetParam("waveAmp" )->GetValue();
    float waveFreq = remap( GetParam("waveFreq" )->GetValue(), 0.f, 20.f );
    float waveSpd  = remap( GetParam("waveSpeed")->GetValue(), 0.f, 10.f );

    // ── Read dropdown params ───────────────────────────────────────────────
    // ParamOption::GetValue() returns the index as a float (0, 1, 2, 3...)
    // We cast to int to use as the shader mode integer
    auto shadingOpt = std::dynamic_pointer_cast<ParamOption>( GetParam( "shadingMode" ) );
    auto blendOpt   = std::dynamic_pointer_cast<ParamOption>( GetParam( "blendMode"   ) );

    int  shadingMode = shadingOpt ? (int)shadingOpt->GetValue() : 0;
    bool additive    = blendOpt   ? blendOpt->IsCurrentOption( "Additive" ) : false;

    float bgOpacity = GetParam("bgOpacity")->GetValue();
    float bgR = GetParam("bgR")->GetValue();
    float bgG = GetParam("bgG")->GetValue();
    float bgB = GetParam("bgB")->GetValue();

    // ── Build matrices ─────────────────────────────────────────────────────
    float aspect = ( H > 0 ) ? (float)W / (float)H : 1.f;

    glm::mat4 model = glm::mat4( 1.f );
    model = glm::translate( model, glm::vec3( transX, transY, 0.f ) );
    model = glm::rotate( model, glm::radians(rotX), glm::vec3(1,0,0) );
    model = glm::rotate( model, glm::radians(rotY), glm::vec3(0,1,0) );
    model = glm::rotate( model, glm::radians(rotZ), glm::vec3(0,0,1) );
    model = glm::scale(  model, glm::vec3(scale) );

    float ox = glm::radians(orbitX);
    float oy = glm::radians(orbitY);
    glm::vec3 camPos(
        camDist * cos(ox) * sin(oy),
        camDist * sin(ox),
        camDist * cos(ox) * cos(oy)
    );

    glm::mat4 view    = glm::lookAt( camPos, glm::vec3(0,0,0), glm::vec3(0,1,0) );
    glm::mat4 proj    = glm::perspective( glm::radians(fov), aspect, 0.01f, 1000.f );
    glm::mat3 normalMat = glm::mat3( glm::transpose( glm::inverse(model) ) );

    float lz = 1.f - std::abs(lightDX) - std::abs(lightDY);
    glm::vec3 lightDir = glm::normalize( glm::vec3(lightDX, lightDY, lz) );

    // ── Render into own FBO ────────────────────────────────────────────────
    glBindFramebuffer( GL_FRAMEBUFFER, fbo );
    glViewport( 0, 0, W, H );
    glEnable( GL_DEPTH_TEST );
    glDepthFunc( GL_LEQUAL );

    if ( additive )
        { glEnable(GL_BLEND); glBlendFunc(GL_ONE, GL_ONE); }
    else
        { glEnable(GL_BLEND); glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA); }

    glClearColor( bgR, bgG, bgB, bgOpacity );
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    // ── Pass 1: Solid mesh ─────────────────────────────────────────────────
    glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
    glUseProgram( solidProgram );

    glUniformMatrix4fv( glGetUniformLocation(solidProgram,"uModel"),        1, GL_FALSE, glm::value_ptr(model)     );
    glUniformMatrix4fv( glGetUniformLocation(solidProgram,"uView"),         1, GL_FALSE, glm::value_ptr(view)      );
    glUniformMatrix4fv( glGetUniformLocation(solidProgram,"uProjection"),   1, GL_FALSE, glm::value_ptr(proj)      );
    glUniformMatrix3fv( glGetUniformLocation(solidProgram,"uNormalMatrix"), 1, GL_FALSE, glm::value_ptr(normalMat) );

    glUniform3f( glGetUniformLocation(solidProgram,"uLightDir"),   lightDir.x, lightDir.y, lightDir.z );
    glUniform3f( glGetUniformLocation(solidProgram,"uLightColor"), lightR,     lightG,     lightB     );
    glUniform3f( glGetUniformLocation(solidProgram,"uCameraPos"),  camPos.x,   camPos.y,   camPos.z   );
    glUniform1f( glGetUniformLocation(solidProgram,"uAmbient"),    ambient  );
    glUniform1f( glGetUniformLocation(solidProgram,"uSpecular"),   specular );
    glUniform1f( glGetUniformLocation(solidProgram,"uShininess"),  shiny    );

    glUniform3f( glGetUniformLocation(solidProgram,"uBaseColor"),        baseR,    baseG,    baseB    );
    glUniform3f( glGetUniformLocation(solidProgram,"uFresnelColor"),     fresnelR, fresnelG, fresnelB );
    glUniform3f( glGetUniformLocation(solidProgram,"uBgColor"),          bgR,      bgG,      bgB      );
    glUniform1f( glGetUniformLocation(solidProgram,"uFresnelIntensity"), fresnel   );
    glUniform1f( glGetUniformLocation(solidProgram,"uBgOpacity"),        bgOpacity );

    glUniform1f( glGetUniformLocation(solidProgram,"uExplodeAmount"), explode     );
    glUniform1f( glGetUniformLocation(solidProgram,"uWaveAmplitude"), waveAmp     );
    glUniform1f( glGetUniformLocation(solidProgram,"uWaveFrequency"), waveFreq    );
    glUniform1f( glGetUniformLocation(solidProgram,"uWaveSpeed"),     waveSpd     );
    glUniform1f( glGetUniformLocation(solidProgram,"uTime"),          elapsedTime );
    glUniform1i( glGetUniformLocation(solidProgram,"uShadingMode"),   shadingMode );

    glBindVertexArray( vao );
    glDrawElements( GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr );

    // ── Pass 2: Wireframe overlay ──────────────────────────────────────────
    if ( wireMix > 0.001f )
    {
        glEnable( GL_POLYGON_OFFSET_LINE );
        glPolygonOffset( -1.f, -1.f );
        glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );
        glUseProgram( wireProgram );

        glUniformMatrix4fv( glGetUniformLocation(wireProgram,"uModel"),      1, GL_FALSE, glm::value_ptr(model) );
        glUniformMatrix4fv( glGetUniformLocation(wireProgram,"uView"),       1, GL_FALSE, glm::value_ptr(view)  );
        glUniformMatrix4fv( glGetUniformLocation(wireProgram,"uProjection"), 1, GL_FALSE, glm::value_ptr(proj)  );
        glUniform3f( glGetUniformLocation(wireProgram,"uWireColor"),     wireR, wireG, wireB );
        glUniform1f( glGetUniformLocation(wireProgram,"uWireMix"),       wireMix );
        glUniform1f( glGetUniformLocation(wireProgram,"uExplodeAmount"), explode );

        glDrawElements( GL_TRIANGLES, indexCount, GL_UNSIGNED_INT, nullptr );

        glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
        glDisable( GL_POLYGON_OFFSET_LINE );
    }

    glBindVertexArray( 0 );

    // ── Blit to Resolume's host FBO ────────────────────────────────────────
    glBindFramebuffer( GL_READ_FRAMEBUFFER, fbo );
    glBindFramebuffer( GL_DRAW_FRAMEBUFFER, inputTextures->HostFBO );
    glBlitFramebuffer( 0, 0, W, H, 0, 0, W, H, GL_COLOR_BUFFER_BIT, GL_NEAREST );

    // ── Restore GL state ───────────────────────────────────────────────────
    glBindFramebuffer( GL_FRAMEBUFFER, inputTextures->HostFBO );
    glDisable( GL_DEPTH_TEST );
    glDepthFunc( GL_LESS );
    glDisable( GL_BLEND );
    glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
    glBlendFuncSeparate( GL_ONE, GL_ZERO, GL_ONE, GL_ZERO );

    GLint numUnits;
    glGetIntegerv( GL_MAX_TEXTURE_IMAGE_UNITS, &numUnits );
    for ( GLint i = 0; i < numUnits; ++i )
    {
        glActiveTexture( GL_TEXTURE0 + i );
        glBindTexture( GL_TEXTURE_1D,                   0 );
        glBindTexture( GL_TEXTURE_2D,                   0 );
        glBindTexture( GL_TEXTURE_3D,                   0 );
        glBindTexture( GL_TEXTURE_1D_ARRAY,             0 );
        glBindTexture( GL_TEXTURE_2D_ARRAY,             0 );
        glBindTexture( GL_TEXTURE_RECTANGLE,            0 );
        glBindTexture( GL_TEXTURE_CUBE_MAP,             0 );
        glBindTexture( GL_TEXTURE_CUBE_MAP_ARRAY,       0 );
        glBindTexture( GL_TEXTURE_BUFFER,               0 );
        glBindTexture( GL_TEXTURE_2D_MULTISAMPLE,       0 );
        glBindTexture( GL_TEXTURE_2D_MULTISAMPLE_ARRAY, 0 );
    }
    glActiveTexture( GL_TEXTURE0 );
    glBindRenderbuffer( GL_RENDERBUFFER, 0 );
    glBindVertexArray( 0 );
    glBindBuffer( GL_ARRAY_BUFFER, 0 );
    glBindBuffer( GL_ELEMENT_ARRAY_BUFFER, 0 );
    glUseProgram( 0 );
    while ( glGetError() != GL_NO_ERROR ) {}

    return FF_SUCCESS;
}

// ════════════════════════════════════════════════════════════════════════════
//  CLEAN — free all GPU resources
// ════════════════════════════════════════════════════════════════════════════

void ModelViewer::Clean()
{
    unloadMesh();
    if ( solidProgram ) { glDeleteProgram(        solidProgram ); solidProgram = 0; }
    if ( wireProgram  ) { glDeleteProgram(        wireProgram  ); wireProgram  = 0; }
    if ( fbo          ) { glDeleteFramebuffers(  1, &fbo       ); fbo          = 0; }
    if ( colorTex     ) { glDeleteTextures(      1, &colorTex  ); colorTex     = 0; }
    if ( depthRbo     ) { glDeleteRenderbuffers( 1, &depthRbo  ); depthRbo     = 0; }
}

// ════════════════════════════════════════════════════════════════════════════
//  COMPILE SHADER PROGRAM
// ════════════════════════════════════════════════════════════════════════════

GLuint ModelViewer::compileProgram( const char* vertSrc, const char* fragSrc )
{
    auto compileShader = []( GLenum type, const char* src ) -> GLuint
    {
        GLuint shader = glCreateShader( type );
        glShaderSource( shader, 1, &src, nullptr );
        glCompileShader( shader );
        GLint ok;
        glGetShaderiv( shader, GL_COMPILE_STATUS, &ok );
        if ( !ok ) { glDeleteShader( shader ); return 0u; }
        return shader;
    };

    GLuint vert = compileShader( GL_VERTEX_SHADER,   vertSrc );
    GLuint frag = compileShader( GL_FRAGMENT_SHADER, fragSrc );
    if ( !vert || !frag ) return 0;

    GLuint prog = glCreateProgram();
    glAttachShader( prog, vert );
    glAttachShader( prog, frag );
    glLinkProgram(  prog );
    glDeleteShader( vert );
    glDeleteShader( frag );

    GLint ok;
    glGetProgramiv( prog, GL_LINK_STATUS, &ok );
    if ( !ok ) { glDeleteProgram( prog ); return 0; }
    return prog;
}
