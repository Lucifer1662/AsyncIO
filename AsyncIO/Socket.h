#pragma once

#include <mstcpip.h>
#include <stdio.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include "winsock.h"

struct Socket {
    SOCKET csock = INVALID_SOCKET;

    Socket() : csock(socket(AF_INET6, SOCK_STREAM, 0)) {}
    Socket(SOCKET csock) : csock(csock) {}

    bool blocking(bool block) {
        ULONG uNonBlockingMode = block ? 0 : 1;
        return SOCKET_ERROR == ioctlsocket(csock, FIONBIO, &uNonBlockingMode);
    }

    int read(char* data, size_t amount) { return recv(csock, data, amount, 0); }

    int send(const char* data, size_t amount) {
        return ::send(csock, data, amount, 0);
    }

    void listen(int port) {
        SOCKADDR_STORAGE addr = {0};
        addr.ss_family = AF_INET6;
        INETADDR_SETANY((SOCKADDR*)&addr);
        SS_PORT((SOCKADDR*)&addr) = htons(port);

        if (SOCKET_ERROR == bind(csock, (SOCKADDR*)&addr, sizeof(addr))) {
            throw std::exception("bind");
        }

        if (SOCKET_ERROR == ::listen(csock, 1)) {
            throw std::exception("listen");
        }
    }

    Socket accept(){
        auto sock = ::accept(csock, NULL, NULL);
        return Socket(sock);
    }

    void accept(Socket& socket){
        socket.csock = ::accept(csock, NULL, NULL);
    }

    bool connect(const char* ip, int port) {
        SOCKADDR_STORAGE clientService = {0};
        clientService.ss_family = AF_INET6;
        INETADDR_SETLOOPBACK((SOCKADDR*)&clientService);
        // clientService.sin_addr.s_addr = inet_addr(ip);
        SS_PORT((SOCKADDR*)&clientService) = htons(port);
        // clientService.sin_port = htons(port);

        return SOCKET_ERROR != ::connect(csock, (SOCKADDR*)&clientService,
                                         sizeof(clientService));
    }

    bool valid() { return INVALID_SOCKET == csock; }

    void close() {
        if (INVALID_SOCKET != csock) {
            closesocket(csock);
            csock = INVALID_SOCKET;
        }
    }
};