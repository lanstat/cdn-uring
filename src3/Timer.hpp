#ifndef SRC_TIMER_HPP_
#define SRC_TIMER_HPP_

#include "EpollObject.hpp"

class Timer {
   public:
    Timer();
    ~Timer();
    bool Start(const unsigned int &msec);
    virtual void Exec() = 0;
    void ValidateTheTimer();

   private:
    unsigned int msec_;
    int fd_;
    void parseEvent(const epoll_event &event);
};

#endif  // EPOLL_TIMER_H
