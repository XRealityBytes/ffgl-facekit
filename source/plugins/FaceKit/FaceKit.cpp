#include "FaceKit.h"
#include <ffgl/FFGLLog.h>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cstdlib>

using namespace ffglqs;

namespace
{
static const char* kDefaultModelPath = "";

static constexpr unsigned int PI_MODEL_PATH         = 0;
static constexpr unsigned int PI_MODEL_FORMAT       = 1;
static constexpr unsigned int PI_MODEL_STATUS       = 2;
static constexpr unsigned int PI_MESH_TOGGLE_BASE   = 3;
static constexpr unsigned int PI_RENDER_LEVEL       = PI_MESH_TOGGLE_BASE + FaceKitPlugin::MAX_COMPONENT_TOGGLES;
static constexpr unsigned int PI_SHADING            = PI_RENDER_LEVEL + 1;
static constexpr unsigned int PI_COLOR_R            = PI_SHADING + 1;
static constexpr unsigned int PI_COLOR_G            = PI_COLOR_R + 1;
static constexpr unsigned int PI_COLOR_B            = PI_COLOR_G + 1;
static constexpr unsigned int PI_LIGHT_X            = PI_COLOR_B + 1;
static constexpr unsigned int PI_LIGHT_Y            = PI_LIGHT_X + 1;
static constexpr unsigned int PI_LIGHT_Z            = PI_LIGHT_Y + 1;
static constexpr unsigned int PI_AMBIENT            = PI_LIGHT_Z + 1;
static constexpr unsigned int PI_CAM_Z              = PI_AMBIENT + 1;
static constexpr unsigned int PI_ORBIT_X            = PI_CAM_Z + 1;
static constexpr unsigned int PI_ORBIT_Y            = PI_ORBIT_X + 1;
static constexpr unsigned int PI_FOV                = PI_ORBIT_Y + 1;
static constexpr unsigned int PI_BG_OPACITY         = PI_FOV + 1;
static constexpr unsigned int PI_BG_R               = PI_BG_OPACITY + 1;
static constexpr unsigned int PI_BG_G               = PI_BG_R + 1;
static constexpr unsigned int PI_BG_B               = PI_BG_G + 1;
static constexpr unsigned int PI_OSC_PORT           = PI_BG_B + 1;
static constexpr unsigned int PI_OSC_AGENT          = PI_OSC_PORT + 1;
static constexpr unsigned int PI_OSC_STATUS         = PI_OSC_AGENT + 1;

std::string MeshToggleParamName( size_t slot )
{
	char name[32];
	snprintf( name, sizeof( name ), "meshToggle%02zu", slot + 1 );
	return name;
}
}

// ── GLSL ─────────────────────────────────────────────────────────────────────

static const char* kVS = R"GLSL(
#version 410 core
layout(location=0) in vec3  aPos;
layout(location=1) in vec3  aNormal;
layout(location=2) in float aSubmeshId;

uniform mat4 uMVP;
uniform mat3 uNormalMat;

out vec3  vNormal;
out float vSubmeshId;

void main()
{
    gl_Position = uMVP * vec4(aPos, 1.0);
    vNormal     = normalize(uNormalMat * aNormal);
    vSubmeshId  = aSubmeshId;
}
)GLSL";

static const char* kFS = R"GLSL(
#version 410 core
in vec3  vNormal;
in float vSubmeshId;

// 0=Flat  1=Diffuse  2=Wireframe  3=Normals  4=Toon  5=Part Colors
uniform int   uShadingMode;
uniform vec3  uColor;
uniform vec3  uLightDir;
uniform float uAmbient;

out vec4 fragColor;

// Spread 17 sub-mesh IDs across the hue wheel using the golden-ratio offset
vec3 hueColor(float h)
{
    h = fract(h);
    float r = abs(h * 6.0 - 3.0) - 1.0;
    float g = 2.0 - abs(h * 6.0 - 2.0);
    float b = 2.0 - abs(h * 6.0 - 4.0);
    return clamp(vec3(r, g, b), 0.0, 1.0);
}

void main()
{
    vec3 N = normalize(vNormal);
    vec3 L = normalize(uLightDir);

    if (uShadingMode == 0 || uShadingMode == 2)  // Flat / Wireframe
    {
        fragColor = vec4(uColor, 1.0);
    }
    else if (uShadingMode == 1)  // Diffuse
    {
        float diff = max(dot(N, L), 0.0);
        fragColor  = vec4(uColor * (uAmbient + (1.0 - uAmbient) * diff), 1.0);
    }
    else if (uShadingMode == 3)  // Normals
    {
        fragColor = vec4(N * 0.5 + 0.5, 1.0);
    }
    else if (uShadingMode == 4)  // Toon
    {
        float diff    = max(dot(N, L), 0.0);
        float stepped = floor(diff * 4.0) / 4.0;
        fragColor = vec4(uColor * (0.2 + 0.8 * stepped), 1.0);
    }
    else  // Part Colors
    {
        fragColor = vec4(hueColor(vSubmeshId * 0.618033988), 1.0);
    }
}
)GLSL";

// ── Registration ──────────────────────────────────────────────────────────────

static PluginInstance p = Source::CreatePlugin<FaceKitPlugin>( {
	"FK01",
	"FaceKit"
} );

// ── Constructor ───────────────────────────────────────────────────────────────

FaceKitPlugin::FaceKitPlugin()
{
	// The quickstart Source base still compiles its own pass-through shader during InitGL.
	// FaceKit renders via its own program in Render(), but the base path still needs a valid fragment body.
	SetFragmentShader( "void main() { fragColor = vec4(0.0); }" );

	for( size_t slot = 0; slot < MAX_COMPONENT_TOGGLES; ++slot )
		m_componentParamNames[slot] = MeshToggleParamName( slot );

	// ── Model ─────────────────────────────────────────────────────────────────
	AddParam( ParamFile::create( "modelPath", std::vector<std::string>{ "obj", "gltf", "glb" }, kDefaultModelPath ) );
	AddParam( ParamOption::Create( "modelFormat", {
		{ "Auto",            0.f },
		{ "ICT FaceKit OBJ", 1.f },
		{ "glTF / GLB",      2.f }
	} ) );
	AddParam( ParamText::create( "modelStatus", "No model path set" ) );
	for( size_t slot = 0; slot < MAX_COMPONENT_TOGGLES; ++slot )
		AddParam( ParamBool::Create( m_componentParamNames[slot], true ) );

	// ── Render level ──────────────────────────────────────────────────────────
	AddParam( ParamOption::Create( "renderLevel", {
		{ "Face",        0.f },
		{ "Face + Eyes", 1.f },
		{ "Full",        2.f }
	}, 2 ) );

	// ── Shading ───────────────────────────────────────────────────────────────
	AddParam( ParamOption::Create( "shading", {
		{ "Flat",        0.f },
		{ "Diffuse",     1.f },
		{ "Wireframe",   2.f },
		{ "Normals",     3.f },
		{ "Toon",        4.f },
		{ "Part Colors", 5.f }
	}, 3 ) );

	// ── Colour ────────────────────────────────────────────────────────────────
	// Default: neutral warm white
	AddParam( Param::Create( "colorR", 1.0f ) );
	AddParam( Param::Create( "colorG", 1.0f ) );
	AddParam( Param::Create( "colorB", 1.0f ) );

	// ── Lighting ──────────────────────────────────────────────────────────────
	// Each 0..1 maps to -1..1.  Default (0.5, 0.75, 0.6) → direction (0, 0.5, 0.2).
	AddParam( Param::Create( "lightX", 0.5f  ) );
	AddParam( Param::Create( "lightY", 0.75f ) );
	AddParam( Param::Create( "lightZ", 0.6f  ) );
	// Ambient 0..1, default 0.15
	AddParam( Param::Create( "ambient", 0.15f ) );

	// ── Camera ────────────────────────────────────────────────────────────────
	// camZ: 0..1 → 0.5x..2.0x fitted distance from the current model bounds.
	AddParam( Param::Create( "camZ",   0.38f ) );
	// Orbit angles: 0..1 → -180..180 degrees.  0.5 = straight on.
	AddParam( Param::Create( "orbitX", 0.25f ) );
	AddParam( Param::Create( "orbitY", 0.5f  ) );
	// FOV: 0..1 → 10..90 degrees.  Default ~0.39 ≈ 45°.
	AddParam( Param::Create( "fov",    0.39f ) );

	// ── Background ────────────────────────────────────────────────────────────
	AddParam( Param::Create( "bgOpacity", 0.0f ) );
	AddParam( Param::Create( "bgR",       0.0f ) );
	AddParam( Param::Create( "bgG",       0.0f ) );
	AddParam( Param::Create( "bgB",       0.0f ) );

	// ── OSC ───────────────────────────────────────────────────────────────────
	AddParam( ParamText::create( "oscPort",    "7400" ) );
	AddParam( ParamOption::Create( "oscAgent", {
		{ "Any",     0.f },
		{ "Agent A", 1.f },
		{ "Agent B", 2.f }
	} ) );
	AddParam( ParamText::create( "oscStatus",  "port 7400 | pkts: 0" ) );

	m_oscPortLast = "7400";
	m_oscAgentLast = OscAgentSelection_Any;
	m_osc.SetAgentSelection( m_oscAgentLast );
	m_osc.Start( 7400, &m_faceState );

	SetParamGroup( PI_MODEL_PATH, "Model" );
	SetParamGroup( PI_MODEL_FORMAT, "Model" );
	SetParamGroup( PI_MODEL_STATUS, "Model" );
	for( size_t slot = 0; slot < MAX_COMPONENT_TOGGLES; ++slot )
		SetParamGroup( PI_MESH_TOGGLE_BASE + (unsigned int)slot, "Meshes" );
	SetParamGroup( PI_RENDER_LEVEL, "Render" );
	SetParamGroup( PI_SHADING, "Render" );
	SetParamGroup( PI_COLOR_R, "Render" );
	SetParamGroup( PI_COLOR_G, "Render" );
	SetParamGroup( PI_COLOR_B, "Render" );
	SetParamGroup( PI_LIGHT_X, "Light" );
	SetParamGroup( PI_LIGHT_Y, "Light" );
	SetParamGroup( PI_LIGHT_Z, "Light" );
	SetParamGroup( PI_AMBIENT, "Light" );
	SetParamGroup( PI_CAM_Z, "Camera" );
	SetParamGroup( PI_ORBIT_X, "Camera" );
	SetParamGroup( PI_ORBIT_Y, "Camera" );
	SetParamGroup( PI_FOV, "Camera" );
	SetParamGroup( PI_BG_OPACITY, "Output" );
	SetParamGroup( PI_BG_R, "Output" );
	SetParamGroup( PI_BG_G, "Output" );
	SetParamGroup( PI_BG_B, "Output" );
	SetParamGroup( PI_OSC_PORT, "OSC" );
	SetParamGroup( PI_OSC_AGENT, "OSC" );
	SetParamGroup( PI_OSC_STATUS, "OSC" );
}

FFResult FaceKitPlugin::Init()
{
	RefreshMeshToggleParams( false );
	QueueModelRequestFromParams();
	ApplyQueuedModelRequest( true );
	m_osc.SetAgentSelection( m_oscAgentLast );
	SetStatusTextParam( "oscStatus", PI_OSC_STATUS, m_oscStatusText, m_osc.GetStatusText() );
	return FF_SUCCESS;
}

// ── Clean ─────────────────────────────────────────────────────────────────────

void FaceKitPlugin::Clean()
{
	m_osc.Stop();

	if( m_loadThread.joinable() ) m_loadThread.join();
	m_mesh.DeInitGL();
	m_mesh.Clear();

	if( m_program  ) { glDeleteProgram( m_program ); m_program  = 0; }
	if( m_fbo      ) { glDeleteFramebuffers(  1, &m_fbo      ); m_fbo      = 0; }
	if( m_colorTex ) { glDeleteTextures(      1, &m_colorTex ); m_colorTex = 0; }
	if( m_depthRbo ) { glDeleteRenderbuffers( 1, &m_depthRbo ); m_depthRbo = 0; }

	m_fboW = m_fboH = 0;
	m_glReady = false;
}

// ── Render ────────────────────────────────────────────────────────────────────

static float remap( float v, float lo, float hi ) { return lo + v * ( hi - lo ); }

void FaceKitPlugin::SetStatusTextParam( const char* paramName, unsigned int paramIndex,
                                        std::string& cache, const std::string& text )
{
	if( cache == text )
		return;

	cache = text;
	auto param = GetParamText( paramName );
	if( param )
		param->text = text;
	RaiseParamEvent( paramIndex, FF_EVENT_FLAG_VALUE );
}

void FaceKitPlugin::RefreshMeshToggleParams( bool resetValues )
{
	const std::vector<FaceModelComponent>& components = m_mesh.GetComponents();
	const size_t visibleCount = std::min( components.size(), MAX_COMPONENT_TOGGLES );

	for( size_t slot = 0; slot < MAX_COMPONENT_TOGGLES; ++slot )
	{
		const unsigned int paramIndex = PI_MESH_TOGGLE_BASE + (unsigned int)slot;
		const bool visible = slot < visibleCount;
		const std::string displayName = visible
			? components[slot].name
			: std::string( "Mesh " ) + std::to_string( slot + 1 );

		SetParamDisplayName( paramIndex, displayName, true );
		SetParamVisibility( paramIndex, visible, true );

		auto toggleParam = std::dynamic_pointer_cast<ParamBool>( GetParam( m_componentParamNames[slot] ) );
		if( toggleParam && ( resetValues || !visible ) )
		{
			toggleParam->SetValue( true );
			RaiseParamEvent( paramIndex, FF_EVENT_FLAG_VALUE );
		}
	}

	if( components.size() > MAX_COMPONENT_TOGGLES )
	{
		FFGLLog::LogToHost(
			( std::string( "FaceKit: Model exposes " ) + std::to_string( components.size() ) +
			  " components; only the first " + std::to_string( MAX_COMPONENT_TOGGLES ) +
			  " have UI toggles. Remaining components stay visible." ).c_str()
		);
	}
}

void FaceKitPlugin::QueueModelRequestFromParams()
{
	auto pathParam = GetParamText( "modelPath" );
	std::string modelPath = pathParam ? pathParam->text : "";

	auto formatParam = std::dynamic_pointer_cast<ParamOption>( GetParam( "modelFormat" ) );
	FaceModelFormat modelFormat = FaceModelFormat_Auto;
	if( formatParam )
		modelFormat = (FaceModelFormat)(int)formatParam->GetRealValue();

	while( !modelPath.empty() && ( modelPath.back() == '/' || modelPath.back() == '\\' ) )
		modelPath.pop_back();

	{
		std::lock_guard<std::mutex> lk( m_requestedModelMutex );
		m_requestedModelPath = modelPath;
		m_requestedModelFormat = modelFormat;
	}
	m_modelRequestDirty = true;
}

void FaceKitPlugin::BeginModelLoad( const std::string& modelPath, FaceModelFormat modelFormat )
{
	SetStatusTextParam( "modelStatus", PI_MODEL_STATUS, m_modelStatusText, "Loading..." );
	FFGLLog::LogToHost( ( std::string( "FaceKit: Loading model " ) + modelPath ).c_str() );
	m_loadInProgress = true;
	m_loadThread = std::thread( [this, modelPath, modelFormat]()
	{
		FaceModelData loadedModel;
		std::string loadMessage;
		const bool ok = LoadFaceModel( modelPath, modelFormat, loadedModel, loadMessage );

		std::lock_guard<std::mutex> lk( m_pendingLoadMutex );
		m_pendingModel = std::move( loadedModel );
		m_pendingLoadSucceeded = ok;
		m_pendingLoadMessage = ok ? m_pendingModel.statusText : loadMessage;
		m_loadInProgress = false;
		m_loadFinished   = true;
	} );
}

void FaceKitPlugin::ApplyQueuedModelRequest( bool allowGlOps )
{
	if( !m_modelRequestDirty.exchange( false ) )
	{
		if( allowGlOps && m_modelUnloadRequested.exchange( false ) )
		{
			m_mesh.DeInitGL();
			m_mesh.Clear();
			RefreshMeshToggleParams( true );
		}
		return;
	}

	std::string modelPath;
	FaceModelFormat modelFormat = FaceModelFormat_Auto;
	{
		std::lock_guard<std::mutex> lk( m_requestedModelMutex );
		modelPath = m_requestedModelPath;
		modelFormat = m_requestedModelFormat;
	}

	if( modelPath == m_lastModelPath && modelFormat == m_lastModelFormat )
		return;

	m_lastModelPath = modelPath;
	m_lastModelFormat = modelFormat;

	if( m_loadThread.joinable() ) m_loadThread.join();
	m_loadFinished   = false;
	m_loadInProgress = false;

	if( modelPath.empty() )
	{
		if( allowGlOps )
		{
			m_mesh.DeInitGL();
			m_mesh.Clear();
			RefreshMeshToggleParams( true );
		}
		else
		{
			m_modelUnloadRequested = true;
		}
		SetStatusTextParam( "modelStatus", PI_MODEL_STATUS, m_modelStatusText, "No model path set" );
		return;
	}

	BeginModelLoad( modelPath, modelFormat );
}

static float ComputeFittedCameraDistance( const glm::vec3& boundsMin, const glm::vec3& boundsMax,
                                          const glm::vec3& target, float orbitXDeg, float orbitYDeg,
                                          float fovDeg, float aspect, const glm::vec3& up )
{
	const float verticalTan   = std::max( std::tan( glm::radians( std::max( fovDeg, 1.f ) ) * 0.5f ), 1e-3f );
	const float horizontalTan = std::max( verticalTan * std::max( aspect, 0.1f ), 1e-3f );

	const float ox = glm::radians( orbitXDeg );
	const float oy = glm::radians( orbitYDeg );
	glm::vec3 cameraDirection(
		glm::cos( ox ) * glm::sin( oy ),
		glm::sin( ox ),
		glm::cos( ox ) * glm::cos( oy )
	);
	if( glm::length( cameraDirection ) < 1e-6f )
		cameraDirection = glm::vec3( 0.f, 0.f, 1.f );
	else
		cameraDirection = glm::normalize( cameraDirection );

	glm::vec3 right = glm::cross( up, cameraDirection );
	if( glm::length( right ) < 1e-6f )
		right = glm::vec3( 1.f, 0.f, 0.f );
	else
		right = glm::normalize( right );
	const glm::vec3 cameraUp = glm::normalize( glm::cross( cameraDirection, right ) );

	float requiredDistance = 0.1f;
	for( int cornerIndex = 0; cornerIndex < 8; ++cornerIndex )
	{
		const glm::vec3 corner(
			( cornerIndex & 1 ) ? boundsMax.x : boundsMin.x,
			( cornerIndex & 2 ) ? boundsMax.y : boundsMin.y,
			( cornerIndex & 4 ) ? boundsMax.z : boundsMin.z
		);
		const glm::vec3 local = corner - target;
		const float x = glm::dot( local, right );
		const float y = glm::dot( local, cameraUp );
		const float z = glm::dot( local, cameraDirection );

		requiredDistance = std::max(
			requiredDistance,
			z + std::max( std::abs( x ) / horizontalTan, std::abs( y ) / verticalTan )
		);
	}

	return requiredDistance;
}

FFResult FaceKitPlugin::Render( ProcessOpenGLStruct* pGL )
{
	QueueModelRequestFromParams();
	ApplyQueuedModelRequest( true );

	// ── Promote completed load to GL on the render thread ─────────────────────
	if( m_loadFinished.exchange( false ) )
	{
		if( m_loadThread.joinable() ) m_loadThread.join();

		FaceModelData loadedModel;
		bool loadSucceeded = false;
		std::string loadMessage;
		{
			std::lock_guard<std::mutex> lk( m_pendingLoadMutex );
			loadedModel       = std::move( m_pendingModel );
			m_pendingModel    = FaceModelData();
			loadSucceeded     = m_pendingLoadSucceeded;
			loadMessage       = m_pendingLoadMessage;
		}

		if( loadSucceeded )
		{
			m_mesh.DeInitGL();
			m_mesh.AdoptModel( std::move( loadedModel ) );
			RefreshMeshToggleParams( true );
			if( !m_mesh.InitGL() )
			{
				SetStatusTextParam( "modelStatus", PI_MODEL_STATUS, m_modelStatusText, "OpenGL upload failed" );
				FFGLLog::LogToHost( "FaceKit: OpenGL upload failed for loaded model." );
			}
			else
			{
				SetStatusTextParam( "modelStatus", PI_MODEL_STATUS, m_modelStatusText, m_mesh.GetStatusText() );
				FFGLLog::LogToHost( ( std::string( "FaceKit: Loaded model: " ) + m_mesh.GetStatusText() ).c_str() );
				const std::vector<std::string>& warnings = m_mesh.GetWarnings();
				for( size_t i = 0; i < warnings.size(); ++i )
					FFGLLog::LogToHost( ( std::string( "FaceKit: " ) + warnings[i] ).c_str() );
			}
		}
		else
		{
			SetStatusTextParam( "modelStatus", PI_MODEL_STATUS, m_modelStatusText,
			                    loadMessage.empty() ? "Model load failed" : loadMessage );
			FFGLLog::LogToHost( ( std::string( "FaceKit: " ) + m_modelStatusText ).c_str() );
		}
	}

	// ── OSC port: restart receiver if changed ────────────────────────────────
	{
		auto portParam = std::dynamic_pointer_cast<ParamText>( GetParam( "oscPort" ) );
		std::string portStr = portParam ? portParam->text : "7400";
		if( portStr != m_oscPortLast )
		{
			int port = atoi( portStr.c_str() );
			if( port > 0 && port < 65536 )
			{
				m_oscPortLast = portStr;
				m_osc.Start( port, &m_faceState );
			}
		}
	}

	// ── OSC agent filter ──────────────────────────────────────────────────────
	{
		auto agentParam = std::dynamic_pointer_cast<ParamOption>( GetParam( "oscAgent" ) );
		const int selectionValue = agentParam ? (int)agentParam->GetRealValue() : 0;
		const OscAgentSelection selection = selectionValue <= 0
			? OscAgentSelection_Any
			: ( selectionValue == 1 ? OscAgentSelection_AgentA : OscAgentSelection_AgentB );
		if( selection != m_oscAgentLast )
		{
			m_oscAgentLast = selection;
			m_osc.SetAgentSelection( selection );
		}
	}

	// ── Status params ─────────────────────────────────────────────────────────
	{
		auto pathParam = GetParamText( "modelPath" );
		const bool hasModelPath = pathParam && !pathParam->text.empty();

		SetStatusTextParam( "oscStatus", PI_OSC_STATUS, m_oscStatusText, m_osc.GetStatusText() );

		if( !hasModelPath )
			SetStatusTextParam( "modelStatus", PI_MODEL_STATUS, m_modelStatusText, "No model path set" );
		else if( m_loadInProgress )
			SetStatusTextParam( "modelStatus", PI_MODEL_STATUS, m_modelStatusText, "Loading..." );
		else if( m_mesh.IsGLReady() && m_modelStatusText.empty() )
			SetStatusTextParam( "modelStatus", PI_MODEL_STATUS, m_modelStatusText, m_mesh.GetStatusText() );
	}

	// ── Lazy shader compile ───────────────────────────────────────────────────
	if( !m_glReady )
	{
		while( glGetError() != GL_NO_ERROR ) {}
		m_program = CompileProgram( kVS, kFS );
		if( !m_program ) return FF_FAIL;
		m_glReady = true;
		while( glGetError() != GL_NO_ERROR ) {}
	}

	// ── FBO ───────────────────────────────────────────────────────────────────
	GLint vp[4]; glGetIntegerv( GL_VIEWPORT, vp );
	int W = vp[2], H = vp[3];
	EnsureFBO( W, H );

	glBindFramebuffer( GL_FRAMEBUFFER, m_fbo );
	glViewport( 0, 0, W, H );
	glEnable( GL_DEPTH_TEST );
	glDepthFunc( GL_LEQUAL );

	glClearColor(
		GetParam( "bgR"       )->GetValue(),
		GetParam( "bgG"       )->GetValue(),
		GetParam( "bgB"       )->GetValue(),
		GetParam( "bgOpacity" )->GetValue()
	);
	glClear( GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT );

	if( !m_mesh.IsGLReady() )
	{
		// No mesh yet — blit empty frame
		glBindFramebuffer( GL_READ_FRAMEBUFFER, m_fbo );
		glBindFramebuffer( GL_DRAW_FRAMEBUFFER, pGL->HostFBO );
		glBlitFramebuffer( 0,0,W,H, 0,0,W,H, GL_COLOR_BUFFER_BIT, GL_NEAREST );
		glBindFramebuffer( GL_FRAMEBUFFER, pGL->HostFBO );
		glDisable( GL_DEPTH_TEST );
		glDepthFunc( GL_LESS );
		return FF_SUCCESS;
	}

	// ── Snapshot face state ───────────────────────────────────────────────────
	std::array<float, NUM_EXPRESSIONS> localExpr;
	std::array<float, NUM_IDENTITIES>  localId;
	float localPose[6];
	bool  idDirty;
	{
		std::lock_guard<std::mutex> lk( m_faceState.mutex );
		localExpr = m_faceState.exprWeights;
		localId   = m_faceState.idWeights;
		memcpy( localPose, m_faceState.headpose, sizeof( localPose ) );
		idDirty              = m_faceState.idDirty;
		m_faceState.idDirty  = false;
	}

	// ── Blend + upload ────────────────────────────────────────────────────────
	m_mesh.Update( localExpr.data(), localId.data(), idDirty );

	// ── FFGL parameters ───────────────────────────────────────────────────────
	auto renderOpt  = std::dynamic_pointer_cast<ParamOption>( GetParam( "renderLevel" ) );
	auto shadingOpt = std::dynamic_pointer_cast<ParamOption>( GetParam( "shading"     ) );
	int renderLevel = renderOpt  ? (int)renderOpt->GetRealValue()  : 0;
	int shadingMode = shadingOpt ? (int)shadingOpt->GetRealValue() : 0;
	std::array<bool, MAX_COMPONENT_TOGGLES> componentEnabled;
	componentEnabled.fill( true );
	for( size_t slot = 0; slot < MAX_COMPONENT_TOGGLES; ++slot )
	{
		auto toggleParam = std::dynamic_pointer_cast<ParamBool>( GetParam( m_componentParamNames[slot] ) );
		componentEnabled[slot] = !toggleParam || toggleParam->GetValue() >= 0.5f;
	}

	float colorR  = GetParam( "colorR"  )->GetValue();
	float colorG  = GetParam( "colorG"  )->GetValue();
	float colorB  = GetParam( "colorB"  )->GetValue();
	float lightX  = GetParam( "lightX"  )->GetValue() * 2.f - 1.f;
	float lightY  = GetParam( "lightY"  )->GetValue() * 2.f - 1.f;
	float lightZ  = GetParam( "lightZ"  )->GetValue() * 2.f - 1.f;
	float ambient = GetParam( "ambient" )->GetValue();
	float camZoom = remap( GetParam( "camZ"   )->GetValue(), 0.5f, 2.0f  );
	float orbitX  = remap( GetParam( "orbitX" )->GetValue(), -180.f, 180.f );
	float orbitY  = remap( GetParam( "orbitY" )->GetValue(), -180.f, 180.f );
	float fov     = remap( GetParam( "fov"    )->GetValue(), 10.f,  90.f  );

	// ── Matrices ──────────────────────────────────────────────────────────────
	float aspect = H > 0 ? (float)W / H : 1.f;

	// Model: head pose from OSC (tx, ty, tz in model units; rx, ry, rz in degrees)
	glm::mat4 model( 1.f );
	model = glm::translate( model, glm::vec3( localPose[0], localPose[1], localPose[2] ) );
	model = glm::rotate( model, glm::radians( localPose[3] ), glm::vec3( 1, 0, 0 ) );
	model = glm::rotate( model, glm::radians( localPose[4] ), glm::vec3( 0, 1, 0 ) );
	model = glm::rotate( model, glm::radians( localPose[5] ), glm::vec3( 0, 0, 1 ) );

	// Camera orbit around the static model bounds center so arbitrary assets stay framed.
	float ox = glm::radians( orbitX ), oy = glm::radians( orbitY );
	const glm::vec3 target = m_mesh.HasFocusPoint()
	                       ? m_mesh.GetFocusPoint()
	                       : ( m_mesh.HasBounds() ? m_mesh.GetBoundsCenter() : glm::vec3( 0.f ) );
	glm::vec3 up = glm::cos( ox ) >= 0.f ? glm::vec3( 0, 1, 0 ) : glm::vec3( 0, -1, 0 );
	const float fitDistance = m_mesh.HasBounds()
	                        ? ComputeFittedCameraDistance( m_mesh.GetBoundsMin(), m_mesh.GetBoundsMax(),
	                                                       target, orbitX, orbitY, fov, aspect, up ) * 1.05f
	                        : 25.f;
	const float camDistance = fitDistance * camZoom;
	glm::vec3 camPos(
		target.x + camDistance * glm::cos( ox ) * glm::sin( oy ),
		target.y + camDistance * glm::sin( ox ),
		target.z + camDistance * glm::cos( ox ) * glm::cos( oy )
	);

	const float modelRadius = std::max( m_mesh.GetBoundingRadius(), 0.1f );
	const float poseTranslation = glm::length( glm::vec3( localPose[0], localPose[1], localPose[2] ) );
	const float nearPlane = std::max( 0.01f, camDistance - modelRadius * 2.5f - poseTranslation );
	const float farPlane  = std::max( nearPlane + 10.f, camDistance + modelRadius * 4.f + poseTranslation + 10.f );

	glm::mat4 view      = glm::lookAt( camPos, target, up );
	glm::mat4 proj      = glm::perspective( glm::radians( fov ), aspect, nearPlane, farPlane );
	glm::mat4 mvp       = proj * view * model;
	glm::mat3 normalMat = glm::transpose( glm::inverse( glm::mat3( model ) ) );

	// ── Draw ──────────────────────────────────────────────────────────────────
	glEnable( GL_BLEND );
	glBlendFunc( GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA );

	bool wireframe = ( shadingMode == 2 );
	if( wireframe )
		glPolygonMode( GL_FRONT_AND_BACK, GL_LINE );

	glUseProgram( m_program );
	glUniformMatrix4fv( glGetUniformLocation( m_program, "uMVP"        ), 1, GL_FALSE, glm::value_ptr( mvp       ) );
	glUniformMatrix3fv( glGetUniformLocation( m_program, "uNormalMat"  ), 1, GL_FALSE, glm::value_ptr( normalMat ) );
	glUniform1i( glGetUniformLocation( m_program, "uShadingMode" ), shadingMode );
	glUniform3f( glGetUniformLocation( m_program, "uColor"       ), colorR, colorG, colorB );
	glUniform3f( glGetUniformLocation( m_program, "uLightDir"    ), lightX, lightY, lightZ );
	glUniform1f( glGetUniformLocation( m_program, "uAmbient"     ), ambient );

	m_mesh.Draw( renderLevel, componentEnabled.data(), componentEnabled.size() );

	glUseProgram( 0 );
	if( wireframe )
		glPolygonMode( GL_FRONT_AND_BACK, GL_FILL );
	glDisable( GL_CULL_FACE );
	glDisable( GL_BLEND );

	// ── Blit to host FBO ──────────────────────────────────────────────────────
	glBindFramebuffer( GL_READ_FRAMEBUFFER, m_fbo );
	glBindFramebuffer( GL_DRAW_FRAMEBUFFER, pGL->HostFBO );
	glBlitFramebuffer( 0,0,W,H, 0,0,W,H, GL_COLOR_BUFFER_BIT, GL_NEAREST );
	glBindFramebuffer( GL_FRAMEBUFFER, pGL->HostFBO );
	glDisable( GL_DEPTH_TEST );
	glDepthFunc( GL_LESS );

	return FF_SUCCESS;
}

FFResult FaceKitPlugin::SetFloatParameter( unsigned int index, float value )
{
	const FFResult result = Plugin::SetFloatParameter( index, value );
	if( result != FF_SUCCESS )
		return result;

	if( index == PI_MODEL_FORMAT )
		QueueModelRequestFromParams();

	return FF_SUCCESS;
}

FFResult FaceKitPlugin::SetTextParameter( unsigned int index, const char* value )
{
	const FFResult result = Plugin::SetTextParameter( index, value );
	if( result != FF_SUCCESS )
		return result;

	if( index == PI_MODEL_PATH )
		QueueModelRequestFromParams();

	return FF_SUCCESS;
}

// ── Helpers ───────────────────────────────────────────────────────────────────

void FaceKitPlugin::EnsureFBO( int w, int h )
{
	if( w == m_fboW && h == m_fboH ) return;

	if( m_fbo      ) glDeleteFramebuffers(  1, &m_fbo      );
	if( m_colorTex ) glDeleteTextures(      1, &m_colorTex );
	if( m_depthRbo ) glDeleteRenderbuffers( 1, &m_depthRbo );
	m_fboW = w; m_fboH = h;

	glGenTextures( 1, &m_colorTex );
	glBindTexture( GL_TEXTURE_2D, m_colorTex );
	glTexImage2D( GL_TEXTURE_2D, 0, GL_RGBA8, w, h, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR );
	glTexParameteri( GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR );
	glBindTexture( GL_TEXTURE_2D, 0 );

	glGenRenderbuffers( 1, &m_depthRbo );
	glBindRenderbuffer( GL_RENDERBUFFER, m_depthRbo );
	glRenderbufferStorage( GL_RENDERBUFFER, GL_DEPTH_COMPONENT24, w, h );
	glBindRenderbuffer( GL_RENDERBUFFER, 0 );

	glGenFramebuffers( 1, &m_fbo );
	glBindFramebuffer( GL_FRAMEBUFFER, m_fbo );
	glFramebufferTexture2D(    GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,   m_colorTex, 0 );
	glFramebufferRenderbuffer( GL_FRAMEBUFFER, GL_DEPTH_ATTACHMENT,  GL_RENDERBUFFER, m_depthRbo    );
	glBindFramebuffer( GL_FRAMEBUFFER, 0 );
}

GLuint FaceKitPlugin::CompileProgram( const char* vs, const char* fs )
{
	auto compile = []( GLenum type, const char* src ) -> GLuint
	{
		GLuint s = glCreateShader( type );
		glShaderSource( s, 1, &src, nullptr );
		glCompileShader( s );
		GLint ok; glGetShaderiv( s, GL_COMPILE_STATUS, &ok );
		if( !ok )
		{
			GLchar log[512]; glGetShaderInfoLog( s, sizeof( log ), nullptr, log );
			FFGLLog::LogToHost( ( std::string( "FaceKit shader: " ) + log ).c_str() );
			glDeleteShader( s ); return 0u;
		}
		return s;
	};
	GLuint v = compile( GL_VERTEX_SHADER,   vs );
	GLuint f = compile( GL_FRAGMENT_SHADER, fs );
	if( !v || !f ) { if(v) glDeleteShader(v); if(f) glDeleteShader(f); return 0; }
	GLuint prog = glCreateProgram();
	glAttachShader( prog, v ); glAttachShader( prog, f );
	glLinkProgram( prog );
	glDeleteShader( v ); glDeleteShader( f );
	GLint ok; glGetProgramiv( prog, GL_LINK_STATUS, &ok );
	if( !ok ) { glDeleteProgram( prog ); return 0; }
	return prog;
}
