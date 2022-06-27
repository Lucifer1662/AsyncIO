
#pragma once
#include <functional>
#include <list>
#include <map>
#include <memory>
#include <sstream>
#include <unordered_map>
#include <vector>

#include "chrono"
#include "winsock.h"

class PollObject : public WSAPOLLFD {
   public:
    PollObject(SOCKET fd) {
        this->fd = fd;
        this->events = 0;
        this->revents = 0;
    }

   public:
    void listforRead(bool enable) {
        if (enable) {
            events = events | POLLRDNORM;
        } else {
            events = events & ~POLLRDNORM;
        }
    }

    void listforWrite(bool enable) {
        if (enable) {
            events = events | POLLWRNORM;
        } else {
            events = events & ~POLLWRNORM;
        }
    }

    bool eventOccured() { return revents != 0; }

    bool readEventOccured() { return (revents & POLLRDNORM) == POLLRDNORM; }

    bool writeEventOccured() { return (revents & POLLWRNORM) == POLLWRNORM; }

    void listForReadOrWrite(bool enable) {
        listforRead(enable);
        listforWrite(enable);
    }
};

struct Data {
    Data(char* data, size_t size) : data(data), size(size) {}

    char* data;
    size_t size;
    size_t offset = 0;
    std::function<bool(char, size_t)> readerFunc;

    size_t amountOfDataNeeded() { return size - offset; }

    char* readIn(char* buf, size_t buf_size) {
        size_t i = 0;
        while (i < buf_size) {
            auto jump = readerFunc(buf[i], i + offset);
            if (jump == 0) {
                break;
            }
            i += jump;
        }
        memcpy(data + offset, buf, i);
        return buf + i;
    }
};

#include <experimental/coroutine>
#include <thread>

 
struct ReturnObject2 {
  struct promise_type {
    ReturnObject2 get_return_object() {
      return {
        // Uses C++20 designated initializer syntax
        .h_ = std::experimental::coroutine_handle<promise_type>::from_promise(*this)
      };
    }
    std::experimental::suspend_never initial_suspend() { return {}; }
    std::experimental::suspend_never final_suspend() noexcept { return {}; }
    void unhandled_exception() {}
    void return_void(){}
  };

  std::experimental::coroutine_handle<promise_type> h_;
  operator std::experimental::coroutine_handle<promise_type>() const { return h_; }
  // A coroutine_handle<promise_type> converts to coroutine_handle<>
  operator std::experimental::coroutine_handle<>() const { return h_; }
};


ReturnObject2
counter2(SOCKET socket, char* data, size_t size, int& amoutnRecieved)
{
  while(true) {
    co_await std::experimental::suspend_always{};
    amoutnRecieved = recv(socket, data, size, 0);
  }
}



class SocketBuf
    : public std::streambuf {
    char*           buffer_;
    SOCKET fd;
    // context for the compression
public:
    SocketBuf(SOCKET fd): buffer_(new char[1024]), fd(fd) {
        // initialize compression context
    }
    ~SocketBuf() { delete[] this->buffer_; }

    int underflow() {
        if (this->gptr() == this->egptr()) {
            auto amountRecieved = recv(fd, buffer_, 1024, 0);
            if(amountRecieved == -1){
                // throw std::exception("more data coming");
                return std::char_traits<char>::to_int_type(*this->gptr());
            }
            
            this->setg(this->buffer_, this->buffer_, this->buffer_ + amountRecieved);
            return std::char_traits<char>::to_int_type(*this->gptr());
        }else{
            throw std::exception("more data coming");
        }      
    }
};



template <typename Func>
using FuncQueue = std::list<std::function<Func>>;

struct SocketMeta {
    FuncQueue<bool()> reads;
    SocketBuf buf;
    

    std::list<std::function<bool(std::istream&)>> readDataQueue;

    SocketMeta(SOCKET socket): buf(socket){}

    void executeFront(){
        auto addToBack = reads.front()(); 
        if (addToBack) {
            reads.push_back(reads.front());
            readDataQueue.push_back(std::move(readDataQueue.front()));
        }
        reads.pop_front();
        readDataQueue.pop_front();
    }


    void readIn(SOCKET fd) {
        auto istream = std::istream(&buf);  
        int amountRecieved = 0;
        char buff[100];
        std::experimental::coroutine_handle<> h = counter2(fd, buff, 100, amountRecieved);

        while (reads.size() > 0 && amountRecieved != -1) {

                // h();


            // amountRecieved = recv(fd, buff, 100,0);
            // if(amountRecieved != -1)
                // buf.sputn(buff, amountRecieved);

            auto pos = istream.tellg();
            if(readDataQueue.front()(istream)){
                executeFront();
            } else if(pos == istream.tellg()){
                break;
            }

         
        }
        
        h.destroy();

           
    }

    FuncQueue<bool(size_t)> writes;
    std::list<Data> writeDataQueue;

};

#define WS_VER 0x0202

class Context {
    std::vector<PollObject> fdarray;
    std::unordered_map<SOCKET, size_t> socketToIndex;
    std::vector<std::unique_ptr<SocketMeta>> socketMetaMap;
    std::map<long long, FuncQueue<void()>> timers;

    bool running = false;
    int waitPeriod = 1;
    bool didStartWSA;

    int poll(int waitPeriod) {
        if (fdarray.size() > 0) {
            return WSAPoll(fdarray.data(), fdarray.size(), waitPeriod);
        } else if (!timers.empty()) {
            if (timers.begin()->first > now())
                Sleep(timers.begin()->first - now());
        } else {
            Sleep(waitPeriod * 1000);
        }
        return 0;
    }

   public:
    Context() {
        WSADATA wsd;
        auto nErr = WSAStartup(WS_VER, &wsd);
        if (nErr) {
            WSASetLastError(nErr);
            throw std::exception("WSAStartup");
        }
        didStartWSA = !nErr;
    }

    ~Context() {
        if (didStartWSA) WSACleanup();
    }

    long long now() {
        return std::chrono::duration_cast<std::chrono::milliseconds>(
                   std::chrono::system_clock::now().time_since_epoch())
            .count();
    }

    /*
    adds a PollObject to the be polled
    */
    void addPollObject(SOCKET socket) {
        socketToIndex.insert({socket, fdarray.size()});
        fdarray.emplace_back(socket);
        socketMetaMap.emplace_back(new SocketMeta(socket));
    }

    void removePollObject(SOCKET socket) {
        auto it = socketToIndex.find(socket);
        if (it != socketToIndex.end()) {
            auto index = it->second;

            // if not at the back
            if (index != fdarray.size()) {
                fdarray[index] = fdarray.back();
                socketToIndex[fdarray.back().fd] = index;
            }

            // remove from array and socket to index
            fdarray.pop_back();
            socketToIndex.erase(socket);
        }
    }

    template <typename Func1, typename Func2>
    void addReader(SOCKET socket, Func1&& reader, Func2&& func) {
        auto it = socketToIndex.find(socket);
        if (it != socketToIndex.end()) {
            auto index = it->second;

            auto& socketmeta = *socketMetaMap[index];
            socketmeta.reads.emplace_back(std::forward<Func2>(func));
            socketmeta.readDataQueue.emplace_back(std::forward<Func1>(reader));
            fdarray[index].listforRead(true);
        }
    }

    template <typename Func>
    void addWriter(SOCKET socket, char* data, size_t size, Func&& func) {
        auto it = socketToIndex.find(socket);
        if (it != socketToIndex.end()) {
            auto index = it->second;

            auto& socketmeta = *socketMetaMap[index];
            socketmeta.writes.emplace_back(std::forward<Func>(func));
            socketmeta.writeDataQueue.emplace_back(data, size);
            fdarray[index].listforWrite(true);
        }
    }

    template <typename Func>
    void addTimerIn(size_t in, Func&& func) {
        addTimer(in + now(), std::forward<Func>(func));
    }

    template <typename Func>
    void addTimer(long long at, Func&& func) {
        auto it = timers.find(at);
        if (it != timers.end()) {
            it->second.emplace_back(std::forward<Func&&>(func));
        } else {
            timers.insert({at, {std::forward<Func&&>(func)}});
        }
    }

    bool run_once() {
        auto e = WSAGetLastError();

        auto flag = poll(waitPeriod);
        if (SOCKET_ERROR == flag) {
            auto e = WSAGetLastError();

            return false;
        }
        auto numOfEvents = flag;
        size_t i = 0;
        for (auto& po : fdarray) {
            if (po.eventOccured()) {
                auto& socketmeta = *socketMetaMap[i];

                if (po.readEventOccured()) {
                    // read new data from network
                    // check that there are still listerns on the read, as they
                    // may have been responded to with the extra data buffer
                    if (socketmeta.readDataQueue.size() > 0) {
                        
                        socketmeta.readIn(po.fd);

                        // if it isn't empty then we must wait until we can
                        // read again
                        fdarray[i].listforRead(!socketmeta.reads.empty());
                    }
                }
                if (po.writeEventOccured()) {
                    // send as much as we can
                    while (socketmeta.writes.size() > 0) {
                        auto& data = socketmeta.writeDataQueue.front();
                        int ret = send(po.fd, data.data, data.size, 0);
                        // was not stopped because of not enough space
                        if (ret != WSAENOBUFS && ret != -1) {
                            auto addToBack =
                                socketmeta.writes.front()(data.size);
                            if (addToBack) {
                                socketmeta.writes.push_back(
                                    socketmeta.writes.front());
                                socketmeta.writeDataQueue.push_back(
                                    socketmeta.writeDataQueue.front());
                            }
                            socketmeta.writes.pop_front();
                            socketmeta.writeDataQueue.pop_front();
                        } else {
                            int e = WSAGetLastError();
                            if (e != WSAEWOULDBLOCK)
                                std::cout << "error code : " << e << std::endl;
                            break;
                        }
                    }
                    // if it isn't empty then we must wait until we can write
                    // again
                    fdarray[i].listforWrite(!socketmeta.writes.empty());
                }
            }
            po.revents = 0;
            i++;
        }

        auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
        while (!timers.empty() && timers.begin()->first <= now) {
            for (auto& timerFunc : timers.begin()->second) {
                timerFunc();
            }

            timers.erase(timers.begin());
        }

        return true;
    }

    void run() {
        running = true;
        while (running) {
            run_once();
        }
    }
};

/*
if (po.readEventOccured()) {
                    // read new data from network
                    // check that there are still listerns on the read, as they
                    // may have been responded to with the extra data buffer
                    if (socketmeta.readDataQueue.size() > 0) {
                        while (socketmeta.reads.size() > 0) {
                            auto& data = socketmeta.readDataQueue.front();

                            int amountRecieved = 0;

                            if(data.amountOfDataNeeded() > 0){
                                amountRecieved =
                                    recv(po.fd, data.data + data.offset,
                                        data.amountOfDataNeeded(), 0);

                                if (amountRecieved == -1) break;


                                data.offset += amountRecieved;
                            }

                            // data is full
                            if (data.amountOfDataNeeded() == 0) {
                                auto addToBack =
socketmeta.reads.front()(data.data, data.size); if (addToBack) {
                                    socketmeta.reads.push_back(
                                        socketmeta.reads.front());
                                    socketmeta.readDataQueue.push_back(
                                        socketmeta.readDataQueue.front());
                                    socketmeta.readDataQueue.back().offset = 0;
                                }
                                socketmeta.reads.pop_front();
                                socketmeta.readDataQueue.pop_front();
                            }
                        }
                        // if it isn't empty then we must wait until we can
                        // read again
                        fdarray[i].listforRead(!socketmeta.reads.empty());
                    }
                }
*/