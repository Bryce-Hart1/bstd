#pragma once

#include <algorithm>
#include <compare>
#include <cstddef>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <utility>
#include <vector>

namespace bstd{
namespace ds{
namespace ts{

    /**
    * @author Bryce Hart @date 9/16/26
    *
    * A threadsafe queue class.
    * Uses a `shared mutex` that is shared between the whole class for read and write access.
    * if you feel there is anything missing / should be added to this class, open an issue on
    * github.
    * @attention no move or copy constructors because `std::shared mutex`
    * is not movable or copyable.
    * @details uses `std::vector` under the hood as a circular buffer. `_front` is the index of the
    * oldest item and `_count` is how many items there are, so the back wraps around to
    * `(_front + _count - 1) % capacity`. When the buffer is full it is relocated into one
    * twice the size, with the front moved back to index `0`.
    * @version Works in: >= Cpp2020
    */
    template <typename T> class Queue{
        private:
        std::vector<std::optional<T>> _data; //empty slots are `std::nullopt` so `T` never needs a default constructor
        std::size_t _front = 0;
        std::size_t _count = 0;
        mutable std::shared_mutex _access;
        using unique_access = std::unique_lock<std::shared_mutex>;
        using shared_access = std::shared_lock<std::shared_mutex>;

        /**
        * @attention if adding private members, do NOT add locks. Public members are already locking.
        */

        /* @returns index in `_data` of the item `offset` spots behind the front. */
        std::size_t index(std::size_t offset) const{
            return (_front + offset) % _data.size();
        }

        /**
        * Moves every item into a new buffer with `space` slots, front first, so the front lands at `0`.
        * Assumes `space >= _count`.
        */
        void relocate(std::size_t space){
            std::vector<std::optional<T>> relocated(space);
            for(std::size_t i = 0; i < _count; i++){
                relocated[i] = std::move_if_noexcept(_data[index(i)]);
            }
            _data = std::move(relocated);
            _front = 0;
        }

        /* Makes sure there is room for `extra` more items, doubling the buffer if there is not. */
        void makeRoom(std::size_t extra){
            if(_count + extra > _data.size()){
                relocate(std::max(_count + extra, _data.empty() ? std::size_t(2) : _data.size() * 2));
            }
        }

        /**
        * Gets read access to this queue and `other` at the same time without deadlocking.
        * Only locks once if `other` is this queue, locking the same mutex twice is undefined.
        */
        std::pair<shared_access, shared_access> readBoth(const Queue& other) const{
            shared_access lock(_access, std::defer_lock);
            shared_access otherLock(other._access, std::defer_lock);
            if(this == &other){
                lock.lock();
            }else{
                std::lock(lock, otherLock);
            }
            return {std::move(lock), std::move(otherLock)};
        }


        public:

        /**
        * Default threadsafe Queue constructor.
        */
        Queue(){}

        /**
        * @param `reserve` amount of size for the queue to prematurely allocate.
         */
        explicit Queue(std::size_t reserve){_data.resize(reserve);}

        /**
        * @attention no move or copy constructors because `std::shared mutex`
        * is not move or copyable.
        */
        Queue(const Queue&) = delete;
        Queue& operator=(const Queue&) = delete;
        Queue(Queue&&) = delete;
        Queue& operator=(Queue&&) = delete;


        /* Clears all elements inside the queue. Keeps the capacity. */
        void clear(){
            unique_access lock(_access);
            for(std::size_t i = 0; i < _count; i++){
                _data[index(i)].reset();
            }
            _front = 0;
            _count = 0;
        }

        /* Resets capacity to `2` and deletes all elements in queue. */
        void resetCapacity(){
            unique_access lock(_access);
            _data = std::vector<std::optional<T>>(2);
            _front = 0;
            _count = 0;
        }

        /**
        * @returns item at the front of the queue (the oldest item), or `std::nullopt` if the queue is empty.
        * does not pop the front item, just peeks
        */
        std::optional<T> front() const{
            shared_access lock(_access);
            if(_count == 0){
                return std::nullopt;
            }
            return _data[_front];
        }

        /**
        * @returns item at the back of the queue (the newest item), or `std::nullopt` if the queue is empty.
        */
        std::optional<T> back() const{
            shared_access lock(_access);
            if(_count == 0){
                return std::nullopt;
            }
            return _data[index(_count - 1)];
        }


        /**
        * check if the queue is completely empty.
        * @returns `true` if the queue has no elements, `false` if it does.
         */
        bool isEmpty() const{
            shared_access lock(_access);
            return _count == 0;
        }

        /**
        * pops front of queue.
        * deletes front item in queue without returning item. Does nothing if the queue is empty.
        */
        void pop(){
            unique_access lock(_access);
            if(_count == 0){
                return;
            }
            _data[_front].reset();
            _front = index(1);
            _count--;
        }

        /**
        * push an element to the back of the queue
        * @param value data to be pushed to the back of the queue.
        */
        void push(T value){
            unique_access lock(_access);
            makeRoom(1);
            _data[index(_count)] = std::move(value);
            _count++;
        }

        /**
        * push a `std::vector` of items onto the queue.
        * front of vector is pushed first.
         */
        void push(std::vector<T> values){
            unique_access lock(_access);
            makeRoom(values.size());
            for(T& v : values){
                _data[index(_count)] = std::move(v); //lock already held, calling push(T) here would deadlock
                _count++;
            }
        }

        /**
        * @returns amount of items in the queue
        */
        std::size_t size() const{
            shared_access lock(_access);
            return _count;
        }

        /**
        * @returns amount of items the queue can hold before it has to relocate
        */
        std::size_t capacity() const{
            shared_access lock(_access);
            return _data.size();
        }

        /**
        * @returns the most slots the underlying vector could ever hold
        */
        std::size_t maxSize() const{
            shared_access lock(_access);
            return _data.max_size();
        }

        /* Reserves space inside the queue to prevent relocation */
        void reserve(std::size_t space){
            unique_access lock(_access);
            if(space > _data.size()){
                relocate(space);
            }
        }


        void operator+=(T value){
            push(std::move(value));//gets lock access inside
        }

        void operator+=(std::vector<T> values){
            push(std::move(values));//gets lock access inside
        }

        /* Compares front to back, the same way `std::vector` compares. */
        auto operator<=>(const Queue& other) const{
            using ordering = std::compare_three_way_result_t<T>;
            auto locks = readBoth(other);
            const std::size_t shared = std::min(_count, other._count);
            for(std::size_t i = 0; i < shared; i++){
                ordering cmp = *_data[index(i)] <=> *other._data[other.index(i)];
                if(cmp != 0){
                    return cmp;
                }
            }
            return ordering(_count <=> other._count);
        }

        bool operator==(const Queue& other) const{
            auto locks = readBoth(other);
            if(_count != other._count){
                return false;
            }
            for(std::size_t i = 0; i < _count; i++){
                if(!(*_data[index(i)] == *other._data[other.index(i)])){
                    return false;
                }
            }
            return true;
        }

        bool operator!=(const Queue& other) const{
            return !(*this == other);//gets lock access inside
        }

    };

}//ts
}//ds

}//bstd
