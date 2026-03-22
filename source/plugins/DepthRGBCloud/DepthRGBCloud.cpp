#define NOMINMAX
#include <windows.h>
#include "DepthRGBCloud.h"
#include <FFGLParamOption.h>
#include <FFGLParamText.h>
#include <ffgl/FFGLLog.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <vector>
#include <cmath>
#include <cstring>

using namespace ffglqs;

// ════════════════════════════════════════════════════════════════════════════
//  SpoutIn implementation
// ════════════════════════════════════════════════════════════════════════════

bool SpoutIn::receive( const std::string& senderName )
{
	if ( senderName.empty() ) return false;

	// Lazy init
	if ( !sp ) {
		sp = GetSpout();
		if ( !sp ) {
			FFGLLog::LogToHost( ( "SpoutIn[" + senderName + "]: GetSpout() returned null" ).c_str() );
			return false;
		}
		FFGLLog::LogToHost( ( "SpoutIn[" + senderName + "]: GetSpout() OK" ).c_str() );
	}

	// Sender name changed — reconnect
	if ( senderName != name ) {
		name = senderName;
		sp->SetReceiverName( name.c_str() );
		if ( tex ) { glDeleteTextures( 1, &tex ); tex = 0; w = h = 0; }
	}

	// Create placeholder texture on first use
	if ( !tex ) {
		glGenTextures( 1, &tex );
		glBindTexture( GL_TEXTURE_2D, tex );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glBindTexture( GL_TEXTURE_2D, 0 );
	}

	if ( !sp->ReceiveTexture( tex, GL_TEXTURE_2D, false, 0 ) ) {
		static int failCount = 0;
		if ( ++failCount <= 5 )
			FFGLLog::LogToHost( ( "SpoutIn[" + name + "]: ReceiveTexture failed (connected=" +
			                      std::string( sp->IsConnected() ? "yes" : "no" ) + ")" ).c_str() );
		return false;
	}
	static bool rxLogged = false;
	if ( !rxLogged ) {
		FFGLLog::LogToHost( ( "SpoutIn[" + name + "]: ReceiveTexture succeeded" ).c_str() );
		rxLogged = true;
	}

	// Sender size change / new connection — recreate texture at correct size
	if ( sp->IsUpdated() ) {
		unsigned int nw = sp->GetSenderWidth();
		unsigned int nh = sp->GetSenderHeight();
		if ( nw > 0 && nh > 0 ) {
			glDeleteTextures( 1, &tex ); tex = 0;
			glGenTextures( 1, &tex );
			glBindTexture( GL_TEXTURE_2D, tex );
			glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, nw, nh, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
			glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
			glBindTexture( GL_TEXTURE_2D, 0 );
			w = nw; h = nh;
			sp->ReceiveTexture( tex, GL_TEXTURE_2D, false, 0 );
		}
	}

	return w > 0;
}

void SpoutIn::release()
{
	if ( tex ) { glDeleteTextures( 1, &tex ); tex = 0; }
	if ( sp )  { sp->Release(); sp = nullptr; }
	w = h = 0;
	name.clear();
}

// ════════════════════════════════════════════════════════════════════════════
//  GLSL
// ════════════════════════════════════════════════════════════════════════════

// Vertex shader — samples the depth texture at each grid UV, unprojects to
// 3D using pinhole camera intrinsics, colours from the RGB texture.
static const char* kVertSrc = R"GLSL(
#version 410 core

layout(location=0) in vec2 aUV;

uniform sampler2D uDepth;
uniform sampler2D uRGB;
uniform vec2      uMaxUVDepth;
uniform vec2      uMaxUVRGB;

uniform mat4  uMVP;
uniform float uPointSize;

// Camera intrinsics (pixels)
uniform float uFx, uFy, uCx, uCy;
uniform float uGridW, uGridH;

// Depth encoding: normalised 0-1 maps to 0..uDepthScale metres
uniform float uDepthScale;

uniform int   uColorMode;   // 0=RGB  1=Depth  2=Height  3=Tint
uniform vec3  uTint;
uniform float uZMax;        // = uDepthScale, for gradient normalisation
uniform float uYHalf;       // half-height of expected world range

out vec3  vCol;
out float vDepth;

vec3 heat( float t ) {
    t = clamp( t, 0.0, 1.0 );
    return vec3( smoothstep(0.3,1.0,t), sin(t*3.14159), 1.0-smoothstep(0.0,0.6,t) );
}

void main()
{
    float d   = texture( uDepth, aUV * uMaxUVDepth ).r * uDepthScale;
    float px  = aUV.x * uGridW;
    float py  = aUV.y * uGridH;

    vec3 pos  = vec3( (px - uCx)*d/uFx, (py - uCy)*d/uFy, d );

    gl_Position  = uMVP * vec4( pos, 1.0 );
    gl_PointSize = uPointSize;
    vDepth       = d;

    vec3 rgb = texture( uRGB, aUV * uMaxUVRGB ).rgb;

    if      ( uColorMode == 1 ) vCol = heat( d / max( uZMax, 1e-5 ) );
    else if ( uColorMode == 2 ) vCol = heat( (pos.y + uYHalf) / max( uYHalf * 2.0, 1e-5 ) );
    else if ( uColorMode == 3 ) vCol = uTint;
    else                        vCol = rgb;
}
)GLSL";

static const char* kFragSrc = R"GLSL(
#version 410 core

in  vec3  vCol;
in  float vDepth;

uniform float uMinDepth;
uniform float uClipNear;
uniform float uClipFar;
uniform int   uRound;

out vec4 fragColor;

void main()
{
    if ( vDepth < uMinDepth || vDepth < uClipNear || vDepth > uClipFar ) discard;
    if ( uRound == 1 ) {
        vec2 c = gl_PointCoord - 0.5;
        if ( dot(c,c) > 0.25 ) discard;
    }
    fragColor = vec4( vCol, 1.0 );
}
)GLSL";

// ════════════════════════════════════════════════════════════════════════════
//  PLUGIN REGISTRATION  (FF_SOURCE — receives depth + RGB via internal Spout)
// ════════════════════════════════════════════════════════════════════════════

static PluginInstance p = Source::CreatePlugin<DepthRGBCloud>( {
	"DC01",
	"Depth RGB Cloud"
} );

// ════════════════════════════════════════════════════════════════════════════
//  CONSTRUCTOR
// ════════════════════════════════════════════════════════════════════════════

DepthRGBCloud::DepthRGBCloud()
{
	// ── Spout ────────────────────────────────────────────────────────────────
	// Default sender names match the recommended TouchDesigner TOP names.
	AddParam( ParamText::create( "depthSender", "TD_Depth" ) );
	AddParam( ParamText::create( "rgbSender",   "TD_RGB"   ) );

	// ── Sensor ────────────────────────────────────────────────────────────────
	AddParam( ParamOption::Create( "resolution", {
		{ "640 x 576",  0.f },
		{ "320 x 288",  1.f },
		{ "512 x 512",  2.f }
	} ) );
	// Intrinsics: Femto Bolt NFOV approximate defaults.
	// Get exact values from the Orbbec SDK (OBCameraParam).
	// fx/fy: 0..1 → 100..1000   default ~502 → (502-100)/900 ≈ 0.447
	AddParam( Param::Create( "fx",         0.447f ) );
	AddParam( Param::Create( "fy",         0.447f ) );
	// cx: 0..1 → 0..1280   default ~320 → 320/1280 = 0.25
	AddParam( Param::Create( "cx",         0.250f ) );
	// cy: 0..1 → 0..1024   default ~287 → 287/1024 ≈ 0.280
	AddParam( Param::Create( "cy",         0.280f ) );
	// depthScale: 0..1 → 1..20 m   default 10 m → (10-1)/19 ≈ 0.474
	AddParam( Param::Create( "depthScale", 0.474f ) );
	// minDepth: 0..1 → 0..1 m   filters invalid zero-depth pixels
	AddParam( Param::Create( "minDepth",   0.0f   ) );

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
	AddParam( Param::Create( "tintR",    1.0f ) );
	AddParam( Param::Create( "tintG",    1.0f ) );
	AddParam( Param::Create( "tintB",    1.0f ) );
	AddParam( Param::Create( "clipNear", 0.0f ) );
	AddParam( Param::Create( "clipFar",  1.0f ) );
	AddParam( Param::Create( "roundPts", 1.0f ) );

	// ── Output ────────────────────────────────────────────────────────────────
	AddParam( Param::Create( "bgOpacity", 0.0f ) );
	AddParam( Param::Create( "bgR",       0.0f ) );
	AddParam( Param::Create( "bgG",       0.0f ) );
	AddParam( Param::Create( "bgB",       0.0f ) );

	// ── Groups ────────────────────────────────────────────────────────────────
	// Spout: 0-1   Sensor: 2-8   Transform: 9-14   Camera: 15-18
	// Render: 19-26   Output: 27-30
	for ( unsigned i : { 0u, 1u } )                                          SetParamGroup( i, "Spout"     );
	for ( unsigned i : { 2u, 3u, 4u, 5u, 6u, 7u, 8u } )                    SetParamGroup( i, "Sensor"    );
	for ( unsigned i : { 9u, 10u, 11u, 12u, 13u, 14u } )                   SetParamGroup( i, "Transform" );
	for ( unsigned i : { 15u, 16u, 17u, 18u } )                             SetParamGroup( i, "Camera"    );
	for ( unsigned i : { 19u, 20u, 21u, 22u, 23u, 24u, 25u, 26u } )       SetParamGroup( i, "Render"    );
	for ( unsigned i : { 27u, 28u, 29u, 30u } )                             SetParamGroup( i, "Output"    );
}

// ════════════════════════════════════════════════════════════════════════════
//  GRID
// ════════════════════════════════════════════════════════════════════════════

void DepthRGBCloud::buildGrid( int w, int h )
{
	unloadGrid();
	std::vector<float> uvs;
	uvs.reserve( w * h * 2 );
	for ( int y = 0; y < h; ++y )
		for ( int x = 0; x < w; ++x ) {
			uvs.push_back( (x + 0.5f) / w );
			uvs.push_back( (y + 0.5f) / h );
		}

	glGenVertexArrays( 1, &gridVAO );
	glGenBuffers( 1, &gridVBO );
	glBindVertexArray( gridVAO );
	glBindBuffer( GL_ARRAY_BUFFER, gridVBO );
	glBufferData( GL_ARRAY_BUFFER, (GLsizeiptr)( uvs.size() * sizeof(float) ), uvs.data(), GL_STATIC_DRAW );
	glVertexAttribPointer( 0, 2, GL_FLOAT, GL_FALSE, 2 * sizeof(float), (void*)0 );
	glEnableVertexAttribArray( 0 );
	glBindVertexArray( 0 );
	glBindBuffer( GL_ARRAY_BUFFER, 0 );

	gridCount = w * h;
	lastGridW = w;
	lastGridH = h;
}

void DepthRGBCloud::unloadGrid()
{
	if ( gridVAO ) { glDeleteVertexArrays( 1, &gridVAO ); gridVAO = 0; }
	if ( gridVBO ) { glDeleteBuffers(      1, &gridVBO ); gridVBO = 0; }
	gridCount = 0;
}

// ════════════════════════════════════════════════════════════════════════════
//  HELPERS
// ════════════════════════════════════════════════════════════════════════════

void DepthRGBCloud::ensureFBO( int w, int h )
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

GLuint DepthRGBCloud::compileProgram( const char* vs, const char* fs )
{
	auto compile = []( GLenum type, const char* src ) -> GLuint {
		GLuint s = glCreateShader( type );
		glShaderSource( s, 1, &src, nullptr );
		glCompileShader( s );
		GLint ok; glGetShaderiv( s, GL_COMPILE_STATUS, &ok );
		if ( !ok ) { glDeleteShader(s); return 0u; }
		return s;
	};
	GLuint v = compile( GL_VERTEX_SHADER,   vs );
	GLuint f = compile( GL_FRAGMENT_SHADER, fs );
	if ( !v || !f ) return 0;
	GLuint prog = glCreateProgram();
	glAttachShader( prog, v ); glAttachShader( prog, f );
	glLinkProgram( prog );
	glDeleteShader( v ); glDeleteShader( f );
	GLint ok; glGetProgramiv( prog, GL_LINK_STATUS, &ok );
	if ( !ok ) { glDeleteProgram( prog ); return 0; }
	return prog;
}

// ════════════════════════════════════════════════════════════════════════════
//  RENDER
// ════════════════════════════════════════════════════════════════════════════

static float remap( float v, float lo, float hi ) { return lo + v * (hi - lo); }

FFResult DepthRGBCloud::Render( ProcessOpenGLStruct* pGL )
{
	// ── Spout receive ─────────────────────────────────────────────────────────
	auto depthParam = std::dynamic_pointer_cast<ParamText>( GetParam( "depthSender" ) );
	auto rgbParam   = std::dynamic_pointer_cast<ParamText>( GetParam( "rgbSender"   ) );
	std::string depthName = depthParam ? depthParam->text : "";
	std::string rgbName   = rgbParam   ? rgbParam->text   : "";

	bool haveDepth = spDepth.receive( depthName );
	bool haveRGB   = spRGB.receive(   rgbName   );

	// ── Lazy GL init ──────────────────────────────────────────────────────────
	if ( !glReady )
	{
		while ( glGetError() != GL_NO_ERROR ) {}
		program = compileProgram( kVertSrc, kFragSrc );
		if ( !program ) return FF_FAIL;
		glReady = true;
		while ( glGetError() != GL_NO_ERROR ) {}
	}

	// ── Grid ──────────────────────────────────────────────────────────────────
	auto resOpt = std::dynamic_pointer_cast<ParamOption>( GetParam("resolution") );
	int resIdx  = resOpt ? (int)resOpt->GetValue() : 0;
	int gW, gH;
	switch ( resIdx ) {
		case 1:  gW = 320; gH = 288; break;
		case 2:  gW = 512; gH = 512; break;
		default: gW = 640; gH = 576; break;
	}
	if ( gW != lastGridW || gH != lastGridH )
		buildGrid( gW, gH );

	// ── FBO ───────────────────────────────────────────────────────────────────
	GLint vp[4]; glGetIntegerv( GL_VIEWPORT, vp );
	const int W = vp[2], H = vp[3];
	ensureFBO( W, H );

	glBindFramebuffer( GL_FRAMEBUFFER, fbo );
	glViewport( 0, 0, W, H );
	glEnable( GL_DEPTH_TEST );
	glDepthFunc( GL_LEQUAL );

	glClearColor( GetParam("bgR")->GetValue(), GetParam("bgG")->GetValue(),
	              GetParam("bgB")->GetValue(), GetParam("bgOpacity")->GetValue() );
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

	if ( !haveDepth || !haveRGB || gridCount == 0 )
	{
		glBindFramebuffer( GL_READ_FRAMEBUFFER, fbo );
		glBindFramebuffer( GL_DRAW_FRAMEBUFFER, pGL->HostFBO );
		glBlitFramebuffer( 0,0,W,H, 0,0,W,H, GL_COLOR_BUFFER_BIT, GL_NEAREST );
		glBindFramebuffer( GL_FRAMEBUFFER, pGL->HostFBO );
		glDisable( GL_DEPTH_TEST ); glDepthFunc( GL_LESS );
		return FF_SUCCESS;
	}

	// ── Parameters ────────────────────────────────────────────────────────────
	float fx         = remap( GetParam("fx"        )->GetValue(), 100.f, 1000.f );
	float fy         = remap( GetParam("fy"        )->GetValue(), 100.f, 1000.f );
	float cx         = remap( GetParam("cx"        )->GetValue(),   0.f, 1280.f );
	float cy         = remap( GetParam("cy"        )->GetValue(),   0.f, 1024.f );
	float depthScale = remap( GetParam("depthScale")->GetValue(),   1.f,   20.f );
	float minDepth   = remap( GetParam("minDepth"  )->GetValue(),   0.f,    1.f );

	float rotX   = remap( GetParam("rotX"  )->GetValue(), -180.f, 180.f );
	float rotY   = remap( GetParam("rotY"  )->GetValue(), -180.f, 180.f );
	float rotZ   = remap( GetParam("rotZ"  )->GetValue(), -180.f, 180.f );
	float scale  = remap( GetParam("scale" )->GetValue(),   0.01f,  5.f );
	float transX = remap( GetParam("transX")->GetValue(),    -2.f,   2.f );
	float transY = remap( GetParam("transY")->GetValue(),    -2.f,   2.f );

	float fov     = remap( GetParam("fov"    )->GetValue(),  10.f, 120.f );
	float camDist = remap( GetParam("camDist")->GetValue(),  0.5f,  20.f );
	float orbitX  = remap( GetParam("orbitX" )->GetValue(),-180.f, 180.f );
	float orbitY  = remap( GetParam("orbitY" )->GetValue(),-180.f, 180.f );

	float ptSize   = remap( GetParam("pointSize")->GetValue(), 1.f, 20.f );
	float tintR    = GetParam("tintR")->GetValue();
	float tintG    = GetParam("tintG")->GetValue();
	float tintB    = GetParam("tintB")->GetValue();
	float clipNear = remap( GetParam("clipNear")->GetValue(), 0.f, depthScale );
	float clipFar  = remap( GetParam("clipFar" )->GetValue(), 0.f, depthScale );
	int   doRound  = GetParam("roundPts")->GetValue() > 0.5f ? 1 : 0;

	auto colorOpt  = std::dynamic_pointer_cast<ParamOption>( GetParam("colorMode") );
	int  colorMode = colorOpt ? (int)colorOpt->GetValue() : 0;

	// ── Matrices ──────────────────────────────────────────────────────────────
	float aspect = H > 0 ? (float)W / H : 1.f;

	glm::mat4 model( 1.f );
	model = glm::translate( model, glm::vec3( transX, transY, 0.f ) );
	model = glm::rotate( model, glm::radians(rotX), glm::vec3(1,0,0) );
	model = glm::rotate( model, glm::radians(rotY), glm::vec3(0,1,0) );
	model = glm::rotate( model, glm::radians(rotZ), glm::vec3(0,0,1) );
	model = glm::scale(  model, glm::vec3(scale) );

	float ox = glm::radians(orbitX), oy = glm::radians(orbitY);
	glm::vec3 camPos( camDist*glm::cos(ox)*glm::sin(oy), camDist*glm::sin(ox), camDist*glm::cos(ox)*glm::cos(oy) );
	glm::vec3 up = glm::cos(ox) >= 0.f ? glm::vec3(0,1,0) : glm::vec3(0,-1,0);

	glm::mat4 view = glm::lookAt( camPos, glm::vec3(0,0,0), up );
	glm::mat4 proj = glm::perspective( glm::radians(fov), aspect, 0.01f, 1000.f );
	glm::mat4 mvp  = proj * view * model;

	// ── Draw ──────────────────────────────────────────────────────────────────
	glEnable( GL_BLEND );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );
	glEnable( GL_PROGRAM_POINT_SIZE );

	glUseProgram( program );

	// Spout textures have no power-of-2 padding — maxUV is always 1.0
	glActiveTexture( GL_TEXTURE0 ); glBindTexture( GL_TEXTURE_2D, spDepth.tex );
	glActiveTexture( GL_TEXTURE1 ); glBindTexture( GL_TEXTURE_2D, spRGB.tex   );

	glUniform1i( glGetUniformLocation(program,"uDepth"),      0 );
	glUniform1i( glGetUniformLocation(program,"uRGB"),        1 );
	glUniform2f( glGetUniformLocation(program,"uMaxUVDepth"), 1.0f, 1.0f );
	glUniform2f( glGetUniformLocation(program,"uMaxUVRGB"),   1.0f, 1.0f );

	glUniformMatrix4fv( glGetUniformLocation(program,"uMVP"), 1, GL_FALSE, glm::value_ptr(mvp) );
	glUniform1f( glGetUniformLocation(program,"uPointSize"),  ptSize     );
	glUniform1f( glGetUniformLocation(program,"uFx"),         fx         );
	glUniform1f( glGetUniformLocation(program,"uFy"),         fy         );
	glUniform1f( glGetUniformLocation(program,"uCx"),         cx         );
	glUniform1f( glGetUniformLocation(program,"uCy"),         cy         );
	glUniform1f( glGetUniformLocation(program,"uGridW"),      (float)gW  );
	glUniform1f( glGetUniformLocation(program,"uGridH"),      (float)gH  );
	glUniform1f( glGetUniformLocation(program,"uDepthScale"), depthScale );
	glUniform1i( glGetUniformLocation(program,"uColorMode"),  colorMode  );
	glUniform3f( glGetUniformLocation(program,"uTint"),       tintR, tintG, tintB );
	glUniform1f( glGetUniformLocation(program,"uZMax"),       depthScale );
	glUniform1f( glGetUniformLocation(program,"uYHalf"),      depthScale * 0.5f );
	glUniform1f( glGetUniformLocation(program,"uMinDepth"),   minDepth   );
	glUniform1f( glGetUniformLocation(program,"uClipNear"),   clipNear   );
	glUniform1f( glGetUniformLocation(program,"uClipFar"),    clipFar    );
	glUniform1i( glGetUniformLocation(program,"uRound"),      doRound    );

	glBindVertexArray( gridVAO );
	glDrawArrays( GL_POINTS, 0, gridCount );
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
	GLint n; glGetIntegerv( GL_MAX_TEXTURE_IMAGE_UNITS, &n );
	for ( GLint i = 0; i < n; ++i ) { glActiveTexture( GL_TEXTURE0+i ); glBindTexture( GL_TEXTURE_2D, 0 ); }
	glActiveTexture( GL_TEXTURE0 );
	while ( glGetError() != GL_NO_ERROR ) {}

	return FF_SUCCESS;
}

// ════════════════════════════════════════════════════════════════════════════
//  CLEAN
// ════════════════════════════════════════════════════════════════════════════

void DepthRGBCloud::Clean()
{
	spDepth.release();
	spRGB.release();
	unloadGrid();
	if ( program  ) { glDeleteProgram(       program  ); program  = 0; }
	if ( fbo      ) { glDeleteFramebuffers(  1, &fbo      ); fbo      = 0; }
	if ( colorTex ) { glDeleteTextures(      1, &colorTex ); colorTex = 0; }
	if ( depthRbo ) { glDeleteRenderbuffers( 1, &depthRbo ); depthRbo = 0; }
}
