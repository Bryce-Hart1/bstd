#include <shared_mutex>
#include <memory>


/**
 * @attention a threadsafe queue
 * Requires import of threadsafe vector
 */
namespace ts{

        template <typename T> class queue{
        friend class threadsafe::vec<T>;
        
        private:
            vec<T> _queue;
            std::atomic<std::size_t> frontPtr;

        void clear(){
            _queue.clear();
        }

        void clear(bool RESET_CAPACITY){
            _queue.clear(RESET_CAPACITY);
        }

        bool isEmpty(){
            return _queue.isEmpty();
        }

        T front(){
            return _queue.at(frontPtr);            
        }

        void pop(){
            frontPtr.fetch_add(1);
        }

        void push(T val){
            _queue.pushBack(val);
        }

        std::size_t size(){
            return _queue.size();
        }
    };

}