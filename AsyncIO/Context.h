
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
            if(amountRecieved != -1){

            }
            this->setg(this->buffer_, this->buffer_, this->buffer_ + amountRecieved);
            return std::char_traits<char>::to_int_type(*this->gptr());
        }else{
            throw std::exception("more data coming");
        }      
    }
};

struct Reader {
    std::function<void(char*, size_t)> consumer;
    size_t readInSoFar = 0;

    template <typename Func>
    Reader(Func&& consumer) : consumer(std::forward<Func>(consumer)) {}
    

    virtual int size() { return -1; };

    virtual int amountToReadFromBuf(char* buf, size_t buf_size) {
        size_t i = 0;
        while (i < buf_size) {
            auto jump = readChar(buf[i]);
            readInSoFar += jump;
            if (jump == 0) {
                break;
            }
            i += jump;
        }
        return min(i, buf_size);
    }

    virtual char* readIn(char* buf, size_t buf_size) {
        int amountToRead = amountToReadFromBuf(buf, buf_size);
        consumer(buf, amountToRead);
        return buf + amountToRead;
    }

    virtual int readChar(char buf) = 0;

    virtual bool finished() = 0;

    virtual void reset() {
        readInSoFar = 0;
    }
};

struct FixedSizeReader : public Reader {
    size_t fixed_size;

    template <typename Func>
    FixedSizeReader(size_t fixed_size, Func&& consumer)
        : fixed_size(fixed_size), Reader(std::forward<Func>(consumer)) {}

    virtual int size() { return fixed_size; };

    virtual int readChar(char c) { return fixed_size-readInSoFar; }

    virtual bool finished() { return fixed_size == readInSoFar; }
};

struct LengthValueReader : public Reader {
    size_t fixed_size;
    bool readingSize = true;

    template <typename Func>
    LengthValueReader(size_t fixed_size, Func&& consumer)
        : fixed_size(fixed_size), Reader(std::forward<Func>(consumer)) {}

    virtual int readChar(char c) { 
        if(readingSize){
            return sizeof(size_t);
        }else{
            return fixed_size-readInSoFar;
        }
    }

    virtual bool finished() { return !readingSize && fixed_size+sizeof(size_t) == readInSoFar; }
    
    virtual void reset(){
        readingSize = true;
    }
};

struct DelimeterSize : public Reader {
    char delim;
    bool fin = false;

    template <typename Func>
    DelimeterSize(char delim, Func&& func)
        : delim(delim), Reader(std::forward<Func>(consumer)) {}

    virtual int readChar(char c) { return c == delim ? 0 : 1; }

    virtual bool finished() { return fin; }
    virtual void reset(){
        fin = false;
    }
};

struct CharStarBuffer{
    char* data;
    CharStarBuffer(char* data):data(data){}

    void operator()(char* buf, size_t size){
        memcpy(data, buf, size);
    }
};



template <typename Func>
using FuncQueue = std::list<std::function<Func>>;

struct SocketMeta {
    FuncQueue<bool()> reads;
    SocketBuf buf;

    std::list<std::unique_ptr<Reader>> readDataQueue;
    char* extraData;
    size_t extraDataSize;
    char* extraData_start;
    char buf[100];
    size_t buf_start = 0;
    size_t bufSize = 0;

    void executeFront(){
        auto addToBack = reads.front()(); 
        if (addToBack) {
            reads.push_back(reads.front());
            readDataQueue.push_back(std::move(readDataQueue.front()));
            readDataQueue.back()->reset();
        }
        reads.pop_front();
        readDataQueue.pop_front();
    }

    void readFromBuf() {
        auto bufIt = buf + buf_start;
        auto bufItEnd = bufIt + bufSize;
        while (reads.size() > 0 && bufIt != bufItEnd) {
            auto& reader = *readDataQueue.front();
            
            bufIt = reader.readIn(bufIt, bufItEnd - bufIt);
            
            if (reader.finished()) {
                executeFront();   
            }
        }
        buf_start =  bufIt - buf;
    }

    void readIn(SOCKET fd) {
        //handle any 0 size data, mostly likely accept requests
        while(readDataQueue.size() > 0 && readDataQueue.front()->size() == 0){
            executeFront();
        }

        readFromBuf();

        // business as usual

        auto amountRecieved = 0;
        while (reads.size() > 0  && amountRecieved != -1) {
            amountRecieved = recv(fd, buf, 100, 0);
            buf_start = 0;
            if(amountRecieved != -1){
                bufSize = amountRecieved;
                readFromBuf();     
            }else{
                bufSize = 0;
            }           
        }
    }

    FuncQueue<bool(size_t)> writes;
    std::list<Data> writeDataQueue;

    ~SocketMeta() { free(extraData_start); }
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
        socketMetaMap.emplace_back(new SocketMeta());
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

    template <typename Func>
    void addReader(SOCKET socket, std::unique_ptr<Reader>&& reader, Func&& func) {
        auto it = socketToIndex.find(socket);
        if (it != socketToIndex.end()) {
            auto index = it->second;

            auto& socketmeta = *socketMetaMap[index];
            socketmeta.reads.emplace_back(std::forward<Func>(func));
            socketmeta.readDataQueue.emplace_back(std::move(reader));
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
                        auto socketStream = std::istream(&buf);

                        char b[11];
                        b[10] = 0;
                  
                        
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