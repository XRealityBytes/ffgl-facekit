#pragma once
#include <array>
#include <atomic>
#include <mutex>
#include <string>
#include <thread>

#include "FaceMorphTargets.h"
#include "UdpSocket.h"

enum OscAgentSelection
{
	OscAgentSelection_Any = 0,
	OscAgentSelection_AgentA = 1,
	OscAgentSelection_AgentB = 2
};

// Shared state written by the OSC thread, read by the render thread
struct FaceState
{
	std::array<float, NUM_EXPRESSIONS> exprWeights = {};
	std::array<float, NUM_IDENTITIES>  idWeights   = {};
	float headpose[6] = {};  // tx, ty, tz, rx, ry, rz
	bool  idDirty     = false;
	std::mutex mutex;
};

class OscReceiver
{
public:
	OscReceiver();
	~OscReceiver();

	// Start listening on udpPort; state must outlive this object.
	void Start( int udpPort, FaceState* state );
	void Stop();
	void SetAgentSelection( OscAgentSelection selection );
	std::string GetStatusText() const;

private:
	void        ThreadFunc();
	void        ParsePacket( const unsigned char* data, int len );
	void        HandleMessage( const char* addr, const char* types,
	                           const unsigned char* args, int argLen );
	void        SetStatusText( const std::string& text );
	void        SetLastAddress( const std::string& addr );

	FaceState*             m_state      = nullptr;
	std::thread            m_thread;
	std::atomic<bool>      m_running{ false };
	UdpSocket              m_socket;
	int                    m_port       = 7400;
	std::atomic<uint32_t>  m_packetCount{ 0 };
	std::atomic<int>       m_agentSelection{ OscAgentSelection_Any };
	mutable std::mutex     m_statusMutex;
	std::string            m_statusText = "stopped";
	std::string            m_lastAddress;

public:
	uint32_t GetPacketCount() const { return m_packetCount.load(); }
	int      GetPort()        const { return m_port; }
};
