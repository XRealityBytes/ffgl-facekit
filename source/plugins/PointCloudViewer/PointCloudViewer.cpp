#define NOMINMAX
#include <windows.h>
#include "PointCloudViewer.h"
#include <FFGLParamText.h>
#include <FFGLParamTrigger.h>
#include <FFGLParamOption.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <fstream>
#include <sstream>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <limits>
#include <algorithm>

using namespace ffglqs;

// ════════════════════════════════════════════════════════════════════════════
//  GLSL
// ════════════════════════════════════════════════════════════════════════════

static const char* kVertSrc = R"GLSL(
#version 410 core

layout(location=0) in vec3 aPos;
layout(location=1) in vec3 aCol;

uniform mat4  uMVP;
uniform float uPointSize;
uniform int   uColorMode;  // 0=RGB  1=Depth  2=Height  3=Tint
uniform vec3  uTint;
uniform vec2  uZRange;     // (minZ, maxZ) in model space
uniform vec2  uYRange;     // (minY, maxY) in model space

out vec3  vCol;
out float vZ;

vec3 heat( float t ) {
    t = clamp( t, 0.0, 1.0 );
    return vec3(
        smoothstep( 0.3, 1.0, t ),
        sin( t * 3.14159 ),
        1.0 - smoothstep( 0.0, 0.6, t )
    );
}

void main()
{
    gl_Position  = uMVP * vec4( aPos, 1.0 );
    gl_PointSize = uPointSize;
    vZ           = aPos.z;

    if      ( uColorMode == 1 ) vCol = heat( ( aPos.z - uZRange.x ) / max( uZRange.y - uZRange.x, 1e-5 ) );
    else if ( uColorMode == 2 ) vCol = heat( ( aPos.y - uYRange.x ) / max( uYRange.y - uYRange.x, 1e-5 ) );
    else if ( uColorMode == 3 ) vCol = uTint;
    else                        vCol = aCol;
}
)GLSL";

static const char* kFragSrc = R"GLSL(
#version 410 core

in  vec3  vCol;
in  float vZ;

uniform float uClipNear;
uniform float uClipFar;
uniform int   uRound;

out vec4 fragColor;

void main()
{
    if ( vZ < uClipNear || vZ > uClipFar ) discard;
    if ( uRound == 1 ) {
        vec2 c = gl_PointCoord - 0.5;
        if ( dot( c, c ) > 0.25 ) discard;
    }
    fragColor = vec4( vCol, 1.0 );
}
)GLSL";

// ════════════════════════════════════════════════════════════════════════════
//  PLUGIN REGISTRATION
// ════════════════════════════════════════════════════════════════════════════

static PluginInstance p = Source::CreatePlugin<PointCloudViewer>( {
    "PC01",
    "Point Cloud Viewer"
} );

// ════════════════════════════════════════════════════════════════════════════
//  PLY PARSER  — ASCII + binary little-endian, XYZ + optional RGB
// ════════════════════════════════════════════════════════════════════════════

enum PlyFmt  { PLY_ASCII, PLY_BINARY_LE, PLY_BINARY_BE };
enum PlyType { PT_FLOAT, PT_DOUBLE, PT_UCHAR, PT_CHAR, PT_USHORT, PT_SHORT, PT_UINT, PT_INT, PT_LIST, PT_UNKNOWN };

static PlyType parsePlyType( const std::string& s )
{
    if ( s == "float"  || s == "float32" ) return PT_FLOAT;
    if ( s == "double" || s == "float64" ) return PT_DOUBLE;
    if ( s == "uchar"  || s == "uint8"  ) return PT_UCHAR;
    if ( s == "char"   || s == "int8"   ) return PT_CHAR;
    if ( s == "ushort" || s == "uint16" ) return PT_USHORT;
    if ( s == "short"  || s == "int16"  ) return PT_SHORT;
    if ( s == "uint"   || s == "uint32" ) return PT_UINT;
    if ( s == "int"    || s == "int32"  ) return PT_INT;
    return PT_UNKNOWN;
}

static int plyTypeSize( PlyType t )
{
    switch ( t ) {
    case PT_FLOAT:  return 4;
    case PT_DOUBLE: return 8;
    case PT_UCHAR:  case PT_CHAR:  return 1;
    case PT_USHORT: case PT_SHORT: return 2;
    case PT_UINT:   case PT_INT:   return 4;
    default:        return 0;
    }
}

static float plyReadFloat( const char* buf, PlyType t )
{
    switch ( t ) {
    case PT_FLOAT:  { float    v; memcpy( &v, buf, 4 ); return v; }
    case PT_DOUBLE: { double   v; memcpy( &v, buf, 8 ); return (float)v; }
    case PT_UCHAR:  return (unsigned char)buf[0] / 255.0f;
    case PT_CHAR:   return (signed   char)buf[0] / 127.0f;
    case PT_USHORT: { uint16_t v; memcpy( &v, buf, 2 ); return (float)v / 65535.0f; }
    case PT_SHORT:  { int16_t  v; memcpy( &v, buf, 2 ); return (float)v / 32767.0f; }
    case PT_UINT:   { uint32_t v; memcpy( &v, buf, 4 ); return (float)v; }
    case PT_INT:    { int32_t  v; memcpy( &v, buf, 4 ); return (float)v; }
    default:        return 0.0f;
    }
}

struct PlyPropDesc
{
    std::string name;
    PlyType     type   = PT_UNKNOWN;
    bool        isList = false;
    int         offset = 0;
};

struct PlyElemDesc
{
    std::string             name;
    int                     count  = 0;
    std::vector<PlyPropDesc> props;
    int                     stride = 0; // -1 = variable-length (has list props)
};

bool PointCloudViewer::loadPLY( const std::string& path )
{
    std::ifstream f( path, std::ios::binary );
    if ( !f.is_open() ) return false;

    // ── Header ───────────────────────────────────────────────────────────────
    PlyFmt fmt = PLY_ASCII;
    std::vector<PlyElemDesc> elems;
    PlyElemDesc* cur = nullptr;

    auto strip = []( std::string& s ) { if ( !s.empty() && s.back() == '\r' ) s.pop_back(); };

    std::string line;
    std::getline( f, line ); strip( line );
    if ( line != "ply" ) return false;

    bool headerDone = false;
    while ( std::getline( f, line ) )
    {
        strip( line );
        std::istringstream ss( line );
        std::string tok; ss >> tok;

        if ( tok == "format" )
        {
            std::string fs; ss >> fs;
            if      ( fs == "ascii"                ) fmt = PLY_ASCII;
            else if ( fs == "binary_little_endian" ) fmt = PLY_BINARY_LE;
            else if ( fs == "binary_big_endian"    ) fmt = PLY_BINARY_BE;
        }
        else if ( tok == "element" )
        {
            elems.push_back( {} );
            cur = &elems.back();
            ss >> cur->name >> cur->count;
        }
        else if ( tok == "property" && cur )
        {
            std::string typeStr; ss >> typeStr;
            PlyPropDesc prop;
            if ( typeStr == "list" )
            {
                prop.isList = true;
                prop.type   = PT_LIST;
                std::string dummy, nameStr;
                ss >> dummy >> dummy >> nameStr; // count_type, data_type, name
                prop.name = nameStr;
            }
            else
            {
                prop.type = parsePlyType( typeStr );
                ss >> prop.name;
            }
            cur->props.push_back( prop );
        }
        else if ( tok == "end_header" )
        {
            headerDone = true;
            break;
        }
    }

    if ( !headerDone ) return false;

    // ── Compute binary strides ────────────────────────────────────────────────
    for ( auto& elem : elems )
    {
        int off = 0;
        for ( auto& prop : elem.props )
        {
            prop.offset = off;
            if ( prop.isList ) { elem.stride = -1; break; }
            off += plyTypeSize( prop.type );
        }
        if ( elem.stride != -1 ) elem.stride = off;
    }

    // ── Find vertex element ───────────────────────────────────────────────────
    PlyElemDesc* vert = nullptr;
    for ( auto& elem : elems )
        if ( elem.name == "vertex" ) { vert = &elem; break; }
    if ( !vert || vert->count == 0 ) return false;

    // ── Map property names ────────────────────────────────────────────────────
    int xi=-1, yi=-1, zi=-1, ri=-1, gi=-1, bi=-1;
    for ( int i = 0; i < (int)vert->props.size(); ++i )
    {
        const auto& n = vert->props[i].name;
        if      ( n=="x"                  ) xi = i;
        else if ( n=="y"                  ) yi = i;
        else if ( n=="z"                  ) zi = i;
        else if ( n=="red"   || n=="r"    ) ri = i;
        else if ( n=="green" || n=="g"    ) gi = i;
        else if ( n=="blue"  || n=="b"    ) bi = i;
    }
    if ( xi < 0 || yi < 0 || zi < 0 ) return false;

    // ── Read vertices ─────────────────────────────────────────────────────────
    std::vector<PointVertex> pts;
    pts.reserve( vert->count );

    float minZ =  std::numeric_limits<float>::max(), maxZ = -std::numeric_limits<float>::max();
    float minY =  std::numeric_limits<float>::max(), maxY = -std::numeric_limits<float>::max();

    if ( fmt == PLY_ASCII )
    {
        // Skip elements before vertex
        for ( auto& elem : elems )
        {
            if ( &elem == vert ) break;
            for ( int i = 0; i < elem.count; ++i )
                std::getline( f, line );
        }

        for ( int i = 0; i < vert->count && std::getline( f, line ); ++i )
        {
            strip( line );
            std::istringstream vss( line );
            std::vector<double> vals( vert->props.size(), 0.0 );
            for ( auto& v : vals ) vss >> v;

            PointVertex pv;
            pv.x = (float)vals[xi];
            pv.y = (float)vals[yi];
            pv.z = (float)vals[zi];
            pv.r = ri >= 0 ? (float)vals[ri] : 1.f;
            pv.g = gi >= 0 ? (float)vals[gi] : 1.f;
            pv.b = bi >= 0 ? (float)vals[bi] : 1.f;

            // Normalise uchar colours stored as text integers (e.g. 255 → 1.0)
            if ( ri >= 0 && vert->props[ri].type == PT_UCHAR ) pv.r /= 255.f;
            if ( gi >= 0 && vert->props[gi].type == PT_UCHAR ) pv.g /= 255.f;
            if ( bi >= 0 && vert->props[bi].type == PT_UCHAR ) pv.b /= 255.f;

            if ( !std::isfinite(pv.x) || !std::isfinite(pv.y) || !std::isfinite(pv.z) ) continue;

            pts.push_back( pv );
            minZ = std::min( minZ, pv.z ); maxZ = std::max( maxZ, pv.z );
            minY = std::min( minY, pv.y ); maxY = std::max( maxY, pv.y );
        }
    }
    else if ( fmt == PLY_BINARY_LE )
    {
        // Skip elements before vertex
        for ( auto& elem : elems )
        {
            if ( &elem == vert ) break;
            if ( elem.stride < 0 ) return false; // can't skip variable-length element
            f.seekg( (std::streamoff)elem.count * elem.stride, std::ios::cur );
        }
        if ( vert->stride <= 0 ) return false;

        std::vector<char> buf( vert->stride );
        for ( int i = 0; i < vert->count; ++i )
        {
            f.read( buf.data(), vert->stride );
            if ( !f.good() ) break;

            PointVertex pv;
            pv.x = plyReadFloat( buf.data() + vert->props[xi].offset, vert->props[xi].type );
            pv.y = plyReadFloat( buf.data() + vert->props[yi].offset, vert->props[yi].type );
            pv.z = plyReadFloat( buf.data() + vert->props[zi].offset, vert->props[zi].type );
            pv.r = ri >= 0 ? plyReadFloat( buf.data() + vert->props[ri].offset, vert->props[ri].type ) : 1.f;
            pv.g = gi >= 0 ? plyReadFloat( buf.data() + vert->props[gi].offset, vert->props[gi].type ) : 1.f;
            pv.b = bi >= 0 ? plyReadFloat( buf.data() + vert->props[bi].offset, vert->props[bi].type ) : 1.f;

            if ( !std::isfinite(pv.x) || !std::isfinite(pv.y) || !std::isfinite(pv.z) ) continue;

            pts.push_back( pv );
            minZ = std::min( minZ, pv.z ); maxZ = std::max( maxZ, pv.z );
            minY = std::min( minY, pv.y ); maxY = std::max( maxY, pv.y );
        }
    }
    else
    {
        return false; // binary_big_endian not supported
    }

    if ( pts.empty() ) return false;

    bboxMinZ = minZ; bboxMaxZ = ( maxZ > minZ ) ? maxZ : minZ + 1.f;
    bboxMinY = minY; bboxMaxY = ( maxY > minY ) ? maxY : minY + 1.f;

    unloadCloud();
    glGenVertexArrays( 1, &vao );
    glGenBuffers( 1, &vbo );
    glBindVertexArray( vao );
    glBindBuffer( GL_ARRAY_BUFFER, vbo );
    glBufferData( GL_ARRAY_BUFFER, (GLsizeiptr)( pts.size() * sizeof(PointVertex) ), pts.data(), GL_STATIC_DRAW );
    glVertexAttribPointer( 0, 3, GL_FLOAT, GL_FALSE, sizeof(PointVertex), (void*)0 );
    glEnableVertexAttribArray( 0 );
    glVertexAttribPointer( 1, 3, GL_FLOAT, GL_FALSE, sizeof(PointVertex), (void*)(3*sizeof(float)) );
    glEnableVertexAttribArray( 1 );
    glBindVertexArray( 0 );
    glBindBuffer( GL_ARRAY_BUFFER, 0 );

    pointCount = (int)pts.size();
    return true;
}

// ════════════════════════════════════════════════════════════════════════════
//  CONSTRUCTOR
// ════════════════════════════════════════════════════════════════════════════

PointCloudViewer::PointCloudViewer()
{
    SetFragmentShader( "void main() { fragColor = vec4(0.0); }" );

    // ── File ─────────────────────────────────────────────────────────────────
    AddParam( ParamText::create( "plyPath", "" ) );
    AddParam( ParamTrigger::Create( "loadCloud" ) );

    // ── Transform ─────────────────────────────────────────────────────────────
    AddParam( Param::Create( "rotX",   0.5f ) );
    AddParam( Param::Create( "rotY",   0.5f ) );
    AddParam( Param::Create( "rotZ",   0.5f ) );
    AddParam( Param::Create( "scale",  0.2f ) );
    AddParam( Param::Create( "transX", 0.5f ) );
    AddParam( Param::Create( "transY", 0.5f ) );

    // ── Camera ────────────────────────────────────────────────────────────────
    AddParam( Param::Create( "fov",     0.45f ) );
    AddParam( Param::Create( "camDist", 0.22f ) );
    AddParam( Param::Create( "orbitX",  0.5f  ) );
    AddParam( Param::Create( "orbitY",  0.5f  ) );

    // ── Render ────────────────────────────────────────────────────────────────
    AddParam( Param::Create( "pointSize", 0.05f ) );
    AddParam( ParamOption::Create( "colorMode", {
        { "RGB",    0.f },
        { "Depth",  1.f },
        { "Height", 2.f },
        { "Tint",   3.f }
    } ) );
    AddParam( Param::Create( "tintR", 1.0f ) );
    AddParam( Param::Create( "tintG", 1.0f ) );
    AddParam( Param::Create( "tintB", 1.0f ) );
    AddParam( Param::Create( "clipNear", 0.0f ) );
    AddParam( Param::Create( "clipFar",  1.0f ) );
    AddParam( Param::Create( "roundPts", 1.0f ) );

    // ── Output ────────────────────────────────────────────────────────────────
    AddParam( Param::Create( "bgOpacity", 0.0f ) );
    AddParam( Param::Create( "bgR", 0.0f ) );
    AddParam( Param::Create( "bgG", 0.0f ) );
    AddParam( Param::Create( "bgB", 0.0f ) );

    // ── Groups ────────────────────────────────────────────────────────────────
    // File: 0,1  Transform: 2-7  Camera: 8-11  Render: 12-19  Output: 20-23
    for ( unsigned i : { 0u, 1u } )                                     SetParamGroup( i, "File" );
    for ( unsigned i : { 2u, 3u, 4u, 5u, 6u, 7u } )                    SetParamGroup( i, "Transform" );
    for ( unsigned i : { 8u, 9u, 10u, 11u } )                          SetParamGroup( i, "Camera" );
    for ( unsigned i : { 12u, 13u, 14u, 15u, 16u, 17u, 18u, 19u } )   SetParamGroup( i, "Render" );
    for ( unsigned i : { 20u, 21u, 22u, 23u } )                        SetParamGroup( i, "Output" );
}

// ════════════════════════════════════════════════════════════════════════════
//  HELPERS
// ════════════════════════════════════════════════════════════════════════════

void PointCloudViewer::unloadCloud()
{
    if ( vao ) { glDeleteVertexArrays( 1, &vao ); vao = 0; }
    if ( vbo ) { glDeleteBuffers(      1, &vbo ); vbo = 0; }
    pointCount = 0;
}

void PointCloudViewer::ensureFBO( int w, int h )
{
    if ( w == fboW && h == fboH ) return;
    if ( fbo      ) glDeleteFramebuffers(  1, &fbo      );
    if ( colorTex ) glDeleteTextures(      1, &colorTex );
    if ( depthRbo ) glDeleteRenderbuffers( 1, &depthRbo );

    fboW = w; fboH = h;

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

GLuint PointCloudViewer::compileProgram( const char* vertSrc, const char* fragSrc )
{
    auto compile = []( GLenum type, const char* src ) -> GLuint {
        GLuint s = glCreateShader( type );
        glShaderSource( s, 1, &src, nullptr );
        glCompileShader( s );
        GLint ok; glGetShaderiv( s, GL_COMPILE_STATUS, &ok );
        if ( !ok ) { glDeleteShader( s ); return 0u; }
        return s;
    };

    GLuint vs = compile( GL_VERTEX_SHADER,   vertSrc );
    GLuint fs = compile( GL_FRAGMENT_SHADER, fragSrc );
    if ( !vs || !fs ) return 0;

    GLuint prog = glCreateProgram();
    glAttachShader( prog, vs );
    glAttachShader( prog, fs );
    glLinkProgram(  prog );
    glDeleteShader( vs );
    glDeleteShader( fs );

    GLint ok; glGetProgramiv( prog, GL_LINK_STATUS, &ok );
    if ( !ok ) { glDeleteProgram( prog ); return 0; }
    return prog;
}

// ════════════════════════════════════════════════════════════════════════════
//  RENDER
// ════════════════════════════════════════════════════════════════════════════

static float remap( float v, float lo, float hi ) { return lo + v * ( hi - lo ); }

FFResult PointCloudViewer::Render( ProcessOpenGLStruct* pGL )
{
    // ── Lazy GL init ──────────────────────────────────────────────────────────
    if ( !glReady )
    {
        while ( glGetError() != GL_NO_ERROR ) {}
        program = compileProgram( kVertSrc, kFragSrc );
        if ( !program ) return FF_FAIL;
        glReady = true;
        while ( glGetError() != GL_NO_ERROR ) {}
    }

    // ── Load trigger / auto-load ──────────────────────────────────────────────
    auto pathParam    = std::dynamic_pointer_cast<ParamText>   ( GetParam( "plyPath"    ) );
    auto triggerParam = std::dynamic_pointer_cast<ParamTrigger>( GetParam( "loadCloud"  ) );

    bool triggered = triggerParam && triggerParam->GetValue() >= 1.f;
    if ( triggered ) triggerParam->Consume();

    bool autoLoad = pathParam && !pathParam->text.empty() && !vao;

    if ( pathParam && ( triggered || autoLoad ) && !pathParam->text.empty() )
        loadPLY( pathParam->text );

    // ── FBO ───────────────────────────────────────────────────────────────────
    GLint vp[4]; glGetIntegerv( GL_VIEWPORT, vp );
    const int W = vp[2], H = vp[3];
    ensureFBO( W, H );

    glBindFramebuffer( GL_FRAMEBUFFER, fbo );
    glViewport( 0, 0, W, H );
    glEnable( GL_DEPTH_TEST );
    glDepthFunc( GL_LEQUAL );

    float bgO = GetParam( "bgOpacity" )->GetValue();
    glClearColor( GetParam("bgR")->GetValue(), GetParam("bgG")->GetValue(), GetParam("bgB")->GetValue(), bgO );
    glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

    if ( !vao || pointCount == 0 )
    {
        glBindFramebuffer( GL_READ_FRAMEBUFFER, fbo );
        glBindFramebuffer( GL_DRAW_FRAMEBUFFER, pGL->HostFBO );
        glBlitFramebuffer( 0,0,W,H, 0,0,W,H, GL_COLOR_BUFFER_BIT, GL_NEAREST );
        glBindFramebuffer( GL_FRAMEBUFFER, pGL->HostFBO );
        glDisable( GL_DEPTH_TEST );
        glDepthFunc( GL_LESS );
        return FF_SUCCESS;
    }

    // ── Parameters ────────────────────────────────────────────────────────────
    float rotX   = remap( GetParam("rotX"  )->GetValue(), -180.f, 180.f );
    float rotY   = remap( GetParam("rotY"  )->GetValue(), -180.f, 180.f );
    float rotZ   = remap( GetParam("rotZ"  )->GetValue(), -180.f, 180.f );
    float scale  = remap( GetParam("scale" )->GetValue(),   0.01f,  5.f );
    float transX = remap( GetParam("transX")->GetValue(),    -2.f,   2.f );
    float transY = remap( GetParam("transY")->GetValue(),    -2.f,   2.f );

    float fov     = remap( GetParam("fov"    )->GetValue(),  10.f, 120.f );
    float camDist = remap( GetParam("camDist")->GetValue(),  0.5f,  20.f );
    float orbitX  = remap( GetParam("orbitX" )->GetValue(), -180.f, 180.f );
    float orbitY  = remap( GetParam("orbitY" )->GetValue(),-180.f, 180.f );

    float ptSize = remap( GetParam("pointSize")->GetValue(), 1.f, 20.f );
    float tintR  = GetParam("tintR")->GetValue();
    float tintG  = GetParam("tintG")->GetValue();
    float tintB  = GetParam("tintB")->GetValue();

    float clipNear = remap( GetParam("clipNear")->GetValue(), bboxMinZ, bboxMaxZ );
    float clipFar  = remap( GetParam("clipFar" )->GetValue(), bboxMinZ, bboxMaxZ );
    int   doRound  = GetParam("roundPts")->GetValue() > 0.5f ? 1 : 0;

    auto colorOpt = std::dynamic_pointer_cast<ParamOption>( GetParam("colorMode") );
    int  colorMode = colorOpt ? (int)colorOpt->GetValue() : 0;

    // ── Matrices ──────────────────────────────────────────────────────────────
    float aspect = H > 0 ? (float)W / H : 1.f;

    glm::mat4 model( 1.f );
    model = glm::translate( model, glm::vec3( transX, transY, 0.f ) );
    model = glm::rotate(    model, glm::radians(rotX), glm::vec3(1,0,0) );
    model = glm::rotate(    model, glm::radians(rotY), glm::vec3(0,1,0) );
    model = glm::rotate(    model, glm::radians(rotZ), glm::vec3(0,0,1) );
    model = glm::scale(     model, glm::vec3(scale) );

    float ox = glm::radians(orbitX), oy = glm::radians(orbitY);
    glm::vec3 camPos( camDist*cos(ox)*sin(oy), camDist*sin(ox), camDist*cos(ox)*cos(oy) );
    glm::vec3 up = cos(ox) >= 0.f ? glm::vec3(0,1,0) : glm::vec3(0,-1,0);

    glm::mat4 view = glm::lookAt( camPos, glm::vec3(0,0,0), up );
    glm::mat4 proj = glm::perspective( glm::radians(fov), aspect, 0.01f, 1000.f );
    glm::mat4 mvp  = proj * view * model;

    // ── Draw ──────────────────────────────────────────────────────────────────
    glEnable( GL_BLEND );
    glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
    glEnable( GL_PROGRAM_POINT_SIZE );

    glUseProgram( program );
    glUniformMatrix4fv( glGetUniformLocation(program,"uMVP"),       1, GL_FALSE, glm::value_ptr(mvp) );
    glUniform1f(  glGetUniformLocation(program,"uPointSize"), ptSize   );
    glUniform1i(  glGetUniformLocation(program,"uColorMode"), colorMode );
    glUniform3f(  glGetUniformLocation(program,"uTint"),      tintR, tintG, tintB );
    glUniform2f(  glGetUniformLocation(program,"uZRange"),    bboxMinZ, bboxMaxZ  );
    glUniform2f(  glGetUniformLocation(program,"uYRange"),    bboxMinY, bboxMaxY  );
    glUniform1f(  glGetUniformLocation(program,"uClipNear"),  clipNear );
    glUniform1f(  glGetUniformLocation(program,"uClipFar"),   clipFar  );
    glUniform1i(  glGetUniformLocation(program,"uRound"),     doRound  );

    glBindVertexArray( vao );
    glDrawArrays( GL_POINTS, 0, pointCount );
    glBindVertexArray( 0 );

    glDisable( GL_PROGRAM_POINT_SIZE );
    glDisable( GL_BLEND );

    // ── Blit to Resolume ──────────────────────────────────────────────────────
    glBindFramebuffer( GL_READ_FRAMEBUFFER, fbo );
    glBindFramebuffer( GL_DRAW_FRAMEBUFFER, pGL->HostFBO );
    glBlitFramebuffer( 0,0,W,H, 0,0,W,H, GL_COLOR_BUFFER_BIT, GL_NEAREST );

    // ── Restore GL state ──────────────────────────────────────────────────────
    glBindFramebuffer( GL_FRAMEBUFFER, pGL->HostFBO );
    glDisable( GL_DEPTH_TEST );
    glDepthFunc( GL_LESS );
    glBlendFuncSeparate( GL_ONE, GL_ZERO, GL_ONE, GL_ZERO );
    glBindVertexArray( 0 );
    glBindBuffer( GL_ARRAY_BUFFER, 0 );
    glUseProgram( 0 );

    GLint numUnits;
    glGetIntegerv( GL_MAX_TEXTURE_IMAGE_UNITS, &numUnits );
    for ( GLint i = 0; i < numUnits; ++i )
    {
        glActiveTexture( GL_TEXTURE0 + i );
        glBindTexture( GL_TEXTURE_2D, 0 );
    }
    glActiveTexture( GL_TEXTURE0 );
    while ( glGetError() != GL_NO_ERROR ) {}

    return FF_SUCCESS;
}

// ════════════════════════════════════════════════════════════════════════════
//  CLEAN
// ════════════════════════════════════════════════════════════════════════════

void PointCloudViewer::Clean()
{
    unloadCloud();
    if ( program  ) { glDeleteProgram(       program  ); program  = 0; }
    if ( fbo      ) { glDeleteFramebuffers(  1, &fbo      ); fbo      = 0; }
    if ( colorTex ) { glDeleteTextures(      1, &colorTex ); colorTex = 0; }
    if ( depthRbo ) { glDeleteRenderbuffers( 1, &depthRbo ); depthRbo = 0; }
}
