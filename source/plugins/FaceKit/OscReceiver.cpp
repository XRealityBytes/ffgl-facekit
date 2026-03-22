#include "OscReceiver.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <cctype>

namespace
{
static const int kArkit52Count = 52;

enum MessageAgentSource
{
	MessageAgentSource_Unscoped = 0,
	MessageAgentSource_AgentA = 1,
	MessageAgentSource_AgentB = 2
};

const char* const kArkit52ExpressionNames[kArkit52Count] = {
	"browDownLeft",
	"browDownRight",
	"browInnerUp",
	"browOuterUpLeft",
	"browOuterUpRight",
	"cheekPuff",
	"cheekSquintLeft",
	"cheekSquintRight",
	"eyeBlinkLeft",
	"eyeBlinkRight",
	"eyeLookDownLeft",
	"eyeLookDownRight",
	"eyeLookInLeft",
	"eyeLookInRight",
	"eyeLookOutLeft",
	"eyeLookOutRight",
	"eyeLookUpLeft",
	"eyeLookUpRight",
	"eyeSquintLeft",
	"eyeSquintRight",
	"eyeWideLeft",
	"eyeWideRight",
	"jawForward",
	"jawLeft",
	"jawOpen",
	"jawRight",
	"mouthClose",
	"mouthDimpleLeft",
	"mouthDimpleRight",
	"mouthFrownLeft",
	"mouthFrownRight",
	"mouthFunnel",
	"mouthLeft",
	"mouthLowerDownLeft",
	"mouthLowerDownRight",
	"mouthPressLeft",
	"mouthPressRight",
	"mouthPucker",
	"mouthRight",
	"mouthRollLower",
	"mouthRollUpper",
	"mouthShrugLower",
	"mouthShrugUpper",
	"mouthSmileLeft",
	"mouthSmileRight",
	"mouthStretchLeft",
	"mouthStretchRight",
	"mouthUpperUpLeft",
	"mouthUpperUpRight",
	"noseSneerLeft",
	"noseSneerRight",
	"tongueOut"
};

float readF32be( const unsigned char* p )
{
	uint32_t i = ( (uint32_t)p[0] << 24 ) | ( (uint32_t)p[1] << 16 ) |
	             ( (uint32_t)p[2] << 8  ) |   (uint32_t)p[3];
	float f;
	memcpy( &f, &i, 4 );
	return f;
}

int32_t readI32be( const unsigned char* p )
{
	return (int32_t)( ( (uint32_t)p[0] << 24 ) | ( (uint32_t)p[1] << 16 ) |
	                  ( (uint32_t)p[2] << 8  ) |   (uint32_t)p[3] );
}

double readF64be( const unsigned char* p )
{
	uint64_t i =
		( (uint64_t)p[0] << 56 ) | ( (uint64_t)p[1] << 48 ) |
		( (uint64_t)p[2] << 40 ) | ( (uint64_t)p[3] << 32 ) |
		( (uint64_t)p[4] << 24 ) | ( (uint64_t)p[5] << 16 ) |
		( (uint64_t)p[6] << 8 )  |   (uint64_t)p[7];
	double f;
	memcpy( &f, &i, 8 );
	return f;
}

int pad4( int n ) { return ( n + 3 ) & ~3; }

std::string ToLowerCopy( const char* text )
{
	std::string lowered = text ? text : "";
	std::transform( lowered.begin(), lowered.end(), lowered.begin(),
	                []( unsigned char c ) { return (char)std::tolower( c ); } );
	return lowered;
}

bool StartsWith( const std::string& value, const char* prefix )
{
	const size_t prefixLen = strlen( prefix );
	return value.size() >= prefixLen && value.compare( 0, prefixLen, prefix ) == 0;
}

const char* MatchPrefix( const std::string& addrLower, const char* const* prefixes, size_t count )
{
	for( size_t index = 0; index < count; ++index )
	{
		if( StartsWith( addrLower, prefixes[index] ) )
			return prefixes[index];
	}
	return nullptr;
}

MessageAgentSource DetectMessageSource( const std::string& addrLower )
{
	if( StartsWith( addrLower, "/avatar-a/" ) || StartsWith( addrLower, "/agent-a/" ) ||
	    StartsWith( addrLower, "/dvoid/agent/a/" ) )
		return MessageAgentSource_AgentA;
	if( StartsWith( addrLower, "/avatar-b/" ) || StartsWith( addrLower, "/agent-b/" ) ||
	    StartsWith( addrLower, "/dvoid/agent/b/" ) )
		return MessageAgentSource_AgentB;
	return MessageAgentSource_Unscoped;
}

const char* OscAgentSelectionLabel( OscAgentSelection selection )
{
	switch( selection )
	{
	case OscAgentSelection_AgentA: return "Agent A";
	case OscAgentSelection_AgentB: return "Agent B";
	default: return "Any";
	}
}

bool AcceptsMessageSource( OscAgentSelection selection, MessageAgentSource source )
{
	if( source == MessageAgentSource_Unscoped )
		return true;
	if( selection == OscAgentSelection_Any )
		return true;
	return (int)source == (int)selection;
}

class OscArgStream
{
public:
	OscArgStream( const char* types, const unsigned char* args, int argLen )
	: m_types( types )
	, m_args( args )
	, m_argLen( argLen )
	{
	}

	bool ReadNumber( float& outValue )
	{
		while( m_types[m_typeIndex] != 0 )
		{
			const char type = m_types[m_typeIndex++];
			switch( type )
			{
			case 'f':
				if( m_argOffset + 4 > m_argLen ) return false;
				outValue = readF32be( m_args + m_argOffset );
				m_argOffset += 4;
				return true;
			case 'i':
				if( m_argOffset + 4 > m_argLen ) return false;
				outValue = (float)readI32be( m_args + m_argOffset );
				m_argOffset += 4;
				return true;
			case 'd':
				if( m_argOffset + 8 > m_argLen ) return false;
				outValue = (float)readF64be( m_args + m_argOffset );
				m_argOffset += 8;
				return true;
			case 'T':
				outValue = 1.f;
				return true;
			case 'F':
				outValue = 0.f;
				return true;
			default:
				if( !SkipArgument( type ) ) return false;
				break;
			}
		}
		return false;
	}

	bool ReadString( std::string& outValue )
	{
		while( m_types[m_typeIndex] != 0 )
		{
			const char type = m_types[m_typeIndex++];
			if( type == 's' || type == 'S' )
			{
				int len = 0;
				while( m_argOffset + len < m_argLen && m_args[m_argOffset + len] != 0 )
					++len;
				if( m_argOffset + len >= m_argLen )
					return false;
				outValue.assign( (const char*)( m_args + m_argOffset ), (size_t)len );
				m_argOffset += pad4( len + 1 );
				return m_argOffset <= m_argLen;
			}

			if( !SkipArgument( type ) )
				return false;
		}
		return false;
	}

private:
	bool SkipArgument( char type )
	{
		switch( type )
		{
		case 'h':
		case 't':
			if( m_argOffset + 8 > m_argLen ) return false;
			m_argOffset += 8;
			return true;
		case 's':
		case 'S':
		{
			int len = 0;
			while( m_argOffset + len < m_argLen && m_args[m_argOffset + len] != 0 )
				++len;
			m_argOffset += pad4( len + 1 );
			return m_argOffset <= m_argLen;
		}
		case 'b':
		{
			if( m_argOffset + 4 > m_argLen ) return false;
			const int blobLen = readI32be( m_args + m_argOffset );
			m_argOffset += 4 + pad4( blobLen );
			return m_argOffset <= m_argLen;
		}
		case 'N':
		case 'I':
			return true;
		default:
			return false;
		}
	}

	const char*          m_types;
	const unsigned char* m_args;
	int                  m_argLen = 0;
	size_t               m_typeIndex = 0;
	int                  m_argOffset = 0;
};
}

// ── OscReceiver ───────────────────────────────────────────────────────────────
OscReceiver::OscReceiver()  = default;
OscReceiver::~OscReceiver() { Stop(); }

void OscReceiver::Start( int port, FaceState* state )
{
	Stop();
	m_state   = state;
	m_port    = port;
	m_packetCount.store( 0, std::memory_order_relaxed );
	m_running = true;
	SetStatusText( "starting on " + std::to_string( port ) );
	m_thread  = std::thread( &OscReceiver::ThreadFunc, this );
}

void OscReceiver::Stop()
{
	m_running = false;
	m_socket.Close();
	if( m_thread.joinable() )
		m_thread.join();
	SetStatusText( "stopped" );
}

void OscReceiver::SetAgentSelection( OscAgentSelection selection )
{
	m_agentSelection.store( (int)selection, std::memory_order_relaxed );
}

std::string OscReceiver::GetStatusText() const
{
	std::lock_guard<std::mutex> lk( m_statusMutex );
	const OscAgentSelection selection =
		(OscAgentSelection)m_agentSelection.load( std::memory_order_relaxed );
	std::string status = m_statusText + " | agent: " + OscAgentSelectionLabel( selection ) +
	                     " | pkts: " + std::to_string( m_packetCount.load( std::memory_order_relaxed ) );
	if( !m_lastAddress.empty() )
		status += " | last: " + m_lastAddress;
	return status;
}

void OscReceiver::SetStatusText( const std::string& text )
{
	std::lock_guard<std::mutex> lk( m_statusMutex );
	m_statusText = text;
}

void OscReceiver::SetLastAddress( const std::string& addr )
{
	std::lock_guard<std::mutex> lk( m_statusMutex );
	m_lastAddress = addr.size() > 64 ? addr.substr( 0, 61 ) + "..." : addr;
}

void OscReceiver::ThreadFunc()
{
	std::string socketError;
	if( !m_socket.Open( m_port, 100, socketError ) )
	{
		SetStatusText( "bind failed on " + std::to_string( m_port ) + ": " + socketError );
		return;
	}

	SetStatusText( "listening on " + std::to_string( m_port ) );

	static unsigned char buf[65536];
	while( m_running )
	{
		int n = m_socket.Receive( buf, sizeof( buf ), socketError );
		if( n > 0 )
			ParsePacket( buf, n );
		else if( n < 0 )
		{
			SetStatusText( "socket error on " + std::to_string( m_port ) + ": " + socketError );
			break;
		}
	}

	m_socket.Close();
}

void OscReceiver::ParsePacket( const unsigned char* data, int len )
{
	if( len < 8 ) return;

	// OSC bundle
	if( memcmp( data, "#bundle\0", 8 ) == 0 )
	{
		int offset = 16;  // skip "#bundle\0" + timetag
		while( offset + 4 <= len )
		{
			int sz = readI32be( data + offset );
			offset += 4;
			if( sz > 0 && offset + sz <= len )
				ParsePacket( data + offset, sz );
			offset += sz;
		}
		return;
	}

	// OSC message
	if( data[0] != '/' ) return;

	int addrEnd = 0;
	while( addrEnd < len && data[addrEnd] != 0 ) addrEnd++;
	int typeStart = pad4( addrEnd + 1 );
	if( typeStart >= len || data[typeStart] != ',' ) return;

	int typeEnd = typeStart;
	while( typeEnd < len && data[typeEnd] != 0 ) typeEnd++;
	int argStart = pad4( typeEnd + 1 );

	HandleMessage(
		(const char*)data,
		(const char*)( data + typeStart + 1 ),  // skip leading ','
		data + argStart,
		len - argStart
	);
}

void OscReceiver::HandleMessage( const char* addr, const char* types,
                                 const unsigned char* args, int argLen )
{
	if( !m_state ) return;
	m_packetCount.fetch_add( 1, std::memory_order_relaxed );
	SetLastAddress( addr ? addr : "" );

	const std::string addrLower = ToLowerCopy( addr );
	const MessageAgentSource messageSource = DetectMessageSource( addrLower );
	const OscAgentSelection  agentSelection =
		(OscAgentSelection)m_agentSelection.load( std::memory_order_relaxed );
	if( !AcceptsMessageSource( agentSelection, messageSource ) )
		return;

	auto readFloats = [&]( float* out, int n )
	{
		OscArgStream stream( types, args, argLen );
		int count = 0;
		float value = 0.f;
		while( count < n && stream.ReadNumber( value ) )
			out[count++] = value;
		return count;
		};

	auto applyBlendshapeValue = [&]( const std::string& name, float value )
	{
		const MorphTargetMatch match = ResolveMorphTargetName( name );
		for( size_t i = 0; i < match.indices.size(); ++i )
		{
			const int index = match.indices[i];
			if( index >= 0 && index < NUM_EXPRESSIONS )
				m_state->exprWeights[index] = value;
		}
		return !match.indices.empty();
	};

	auto applyArkit52Frame = [&]()
	{
		float values[kArkit52Count] = {};
		const int count = readFloats( values, kArkit52Count );
		bool appliedAny = false;
		for( int index = 0; index < count; ++index )
			appliedAny = applyBlendshapeValue( kArkit52ExpressionNames[index], values[index] ) || appliedAny;
		return appliedAny;
	};

	std::lock_guard<std::mutex> lk( m_state->mutex );

	// /Avatar-A/arkit52 or /Avatar-B/arkit52 — 52 floats, then frame index + timestamp
	if( addrLower == "/avatar-a/arkit52" || addrLower == "/avatar-b/arkit52" ||
	    addrLower == "/arkit52" || addrLower == "/facekit/arkit52" )
	{
		applyArkit52Frame();
		return;
	}

	// /facekit/blendshapes  — 53 floats
	if( addrLower == "/facekit/blendshapes" || addrLower == "/blendshapes" ||
	    addrLower == "/facekit/blendshape" || addrLower == "/blendshape" )
	{
		if( types && ( types[0] == 's' || types[0] == 'S' ) )
		{
			OscArgStream stream( types, args, argLen );
			std::string  name;
			float        value = 0.f;
			bool         appliedAny = false;
			while( stream.ReadString( name ) )
			{
				if( !stream.ReadNumber( value ) )
					break;
				appliedAny = applyBlendshapeValue( name, value ) || appliedAny;
			}
			if( appliedAny )
				return;
		}

		readFloats( m_state->exprWeights.data(), NUM_EXPRESSIONS );
		return;
	}
	// /facekit/blendshape/{name}  — 1 float
	static const char* const kBlendshapePrefixes[] = {
		"/facekit/blendshape/",
		"/facekit/blendshapes/",
		"/blendshape/",
		"/blendshapes/"
	};
	if( const char* matchedPrefix = MatchPrefix( addrLower, kBlendshapePrefixes,
	                                             sizeof( kBlendshapePrefixes ) / sizeof( kBlendshapePrefixes[0] ) ) )
	{
		float value = 0.f;
		OscArgStream stream( types, args, argLen );
		if( !stream.ReadNumber( value ) )
			return;
		applyBlendshapeValue( addr + strlen( matchedPrefix ), value );
		return;
	}
	// /facekit/identity  — 100 floats
	if( addrLower == "/facekit/identity" || addrLower == "/identity" )
	{
		readFloats( m_state->idWeights.data(), NUM_IDENTITIES );
		m_state->idDirty = true;
		return;
	}
	// /facekit/identity/{index}  — 1 float
	static const char* const kIdentityPrefixes[] = {
		"/facekit/identity/",
		"/identity/"
	};
	if( const char* matchedPrefix = MatchPrefix( addrLower, kIdentityPrefixes,
	                                             sizeof( kIdentityPrefixes ) / sizeof( kIdentityPrefixes[0] ) ) )
	{
		float value = 0.f;
		OscArgStream stream( types, args, argLen );
		if( !stream.ReadNumber( value ) )
			return;
		int idx = atoi( addr + strlen( matchedPrefix ) );
		if( idx >= 0 && idx < NUM_IDENTITIES )
		{
			m_state->idWeights[idx] = value;
			m_state->idDirty        = true;
		}
		return;
	}
	// /facekit/headpose  — 6 floats: tx ty tz rx ry rz
	if( addrLower == "/facekit/headpose" || addrLower == "/facekit/pose" || addrLower == "/headpose" )
	{
		readFloats( m_state->headpose, 6 );
		return;
	}
	// /facekit/reset
	if( addrLower == "/facekit/reset" || addrLower == "/reset" )
	{
		m_state->exprWeights.fill( 0.f );
		m_state->idWeights.fill( 0.f );
		memset( m_state->headpose, 0, sizeof( m_state->headpose ) );
		m_state->idDirty = true;
		return;
	}
}
