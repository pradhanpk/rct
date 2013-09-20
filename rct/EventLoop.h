#ifndef EVENTLOOP_H
#define EVENTLOOP_H

#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <tuple>
#include <queue>
#include <set>
#include <list>
#include <unordered_set>

#include <event2/event.h>

#include "Apply.h"
#include "rct-config.h"
#if defined(HAVE_EPOLL)
#  include <sys/epoll.h>
#elif defined(HAVE_KQUEUE)
#  include <sys/types.h>
#  include <sys/event.h>
#  include <sys/time.h>
#endif

class EventLoop;

// TODO: (All?) Needed for Header file ?
typedef std::function<void(int, int)> EventCallback;
typedef std::function<void(int)> TimerCallback;
//typedef std::function<void(void)> TimerCallback;

class Event
{
    friend class EventLoop;

public:
    virtual ~Event() {}
    virtual void exec() = 0;
};

template<typename Object, typename... Args>
class SignalEvent : public Event
{
public:
    enum MoveType { Move };

    SignalEvent(Object& o, Args&&... a)
	: obj(o), args(a...)
    {
    }
    SignalEvent(Object&& o, Args&&... a)
	: obj(o), args(a...)
    {
    }
    SignalEvent(Object& o, MoveType, Args&&... a)
	: obj(o), args(std::move(a...))
    {
    }
    SignalEvent(Object&& o, MoveType, Args&&... a)
	: obj(o), args(std::move(a...))
    {
    }

    void exec() { applyMove(obj, args); }

private:
    Object obj;
    std::tuple<Args...> args;
};

template<typename T>
class DeleteLaterEvent : public Event
{
public:
    DeleteLaterEvent(T* d)
	: del(d)
    {
    }

    void exec() { delete del; }

private:
    T* del;
};

extern "C" void postCallback(evutil_socket_t fd,
			     short w,
			     void *data);

class EventLoop : public std::enable_shared_from_this<EventLoop>
{
    struct EventCallbackData;    

public:
    typedef std::shared_ptr<EventLoop> SharedPtr;
    typedef std::weak_ptr<EventLoop> WeakPtr;

    friend void postCallback(evutil_socket_t fd,
			     short w,
			     void *data);
    
    EventLoop();
    ~EventLoop();

    enum Flag {
        None = 0x0,
        MainEventLoop = 0x1,
        EnableSigIntHandler = 0x2
    };
    enum PostType {
        Move = 1,
        Async
    };

    void init(unsigned flags = None);

    unsigned flags() const { return flgs; }

    template<typename T>
    static void deleteLater(T* del)
    {
        if (EventLoop::SharedPtr loop = eventLoop()) {
            loop->post(new DeleteLaterEvent<T>(del));
        } else {
            error("No event loop!");
        }
    }
    
    template<typename Object, typename... Args>
    void post(Object& object, Args&&... args)
    {
        post(new SignalEvent<Object, Args...>(object, 
					      std::forward<Args>(args)...));
    }
    
    template<typename Object, typename... Args>
    void postMove(Object& object, Args&&... args)
    {
        post( new SignalEvent<Object, Args...>(object, 
					       SignalEvent<Object, Args...>
					       ::Move, 
					       std::forward<Args>(args)...));
    }
    
    template<typename Object, typename... Args>
    void callLater(Object&& object, Args&&... args)
    {
        post(new SignalEvent<Object, Args...>(std::forward<Object>(object), 
					      std::forward<Args>(args)...));
    }

    template<typename Object, typename... Args>
    void callLaterMove(Object&& object, Args&&... args)
    {
        post(new SignalEvent<Object, Args...>(std::forward<Object>(object),
					      SignalEvent<Object, Args...>
					      ::Move,
					      std::forward<Args>(args)...));
    }
    
    void post(Event* event);

    enum Mode {
        SocketRead = 0x1,
        SocketWrite = 0x2,
        SocketOneShot = 0x4,
        SocketError = 0x8
    };
    
    void registerSocket(int fd, int mode, std::function<void(int, int)>&& func);
    void updateSocket(int fd, int mode);

    void unregisterSocket(int fd);
    unsigned int processSocket(int fd, int timeout = -1);

    // See Timer.h for the flags
    int registerTimer(TimerCallback&& func, int timeout, 
		      int flags = 0);
    void unregisterTimer(int id);

    enum { Success = 0x100, GeneralError = 0x200, Timeout = 0x400 };
    unsigned int exec(int timeout = -1);
    void quit(int code = 0);

    // TODO: re-establish the original const function??
    bool isRunning();

    static EventLoop::SharedPtr mainEventLoop()
    { std::lock_guard<std::mutex> locker(mainMutex); return mainLoop.lock(); }
    
    static EventLoop::SharedPtr eventLoop();

    static bool isMainThread() 
    {
	return ( EventLoop::mainEventLoop() 
		 && ( std::this_thread::get_id() 
		      == EventLoop::mainEventLoop()->threadId ) ); 
    }

private:
    void cleanup();

    static void eventDispatch(evutil_socket_t fd, short what, void *arg);

    void dispatch(EventCallbackData *cbdata, evutil_socket_t fd, short what);
    
    static void error(const char* err);

    // TODO: revisit feasibility
    int fdToId(int fd)
    {return -fd;}
    
    int generateId() 
    {return nextId++;}
    
private:
    static std::mutex mainMutex;
    mutable std::mutex mutex;
    std::thread::id threadId;

    event_base *eventBase;

    event *sigEvent;
    
    //std::queue<Event*> events;

    typedef std::map<int, std::unique_ptr<EventCallbackData> > EventCallbackMap;

    EventCallbackMap eventCbMap;

    uint32_t nextId;

    static EventLoop::WeakPtr mainLoop;

    unsigned flgs;
private:
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;
};

#endif
