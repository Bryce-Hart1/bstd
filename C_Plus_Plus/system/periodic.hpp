#include <condition_variable>
#include <mutex>
#include <functional>

namespace bstd{

namespace syst{

class PeriodicTask {
public:
    PeriodicTask(std::chrono::milliseconds interval, std::function<void()> work) : _interval(interval), _work(std::move(work))
    {
        _thread = std::thread([this]{
             run();
            });
    }

    ~PeriodicTask() {
        {
            std::lock_guard lock(_mtx);
            _stop = true;
        }
        _cv.notify_one();
        if (_thread.joinable()){ 
            _thread.join();
        }
    }

private:
    void run() {
        std::unique_lock lock(_mtx);
        while (!_stop) {
            _work();
            // wait_for returns early if notified (e.g. on stop), otherwise after `interval_`
            _cv.wait_for(lock, _interval, [this]{ return _stop.load(); });
        }
    }

    std::chrono::milliseconds _interval;
    std::function<void()> _work;
    std::thread _thread;
    std::condition_variable _cv;
    std::mutex _mtx;
    std::atomic<bool> _stop{false};
};


}
}