// server/Server.hpp
#ifndef SERVER_HPP
#define SERVER_HPP

#ifdef _WIN32
    #include <winsock2.h>
    #include <ws2tcpip.h>
    using SOCKET_TYPE = SOCKET;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
using SOCKET_TYPE = int;
#endif

class Server {
public:
    Server(int port);
    ~Server();
    void Start();

private:
    int m_port;
    SOCKET_TYPE m_listenSocket;
    void HandleClient(SOCKET_TYPE clientSocket);
    void InitNetwork();
    void CleanNetwork();
};
#endif// server/Server.hpp
#ifndef SERVER_HPP
#define SERVER_HPP

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
using SOCKET_TYPE = SOCKET;
#else
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
using SOCKET_TYPE = int;
#endif

class Server {
public:
    Server(int port);
    ~Server();
    void Start();

private:
    int m_port;
    SOCKET_TYPE m_listenSocket;
    void HandleClient(SOCKET_TYPE clientSocket);
    void InitNetwork();
    void CleanNetwork();
};
#endif