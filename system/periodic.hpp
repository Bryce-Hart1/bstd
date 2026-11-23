#pragma once
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <functional>
#include <thread>

namespace bstd{

namespace syst{

/**
* Runs `work` on its own thread once every `interval`, starting immediately.
* An exception thrown by `work` is swallowed, it would otherwise terminate the program.
* @attention not copyable or movable, the thread captures `this`.
*/
class PeriodicTask {
public:
    PeriodicTask(std::chrono::milliseconds interval, std::function<void()> work) : _interval(interval), _work(std::move(work))
    {
        _thread = std::thread([this]{
             run();
            });
    }

    PeriodicTask(const PeriodicTask&) = delete;
    PeriodicTask& operator=(const PeriodicTask&) = delete;
    PeriodicTask(PeriodicTask&&) = delete;
    PeriodicTask& operator=(PeriodicTask&&) = delete;

    ~PeriodicTask() {
        stop();
    }

    /**
    * Stops the task and waits for a running tick to finish. Safe to call more than once.
    * If called from inside `work` it only requests the stop, the destructor does the join.
    */
    void stop() {
        {
            std::lock_guard lock(_mtx);
            _stop = true;
        }
        _cv.notify_all();
        // _thread is only ever looked at with _joinMtx held: join() writes to it, so a second stop() checking
        // joinable() without the lock would race with a first one that is still joining.
        // The worker can't take that lock (it would wait on whoever is joining it), so it is told apart by the id it stored.
        if (_workerId.load() == std::this_thread::get_id()){
            return;
        }
        std::lock_guard joinLock(_joinMtx); //two threads calling stop() must not both join
        if (_thread.joinable()){
            _thread.join();
        }
    }

    /* Changes the time between ticks. Takes effect right away, the current wait restarts with the new interval. */
    void setInterval(std::chrono::milliseconds interval) {
        {
            std::lock_guard lock(_mtx);
            _interval = interval;
            _changed = true;
        }
        _cv.notify_all();
    }

private:
    void run() {
        _workerId.store(std::this_thread::get_id()); //before `work` can run, so a stop() from inside it always sees it
        std::unique_lock lock(_mtx);
        while (!_stop) {
            lock.unlock(); //so stop() and setInterval() never wait on a running tick
            try{
                _work();
            }catch(...){
            }
            lock.lock();
            // wait_for returns early on stop or setInterval(), otherwise after `_interval`
            bool restart = true;
            while (restart && !_stop) {
                _changed = false;
                const bool woken = _cv.wait_for(lock, _interval, [this]{ return _stop.load() || _changed; });
                restart = woken && _changed; //new interval: wait again with it instead of ticking
            }
        }
    }

    std::chrono::milliseconds _interval;
    std::function<void()> _work;
    std::thread _thread;
    std::condition_variable _cv;
    std::mutex _mtx;
    std::mutex _joinMtx;
    std::atomic<bool> _stop{false};
    std::atomic<std::thread::id> _workerId{}; //set by the worker itself, default (no thread) until then
    bool _changed = false; //guarded by _mtx
};


}
}
