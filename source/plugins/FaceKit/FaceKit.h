#pragma once
#include <FFGLSDK.h>
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "FaceMesh.h"
#include "OscReceiver.h"

using namespace ffglqs;

class FaceKitPlugin : public Source
{
public:
	static constexpr size_t MAX_COMPONENT_TOGGLES = 48;

	FaceKitPlugin();

	FFResult Init() override;
	void     Clean()  override;
	FFResult Render( ProcessOpenGLStruct* pGL ) override;
	FFResult SetFloatParameter( unsigned int index, float value ) override;
	FFResult SetTextParameter( unsigned int index, const char* value ) override;

private:
	// GL
	GLuint m_program  = 0;
	GLuint m_fbo      = 0;
	GLuint m_colorTex = 0;
	GLuint m_depthRbo = 0;
	int    m_fboW     = 0;
	int    m_fboH     = 0;
	bool   m_glReady  = false;

	// Face data — loaded on a background thread to avoid blocking Resolume
	FaceMesh             m_mesh;
	std::string          m_lastModelPath;
	FaceModelFormat      m_lastModelFormat = FaceModelFormat_Auto;
	std::thread          m_loadThread;
	std::atomic<bool>    m_loadFinished{ false };
	std::atomic<bool>    m_loadInProgress{ false };
	std::mutex           m_pendingLoadMutex;
	FaceModelData        m_pendingModel;
	bool                 m_pendingLoadSucceeded = false;
	std::string          m_pendingLoadMessage;
	std::mutex           m_requestedModelMutex;
	std::string          m_requestedModelPath;
	FaceModelFormat      m_requestedModelFormat = FaceModelFormat_Auto;
	std::atomic<bool>    m_modelRequestDirty{ false };
	std::atomic<bool>    m_modelUnloadRequested{ false };

	// OSC
	FaceState   m_faceState;
	OscReceiver m_osc;
	std::string m_oscPortLast;
	OscAgentSelection m_oscAgentLast = OscAgentSelection_Any;

	// Status state
	std::string m_modelStatusText = "No model path set";
	std::string m_oscStatusText   = "port 7400 | pkts: 0";
	std::array<std::string, MAX_COMPONENT_TOGGLES> m_componentParamNames;

	void   EnsureFBO( int w, int h );
	GLuint CompileProgram( const char* vs, const char* fs );
	void   QueueModelRequestFromParams();
	void   ApplyQueuedModelRequest( bool allowGlOps );
	void   BeginModelLoad( const std::string& modelPath, FaceModelFormat modelFormat );
	void   RefreshMeshToggleParams( bool resetValues );
	void   SetStatusTextParam( const char* paramName, unsigned int paramIndex,
	                           std::string& cache, const std::string& text );
};
