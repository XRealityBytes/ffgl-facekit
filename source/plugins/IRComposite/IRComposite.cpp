#define NOMINMAX
#include <windows.h>
#include "IRComposite.h"
#include <FFGLParamText.h>

#include <cmath>
#include <cstring>

using namespace ffglqs;

// ════════════════════════════════════════════════════════════════════════════
//  SpoutIn implementation
// ════════════════════════════════════════════════════════════════════════════

bool SpoutIn::receive( const std::string& senderName )
{
	if ( senderName.empty() ) return false;

	if ( !sp ) {
		sp = GetSpout();
		if ( !sp ) return false;
	}

	if ( senderName != name ) {
		name = senderName;
		sp->SetReceiverName( name.c_str() );
		if ( tex ) { glDeleteTextures( 1, &tex ); tex = 0; w = h = 0; }
	}

	if ( !tex ) {
		glGenTextures( 1, &tex );
		glBindTexture( GL_TEXTURE_2D, tex );
		glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, 4, 4, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
		glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
		glBindTexture( GL_TEXTURE_2D, 0 );
	}

	if ( !sp->ReceiveTexture( tex, GL_TEXTURE_2D, false, 0 ) )
		return false;

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
//  uCloud = rendered point cloud (FF_EFFECT input from Resolume)
//  uIR    = raw IR texture received via internal Spout receiver
// ════════════════════════════════════════════════════════════════════════════

static const char* kVertSrc = R"GLSL(
#version 410 core
layout(location=0) in vec4 vPosition;
layout(location=1) in vec2 vUV;
out vec2 fUV;
void main() { gl_Position = vPosition; fUV = vUV; }
)GLSL";

static const char* kFragSrc = R"GLSL(
#version 410 core

in vec2 fUV;

uniform sampler2D uCloud;
uniform sampler2D uIR;
uniform vec2      uMaxUVCloud;
uniform vec2      uMaxUVIR;

uniform float uTime;

// ── Distortion ───────────────────────────────────────────────────────────────
uniform float uDistortAmount; // 0..1, max offset ~10% of image
uniform float uDistortSpeed;  // animation speed (Hz)
uniform float uDistortScale;  // IR gradient sample radius (in pixels, 0.5..5.5)

// ── Glow ──────────────────────────────────────────────────────────────────────
uniform float uGlowAmount;
uniform float uGlowRadius;
uniform vec3  uGlowColor;

// ── Edge detect ───────────────────────────────────────────────────────────────
uniform float uEdgeAmount;
uniform float uEdgeScale;
uniform vec3  uEdgeColor;

// ── Mask ──────────────────────────────────────────────────────────────────────
uniform float uMaskAmount;
uniform float uMaskThresh;
uniform float uMaskSoft;
uniform int   uMaskInvert;

// ── Skin highlight ────────────────────────────────────────────────────────────
uniform float uSkinAmount;
uniform float uSkinThresh;
uniform vec3  uSkinColor;

// ── Direct IR overlay ─────────────────────────────────────────────────────────
uniform float uIROverlay;

out vec4 fragColor;

void main()
{
    vec2 cloudUV = fUV * uMaxUVCloud;
    vec2 irUV    = fUV * uMaxUVIR;

    // IR pixel step sizes (approximate — assumes ~640×576 IR)
    vec2 irPx = uMaxUVIR / vec2( 640.0, 576.0 );

    // ── UV Distortion using IR gradient ──────────────────────────────────────
    float scl = uDistortScale * 5.0 + 0.5;
    vec2 irGrad = vec2(
        texture( uIR, irUV + vec2( scl * irPx.x, 0.0 ) ).r
      - texture( uIR, irUV - vec2( scl * irPx.x, 0.0 ) ).r,
        texture( uIR, irUV + vec2( 0.0, scl * irPx.y ) ).r
      - texture( uIR, irUV - vec2( 0.0, scl * irPx.y ) ).r
    );
    float wave      = sin( uTime * uDistortSpeed * 6.28318 );
    vec2  distorted = cloudUV + irGrad * uDistortAmount * 0.1 * wave;
    distorted       = clamp( distorted, vec2(0.0), uMaxUVCloud );

    vec4 cloud = texture( uCloud, distorted );

    // ── IR sample ─────────────────────────────────────────────────────────────
    float ir = texture( uIR, irUV ).r;

    // ── Glow (8-tap ring around IR) ───────────────────────────────────────────
    float gr = uGlowRadius * uMaxUVIR.x * 0.1;
    float glowSum = 0.0;
    for ( int i = 0; i < 8; ++i ) {
        float a = float(i) * 0.7854;
        glowSum += texture( uIR, irUV + vec2( cos(a), sin(a) ) * gr ).r;
    }
    glowSum /= 8.0;
    vec3 glow = uGlowColor * glowSum * uGlowAmount;

    // ── Edge detection (Sobel on IR) ──────────────────────────────────────────
    float es = uEdgeScale * 3.0 + 0.5;
    float px = irPx.x * es, py = irPx.y * es;
    float tl = texture( uIR, irUV + vec2(-px,-py) ).r;
    float tc = texture( uIR, irUV + vec2( 0.,-py) ).r;
    float tr = texture( uIR, irUV + vec2( px,-py) ).r;
    float ml = texture( uIR, irUV + vec2(-px, 0.) ).r;
    float mr = texture( uIR, irUV + vec2( px, 0.) ).r;
    float bl = texture( uIR, irUV + vec2(-px, py) ).r;
    float bc = texture( uIR, irUV + vec2( 0., py) ).r;
    float br = texture( uIR, irUV + vec2( px, py) ).r;
    float gx   = -tl - 2.0*ml - bl + tr + 2.0*mr + br;
    float gy   = -tl - 2.0*tc - tr + bl + 2.0*bc + br;
    float edge = sqrt( gx*gx + gy*gy );
    vec3  edges = uEdgeColor * edge * uEdgeAmount;

    // ── Mask ──────────────────────────────────────────────────────────────────
    float soft = max( uMaskSoft * 0.1, 0.001 );
    float mask = smoothstep( uMaskThresh - soft, uMaskThresh + soft, ir );
    if ( uMaskInvert == 1 ) mask = 1.0 - mask;

    // ── Skin highlight ─────────────────────────────────────────────────────────
    float skinSoft = 0.05;
    float skinMask = smoothstep( uSkinThresh - skinSoft, uSkinThresh + skinSoft, ir )
                   * ( 1.0 - smoothstep( uSkinThresh + skinSoft, uSkinThresh + skinSoft*3.0, ir ) );
    vec3 skin = uSkinColor * skinMask * uSkinAmount;

    // ── IR direct overlay ──────────────────────────────────────────────────────
    vec3 irVis = vec3(ir) * uIROverlay;

    // ── Composite ─────────────────────────────────────────────────────────────
    vec3  result = cloud.rgb + glow + edges + skin + irVis;
    float alpha  = cloud.a * mix( 1.0, mask, uMaskAmount );

    fragColor = vec4( result, alpha );
}
)GLSL";

// ════════════════════════════════════════════════════════════════════════════
//  PLUGIN REGISTRATION  (FF_EFFECT — single input: cloud render from Resolume)
// ════════════════════════════════════════════════════════════════════════════

static PluginInstance p = Effect::CreatePlugin<IRComposite>( {
	"IR01",
	"IR Composite"
} );

// ════════════════════════════════════════════════════════════════════════════
//  CONSTRUCTOR
// ════════════════════════════════════════════════════════════════════════════

IRComposite::IRComposite()
{
	// ── Spout ────────────────────────────────────────────────────────────────
	AddParam( ParamText::create( "irSender", "TD_IR" ) );

	// ── Distort ───────────────────────────────────────────────────────────────
	AddParam( Param::Create( "distortAmount", 0.0f ) );
	AddParam( Param::Create( "distortSpeed",  0.2f ) );
	AddParam( Param::Create( "distortScale",  0.1f ) );

	// ── Glow ──────────────────────────────────────────────────────────────────
	AddParam( Param::Create( "glowAmount", 0.0f ) );
	AddParam( Param::Create( "glowRadius", 0.2f ) );
	AddParam( Param::Create( "glowR",      0.2f ) );
	AddParam( Param::Create( "glowG",      0.6f ) );
	AddParam( Param::Create( "glowB",      1.0f ) );

	// ── Edge ──────────────────────────────────────────────────────────────────
	AddParam( Param::Create( "edgeAmount", 0.0f ) );
	AddParam( Param::Create( "edgeScale",  0.1f ) );
	AddParam( Param::Create( "edgeR",      1.0f ) );
	AddParam( Param::Create( "edgeG",      1.0f ) );
	AddParam( Param::Create( "edgeB",      1.0f ) );

	// ── Mask ──────────────────────────────────────────────────────────────────
	AddParam( Param::Create( "maskAmount", 0.0f  ) );
	AddParam( Param::Create( "maskThresh", 0.1f  ) );
	AddParam( Param::Create( "maskSoft",   0.05f ) );
	AddParam( Param::Create( "maskInvert", 0.0f  ) );

	// ── Skin ──────────────────────────────────────────────────────────────────
	AddParam( Param::Create( "skinAmount", 0.0f ) );
	AddParam( Param::Create( "skinThresh", 0.6f ) );
	AddParam( Param::Create( "skinR",      1.0f ) );
	AddParam( Param::Create( "skinG",      0.6f ) );
	AddParam( Param::Create( "skinB",      0.3f ) );

	// ── Mix ───────────────────────────────────────────────────────────────────
	AddParam( Param::Create( "irOverlay",  0.0f ) );

	// ── Groups ────────────────────────────────────────────────────────────────
	// Spout: 0   Distort: 1-3   Glow: 4-8   Edge: 9-13
	// Mask: 14-17   Skin: 18-22   Mix: 23
	SetParamGroup( 0u, "Spout" );
	for ( unsigned i : { 1u, 2u, 3u } )                          SetParamGroup( i, "Distort" );
	for ( unsigned i : { 4u, 5u, 6u, 7u, 8u } )                  SetParamGroup( i, "Glow"    );
	for ( unsigned i : { 9u, 10u, 11u, 12u, 13u } )              SetParamGroup( i, "Edge"    );
	for ( unsigned i : { 14u, 15u, 16u, 17u } )                  SetParamGroup( i, "Mask"    );
	for ( unsigned i : { 18u, 19u, 20u, 21u, 22u } )             SetParamGroup( i, "Skin"    );
	SetParamGroup( 23u, "Mix" );
}

// ════════════════════════════════════════════════════════════════════════════
//  HELPERS
// ════════════════════════════════════════════════════════════════════════════

GLuint IRComposite::compileProgram( const char* vs, const char* fs )
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

FFResult IRComposite::Render( ProcessOpenGLStruct* pGL )
{
	// Cloud render comes from Resolume as the single FF_EFFECT input
	if ( pGL->numInputTextures < 1 ) return FF_FAIL;
	if ( !pGL->inputTextures[0] )    return FF_FAIL;

	// ── Spout receive (IR) ────────────────────────────────────────────────────
	auto irParam = std::dynamic_pointer_cast<ParamText>( GetParam( "irSender" ) );
	std::string irName = irParam ? irParam->text : "";
	bool haveIR = spIR.receive( irName );

	// ── Lazy GL init ──────────────────────────────────────────────────────────
	if ( !glReady )
	{
		while ( glGetError() != GL_NO_ERROR ) {}
		program = compileProgram( kVertSrc, kFragSrc );
		if ( !program ) return FF_FAIL;
		if ( !quad.Initialise() ) return FF_FAIL;
		glReady = true;
		while ( glGetError() != GL_NO_ERROR ) {}
	}

	// ── Render direct to host FBO ─────────────────────────────────────────────
	GLint vp[4]; glGetIntegerv( GL_VIEWPORT, vp );
	glBindFramebuffer( GL_FRAMEBUFFER, pGL->HostFBO );
	glViewport( 0, 0, vp[2], vp[3] );
	glDisable( GL_DEPTH_TEST );

	auto* cloudTex = pGL->inputTextures[0];
	float maxUCloud_s = cloudTex->HardwareWidth  > 0 ? (float)cloudTex->Width  / cloudTex->HardwareWidth  : 1.f;
	float maxUCloud_t = cloudTex->HardwareHeight > 0 ? (float)cloudTex->Height / cloudTex->HardwareHeight : 1.f;

	// Spout texture — no padding, maxUV always 1.0
	// Fall back to a 1×1 black texture unit if IR is not yet connected
	GLuint irTexId = haveIR ? spIR.tex : 0;

	float t = hostTime / 1000.f;

	glUseProgram( program );

	glActiveTexture( GL_TEXTURE0 ); glBindTexture( GL_TEXTURE_2D, cloudTex->Handle );
	glActiveTexture( GL_TEXTURE1 ); glBindTexture( GL_TEXTURE_2D, irTexId           );

	glUniform1i( glGetUniformLocation(program,"uCloud"),       0 );
	glUniform1i( glGetUniformLocation(program,"uIR"),          1 );
	glUniform2f( glGetUniformLocation(program,"uMaxUVCloud"),   maxUCloud_s, maxUCloud_t );
	glUniform2f( glGetUniformLocation(program,"uMaxUVIR"),      1.0f, 1.0f );
	glUniform1f( glGetUniformLocation(program,"uTime"),         t );

	glUniform1f( glGetUniformLocation(program,"uDistortAmount"), GetParam("distortAmount")->GetValue() );
	glUniform1f( glGetUniformLocation(program,"uDistortSpeed"),  GetParam("distortSpeed" )->GetValue() * 4.f );
	glUniform1f( glGetUniformLocation(program,"uDistortScale"),  GetParam("distortScale" )->GetValue() );

	glUniform1f( glGetUniformLocation(program,"uGlowAmount"),  GetParam("glowAmount")->GetValue() );
	glUniform1f( glGetUniformLocation(program,"uGlowRadius"),  GetParam("glowRadius")->GetValue() );
	glUniform3f( glGetUniformLocation(program,"uGlowColor"),   GetParam("glowR")->GetValue(), GetParam("glowG")->GetValue(), GetParam("glowB")->GetValue() );

	glUniform1f( glGetUniformLocation(program,"uEdgeAmount"),  GetParam("edgeAmount")->GetValue() );
	glUniform1f( glGetUniformLocation(program,"uEdgeScale"),   GetParam("edgeScale" )->GetValue() );
	glUniform3f( glGetUniformLocation(program,"uEdgeColor"),   GetParam("edgeR")->GetValue(), GetParam("edgeG")->GetValue(), GetParam("edgeB")->GetValue() );

	glUniform1f( glGetUniformLocation(program,"uMaskAmount"),  GetParam("maskAmount")->GetValue() );
	glUniform1f( glGetUniformLocation(program,"uMaskThresh"),  GetParam("maskThresh")->GetValue() );
	glUniform1f( glGetUniformLocation(program,"uMaskSoft"),    GetParam("maskSoft"  )->GetValue() );
	glUniform1i( glGetUniformLocation(program,"uMaskInvert"),  GetParam("maskInvert")->GetValue() > 0.5f ? 1 : 0 );

	glUniform1f( glGetUniformLocation(program,"uSkinAmount"),  GetParam("skinAmount")->GetValue() );
	glUniform1f( glGetUniformLocation(program,"uSkinThresh"),  GetParam("skinThresh")->GetValue() );
	glUniform3f( glGetUniformLocation(program,"uSkinColor"),   GetParam("skinR")->GetValue(), GetParam("skinG")->GetValue(), GetParam("skinB")->GetValue() );

	glUniform1f( glGetUniformLocation(program,"uIROverlay"),   GetParam("irOverlay")->GetValue() );

	quad.Draw();

	// ── Restore GL state ──────────────────────────────────────────────────────
	glUseProgram( 0 );
	GLint n; glGetIntegerv( GL_MAX_TEXTURE_IMAGE_UNITS, &n );
	for ( GLint i = 0; i < n; ++i ) { glActiveTexture( GL_TEXTURE0+i ); glBindTexture( GL_TEXTURE_2D, 0 ); }
	glActiveTexture( GL_TEXTURE0 );
	glDepthFunc( GL_LESS );
	glBlendFuncSeparate( GL_ONE, GL_ZERO, GL_ONE, GL_ZERO );
	while ( glGetError() != GL_NO_ERROR ) {}

	return FF_SUCCESS;
}

// ════════════════════════════════════════════════════════════════════════════
//  CLEAN
// ════════════════════════════════════════════════════════════════════════════

void IRComposite::Clean()
{
	spIR.release();
	quad.Release();
	if ( program ) { glDeleteProgram( program ); program = 0; }
}
