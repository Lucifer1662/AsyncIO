// AsyncIO.cpp : Defines the entry point for the application.
//

#include "AsyncIO.h"

#include "winsock.h"

#include <thread>
#include <vector>

#include "Socket.h"


#define CLOSESOCK(s)           \
    if (INVALID_SOCKET != s) { \
        closesocket(s);        \
        s = INVALID_SOCKET;    \
    }

#define DEFAULT_WAIT 30000

#define WS_VER 0x0202

#define DEFAULT_PORT 12345

#define TST_MSG "0123456789abcdefghijklmnopqrstuvwxyz\0"


#include "Context.h"

DWORD WINAPI ConnectThread() {
    Context context;

    WSAPOLLFD fdarray = {0};

    INT ret = 0;
    CHAR buf[MAX_PATH] = {0};

    Socket mySocket;

    try {
        if (mySocket.valid()) {
            throw std::exception("invalid socket");
        }

        context.addPollObject(PollObject(mySocket.csock));

        char data[1000];

        context.addReader(mySocket.csock, (char*)&data, sizeof(TST_MSG), [&]() {
            std::cout << "Recieved: " << data << std::endl;
            return true;
        });

        context.addWriter(mySocket.csock, TST_MSG, sizeof(TST_MSG),
                          [&](auto amount) {
                              std::cout << "Wrote: " << TST_MSG << std::endl;
                              return true;
                          });

        bool connect = mySocket.connect("127.0.0.1", DEFAULT_PORT);

        mySocket.blocking(false);

        std::cout << connect << std::endl;

        context.run();

    } catch (std::exception e) {
    }

    return 0;
}

int main() {
    WSADATA wsd;
    INT nStartup = 0, nErr = 0, ret = 0;
    SOCKET lsock = INVALID_SOCKET, asock = INVALID_SOCKET;
    SOCKADDR_STORAGE addr = {0};
    WSAPOLLFD fdarray = {0};
    ULONG uNonBlockingMode = 1;
    CHAR buf[MAX_PATH] = {0};
    HANDLE hThread = NULL;
    DWORD dwThreadId = 0;

    try {
        nErr = WSAStartup(WS_VER, &wsd);
        if (nErr) {
            WSASetLastError(nErr);
            throw std::exception("WSAStartup");
        } else
            nStartup++;


        auto clientThread = std::thread(ConnectThread);

        addr.ss_family = AF_INET6;
        INETADDR_SETANY((SOCKADDR*)&addr);
        SS_PORT((SOCKADDR*)&addr) = htons(DEFAULT_PORT);

        if (INVALID_SOCKET == (lsock = socket(AF_INET6, SOCK_STREAM, 0))) {
            throw std::exception("socket");
        }

        if (SOCKET_ERROR == ioctlsocket(lsock, FIONBIO, &uNonBlockingMode)) {
            throw std::exception("FIONBIO");
        }

        if (SOCKET_ERROR == bind(lsock, (SOCKADDR*)&addr, sizeof(addr))) {
            throw std::exception("bind");
        }

        if (SOCKET_ERROR == listen(lsock, 1)) {
            throw std::exception("listen");
        }

        // Call WSAPoll for readability of listener (accepted)

        fdarray.fd = lsock;
        fdarray.events = POLLRDNORM;

        if (SOCKET_ERROR == (ret = WSAPoll(&fdarray, 1, DEFAULT_WAIT))) {
            throw std::exception("WSAPoll");
        }

        if (ret) {
            if (fdarray.revents & POLLRDNORM) {
                printf("Main: Connection established.\n");

                if (INVALID_SOCKET == (asock = accept(lsock, NULL, NULL))) {
                    throw std::exception("accept");
                }

                if (SOCKET_ERROR == (ret = recv(asock, buf, sizeof(buf), 0))) {
                    throw std::exception("recv");
                } else{
                    printf("Main: recvd %d bytes\n", ret);
                    send(asock,buf,ret, 0);
                }   
            }
        }

        // Call WSAPoll for writeability of accepted

        fdarray.fd = asock;
        fdarray.events = POLLWRNORM;

        if (SOCKET_ERROR == (ret = WSAPoll(&fdarray, 1, DEFAULT_WAIT))) {
            throw std::exception("WSAPoll");
        }

        if (ret) {
            if (fdarray.revents & POLLWRNORM) {
                if (SOCKET_ERROR ==
                    (ret = send(asock, TST_MSG, sizeof(TST_MSG), 0))) {
                    throw std::exception("send");
                } else
                    printf("Main: sent %d bytes\n", ret);
            }
        }



        clientThread.join();
    }catch(std::exception e){

    }

    CLOSESOCK(asock);
    CLOSESOCK(lsock);
    if (nStartup) WSACleanup();

    return 0;
}