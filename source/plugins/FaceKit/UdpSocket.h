#pragma once

#include <string>

class UdpSocket
{
public:
	UdpSocket();
	~UdpSocket();

	bool Open( int port, int timeoutMs, std::string& outError );
	void Close();

	int  Receive( unsigned char* buffer, int bufferSize, std::string& outError );
	bool IsOpen() const;

private:
	unsigned long long m_socket = ~0ull;
};
