// AsyncIO.cpp : Defines the entry point for the application.
//

#include "AsyncIO.h"

#include <thread>
#include <vector>

#include "Context.h"
#include "Socket.h"
#include "Timer.h"
#include "winsock.h"


#define DEFAULT_WAIT 30000

#define DEFAULT_PORT 12345

#define TST_MSG "0123456789abcdefghijklmnopqrstuvwxyz\0"
#define REPLY_MSG "Hello There"




void ConnectThread() {
    Context context;
    CHAR buf[MAX_PATH] = {0};

    Socket mySocket;

    try {
        context.addPollObject(mySocket.csock);

        char data[1000];

        
        //CharStarBuffer(data)
        context.addReader(mySocket.csock,
        std::make_unique<FixedSizeReader>(sizeof(TST_MSG), [&](auto buf, auto size){memcpy(data, buf, size);}),
        [&]() {
            std::cout << "Recieved: " << data << std::endl;
            return true;
        });


        context.addWriter(mySocket.csock, TST_MSG, sizeof(TST_MSG),
                          [&](auto amount) {
                            //   std::cout << "Wrote: " << TST_MSG << std::endl;
                              return true;
                          });


        bool connect = mySocket.connect("127.0.0.1", DEFAULT_PORT);

        mySocket.blocking(false);


        std::cout << connect << std::endl;

        context.run();

    } catch (std::exception e) {
    }
}

int main() {
    std::unique_ptr<std::thread> clientThread;

    try {

        Context context;

        Timer timer1(context, 2000 + context.now(),
                     []() { std::cout << "timer1" << std::endl; });
        timer1.start();

        Interval timer(context, 2000,
                       []() { std::cout << "timer" << std::endl; });
        timer.start();

        clientThread.reset(new std::thread(ConnectThread));

        Socket acceptor;
        acceptor.blocking(false);
        context.addPollObject(acceptor.csock);

        acceptor.listen(DEFAULT_PORT);
        

        context.addReader(acceptor.csock, 
        std::make_unique<FixedSizeReader>(0, [](auto buf, auto size){}),
        [&]() {
            auto mySocket = acceptor.accept();

            context.addPollObject(mySocket.csock);

            char data[1000];

            context.addReader(mySocket.csock,
                std::make_unique<FixedSizeReader>(sizeof(TST_MSG), [&](auto buf, auto size){memcpy(data, buf, size);}),
                [&]() {
                    std::cout << "Recieved: " << data << std::endl;

                    context.addWriter(mySocket.csock, REPLY_MSG,
                                      sizeof(REPLY_MSG), [&](auto amount) {
                                          std::cout << "Wrote: " << REPLY_MSG
                                                    << std::endl;
                                          return false;
                                      });

                    return true;
                });


            size_t size;
            context.addReader(mySocket.csock,
                std::make_unique<FixedSizeReader>(sizeof(size), [&](auto buf, auto size){memcpy(&size, buf, size);}),
                [&]() {                   
                    return true;
                });

            context.addReader(mySocket.csock,
                std::make_unique<FixedSizeReader>(size, [&](auto buf, auto size){memcpy(&size, buf, size);}),
                [&]() {                   
                    return true;
                });

            mySocket.blocking(false);
            
            return false;
        });

        context.run();

    } catch (std::exception e) {

    }

    if (clientThread) clientThread->join();

  

    return 0;
}
