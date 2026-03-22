#include "UdpSocket.h"

#include <climits>
#include <cstring>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#pragma comment( lib, "Ws2_32.lib" )
#else
#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace
{
#ifdef _WIN32
typedef SOCKET SocketHandle;
const SocketHandle INVALID_SOCKET_HANDLE = INVALID_SOCKET;

std::string SocketErrorString( int err )
{
	char* message = nullptr;
	FormatMessageA(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
		nullptr, err, MAKELANGID( LANG_NEUTRAL, SUBLANG_DEFAULT ), (LPSTR)&message, 0, nullptr
	);
	std::string out = message ? message : "Unknown socket error";
	if( message ) LocalFree( message );
	return out;
}
#else
typedef int SocketHandle;
const SocketHandle INVALID_SOCKET_HANDLE = -1;

std::string SocketErrorString( int err )
{
	return std::strerror( err );
}
#endif

SocketHandle ToHandle( unsigned long long value )
{
	return (SocketHandle)value;
}
}

UdpSocket::UdpSocket() = default;

UdpSocket::~UdpSocket()
{
	Close();
}

bool UdpSocket::Open( int port, int timeoutMs, std::string& outError )
{
	Close();

#ifdef _WIN32
	WSADATA wsaData;
	if( WSAStartup( MAKEWORD( 2, 2 ), &wsaData ) != 0 )
	{
		outError = "WSAStartup failed.";
		return false;
	}
#endif

	SocketHandle socketHandle = socket( AF_INET, SOCK_DGRAM, IPPROTO_UDP );
	if( socketHandle == INVALID_SOCKET_HANDLE )
	{
#ifdef _WIN32
		outError = SocketErrorString( WSAGetLastError() );
		WSACleanup();
#else
		outError = SocketErrorString( errno );
#endif
		return false;
	}

	int reuse = 1;
	setsockopt( socketHandle, SOL_SOCKET, SO_REUSEADDR, (const char*)&reuse, sizeof( reuse ) );

#ifdef _WIN32
	DWORD timeout = (DWORD)timeoutMs;
	setsockopt( socketHandle, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof( timeout ) );
#else
	struct timeval timeout;
	timeout.tv_sec  = timeoutMs / 1000;
	timeout.tv_usec = ( timeoutMs % 1000 ) * 1000;
	setsockopt( socketHandle, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof( timeout ) );
#endif

	sockaddr_in addr;
	memset( &addr, 0, sizeof( addr ) );
	addr.sin_family      = AF_INET;
	addr.sin_port        = htons( (unsigned short)port );
	addr.sin_addr.s_addr = htonl( INADDR_ANY );

	if( bind( socketHandle, (sockaddr*)&addr, sizeof( addr ) ) != 0 )
	{
#ifdef _WIN32
		outError = SocketErrorString( WSAGetLastError() );
		closesocket( socketHandle );
		WSACleanup();
#else
		outError = SocketErrorString( errno );
		close( socketHandle );
#endif
		return false;
	}

	m_socket = (unsigned long long)socketHandle;
	return true;
}

void UdpSocket::Close()
{
	if( m_socket == ~0ull )
		return;

	SocketHandle socketHandle = ToHandle( m_socket );
#ifdef _WIN32
	closesocket( socketHandle );
	WSACleanup();
#else
	close( socketHandle );
#endif
	m_socket = ~0ull;
}

int UdpSocket::Receive( unsigned char* buffer, int bufferSize, std::string& outError )
{
	outError.clear();
	if( !IsOpen() )
		return -1;

	SocketHandle socketHandle = ToHandle( m_socket );
	const auto received = recvfrom( socketHandle, (char*)buffer, bufferSize, 0, nullptr, nullptr );
	if( received >= 0 )
		return received > INT_MAX ? INT_MAX : (int)received;

#ifdef _WIN32
	const int err = WSAGetLastError();
	if( err == WSAETIMEDOUT || err == WSAEWOULDBLOCK || err == WSAEINTR )
		return 0;
	outError = SocketErrorString( err );
#else
	if( errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR )
		return 0;
	outError = SocketErrorString( errno );
#endif
	return -1;
}

bool UdpSocket::IsOpen() const
{
	return m_socket != ~0ull;
}
