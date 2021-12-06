#include "Context.h"

struct Timer{
    Context& context;
    std::function<void()> func;
    long long at;

    template<typename Func>
    Timer(Context& context, long long at, Func&& func): context(context), func(func), at(at) {}

    void start(){
        context.addTimer(at, func);
    }
};






struct Interval{
    Context& context;
    std::function<void()> func;
    size_t interval;

    template<typename Func>
    Interval(Context& context, size_t interval, Func&& func):
     func(func), interval(interval), context(context) {}

    void start(){
        context.addTimerIn(interval, [=]() { 
            this->func();
            this->start();
        });
    }
};