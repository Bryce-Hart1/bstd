#pragma once

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace bstd{
namespace ds{
namespace ts{

    /**
    * @author Bryce Hart @date 10/8/26
    *
    * A threadsafe hashmap class.
    * Uses a `shared mutex` that is shared between the whole class for read and write access.
    * if you feel there is anything missing / should be added to this class, open an issue on
    * github.
    * @attention no move or copy constructors because `std::shared mutex`
    * is not movable or copyable.
    * @details uses `std::unordered_map` under the hood, hashed with `std::hash<Key>` by default so
    * standard key types (numbers, `std::string`, pointers, ...) never need a user made hash function.
    * A custom `Hash` / `KeyEqual` can still be passed, the same as `std::unordered_map`.
    *
    * Values are always copied in and out. Nothing returns a reference or iterator into the map, because
    * it would outlive the lock and let another thread change or free it while in use. That is why there is
    * no `operator[]`, `begin()` or `end()`. Instead:
    * - at() reads a value
    * - update() changes a value in place (at() then insertOrAssign() is two locks, another thread can get between them)
    * - keys() / values() / snapshot() give copies to loop over
    * @version Works in: >= Cpp2017
    */
    template <typename Key, typename Value, typename Hash = std::hash<Key>, typename KeyEqual = std::equal_to<Key>>
    class Hashmap{
        private:
        using map = std::unordered_map<Key, Value, Hash, KeyEqual>;
        map _data;
        mutable std::shared_mutex _access;
        using unique_access = std::unique_lock<std::shared_mutex>;
        using shared_access = std::shared_lock<std::shared_mutex>;

        /**
        * @attention if adding private members, do NOT add locks. Public members are already locking.
        */

        /**
        * Gets read access to this hashmap and `other` at the same time without deadlocking.
        * Only locks once if `other` is this hashmap, locking the same mutex twice is undefined.
        */
        std::pair<shared_access, shared_access> readBoth(const Hashmap& other) const{
            shared_access lock(_access, std::defer_lock);
            shared_access otherLock(other._access, std::defer_lock);
            if(this == &other){
                lock.lock();
            }else{
                std::lock(lock, otherLock);
            }
            return {std::move(lock), std::move(otherLock)};
        }

        /* Same as readBoth(), but write access to both. */
        std::pair<unique_access, unique_access> writeBoth(Hashmap& other){
            unique_access lock(_access, std::defer_lock);
            unique_access otherLock(other._access, std::defer_lock);
            if(this == &other){
                lock.lock();
            }else{
                std::lock(lock, otherLock);
            }
            return {std::move(lock), std::move(otherLock)};
        }


        public:

        /**
        * Default threadsafe Hashmap constructor.
        */
        Hashmap(){}

        /**
        * @param `buckets` amount of buckets for the hashmap to prematurely allocate.
        */
        explicit Hashmap(std::size_t buckets) : _data(buckets){}

        /**
        * @param `items` starting `{key, value}` pairs. If a key repeats, the first one is kept.
        */
        Hashmap(std::initializer_list<std::pair<const Key, Value>> items) : _data(items){}

        /**
        * @attention no move or copy constructors because `std::shared mutex`
        * is not move or copyable.
        */
        Hashmap(const Hashmap&) = delete;
        Hashmap& operator=(const Hashmap&) = delete;
        Hashmap(Hashmap&&) = delete;
        Hashmap& operator=(Hashmap&&) = delete;


        /* Clears all elements inside the hashmap. Keeps the bucket count. */
        void clear(){
            unique_access lock(_access);
            _data.clear();
        }

        /**
        * @returns a copy of the value at `key`, or `std::nullopt` if `key` isn't in the hashmap.
        */
        std::optional<Value> at(const Key& key) const{
            shared_access lock(_access);
            auto found = _data.find(key);
            if(found == _data.end()){
                return std::nullopt;
            }
            return found->second;
        }

        /**
        * @returns `true` if `key` is in the hashmap, `false` if it isn't.
        */
        bool contains(const Key& key) const{
            shared_access lock(_access);
            return _data.find(key) != _data.end();
        }

        /**
        * @returns `1` if `key` is in the hashmap, `0` if it isn't. Keys are unique, so it is never more.
        */
        std::size_t count(const Key& key) const{
            shared_access lock(_access);
            return _data.count(key);
        }

        /**
        * Adds `key` with `value`. Does nothing if `key` is already in the hashmap (see insertOrAssign() to overwrite).
        * @returns `true` if it was added, `false` if `key` already existed.
        */
        bool insert(Key key, Value value){
            unique_access lock(_access);
            return _data.emplace(std::move(key), std::move(value)).second;
        }

        /**
        * Adds every `{key, value}` pair in `items`, front of vector first. Keys that already exist are skipped.
        * @returns how many pairs were added.
        */
        std::size_t insert(std::vector<std::pair<Key, Value>> items){
            unique_access lock(_access);
            std::size_t added = 0;
            for(std::pair<Key, Value>& item : items){
                //lock already held, calling insert(Key, Value) here would deadlock
                if(_data.emplace(std::move(item.first), std::move(item.second)).second){
                    added++;
                }
            }
            return added;
        }

        /**
        * Adds `key` with `value`, or overwrites the value if `key` already exists.
        * @returns `true` if it was added, `false` if an existing value was overwritten.
        */
        bool insertOrAssign(Key key, Value value){
            unique_access lock(_access);
            return _data.insert_or_assign(std::move(key), std::move(value)).second;
        }

        /**
        * Builds the value in place from `args` and adds it under `key`. If `key` already exists
        * nothing happens and the value is never built.
        * @returns `true` if it was added, `false` if `key` already existed.
        */
        template <typename... Args>
        bool tryEmplace(Key key, Args&&... args){
            unique_access lock(_access);
            return _data.try_emplace(std::move(key), std::forward<Args>(args)...).second;
        }

        /**
        * Runs `fn(value)` on the value at `key` while holding the lock, so the read and write happen as one step.
        * ex: `map.update("hits", [](int& v){ v++; });`
        * @param `fn` called with `Value&`. It must NOT call any function of this hashmap, the lock is
        * already held and would deadlock.
        * @returns `true` if `key` existed and `fn` ran, `false` if `key` isn't in the hashmap.
        */
        template <typename Fn>
        bool update(const Key& key, Fn fn){
            unique_access lock(_access);
            auto found = _data.find(key);
            if(found == _data.end()){
                return false;
            }
            fn(found->second);
            return true;
        }

        /**
        * Removes `key` and its value.
        * @returns `true` if it was removed, `false` if `key` isn't in the hashmap.
        */
        bool erase(const Key& key){
            unique_access lock(_access);
            return _data.erase(key) != 0;
        }

        /**
        * Removes every pair `pred` returns `true` for, the same as `std::erase_if`.
        * @param `pred` called with `const std::pair<const Key, Value>&`. It must NOT call any function of this
        * hashmap, the lock is already held and would deadlock.
        * @returns how many pairs were removed.
        */
        template <typename Pred>
        std::size_t eraseIf(Pred pred){
            unique_access lock(_access);
            std::size_t removed = 0;
            for(auto itr = _data.begin(); itr != _data.end();){
                if(pred(std::as_const(*itr))){
                    itr = _data.erase(itr);
                    removed++;
                }else{
                    ++itr;
                }
            }
            return removed;
        }

        /**
        * Removes `key` and hands back its value, in one step so no other thread can read or change it between.
        * @returns the removed value, or `std::nullopt` if `key` isn't in the hashmap.
        */
        std::optional<Value> extract(const Key& key){
            unique_access lock(_access);
            auto found = _data.find(key);
            if(found == _data.end()){
                return std::nullopt;
            }
            std::optional<Value> value(std::move(found->second));
            _data.erase(found);
            return value;
        }

        /**
        * Moves every pair from `other` whose key isn't in this hashmap yet. Pairs with a key this hashmap
        * already has stay in `other`, the same as `std::unordered_map::merge`. Does nothing if `other` is this hashmap.
        */
        void merge(Hashmap& other){
            if(this == &other){
                return;
            }
            auto locks = writeBoth(other);
            _data.merge(other._data);
        }

        /* Swaps every pair with `other`. Does nothing if `other` is this hashmap. */
        void swap(Hashmap& other){
            if(this == &other){
                return;
            }
            auto locks = writeBoth(other);
            _data.swap(other._data);
        }

        /**
        * @returns a copy of every key, in no particular order.
        */
        std::vector<Key> keys() const{
            shared_access lock(_access);
            std::vector<Key> r;
            r.reserve(_data.size());
            for(const auto& item : _data){
                r.push_back(item.first);
            }
            return r;
        }

        /**
        * @returns a copy of every value, in no particular order.
        */
        std::vector<Value> values() const{
            shared_access lock(_access);
            std::vector<Value> r;
            r.reserve(_data.size());
            for(const auto& item : _data){
                r.push_back(item.second);
            }
            return r;
        }

        /**
        * @returns a copy of the whole hashmap as a `std::unordered_map`, safe to loop over or change
        * without affecting (or locking) this one.
        */
        map snapshot() const{
            shared_access lock(_access);
            return _data;
        }

        /**
        * check if the hashmap is completely empty.
        * @returns `true` if the hashmap has no elements, `false` if it does.
        */
        bool isEmpty() const{
            shared_access lock(_access);
            return _data.empty();
        }

        /**
        * @returns amount of pairs in the hashmap
        */
        std::size_t size() const{
            shared_access lock(_access);
            return _data.size();
        }

        /**
        * @returns the most pairs the underlying map could ever hold
        */
        std::size_t maxSize() const{
            shared_access lock(_access);
            return _data.max_size();
        }

        /**
        * @returns amount of buckets the pairs are spread across
        */
        std::size_t bucketCount() const{
            shared_access lock(_access);
            return _data.bucket_count();
        }

        /**
        * @returns average pairs per bucket (`size() / bucketCount()`)
        */
        float loadFactor() const{
            shared_access lock(_access);
            return _data.load_factor();
        }

        /**
        * @returns the load factor that triggers a rehash once passed (`1.0` by default)
        */
        float maxLoadFactor() const{
            shared_access lock(_access);
            return _data.max_load_factor();
        }

        /* Sets the load factor that triggers a rehash once passed. Lower uses more memory for faster lookups. */
        void maxLoadFactor(float factor){
            unique_access lock(_access);
            _data.max_load_factor(factor);
        }

        /* Rebuilds the hashmap with at least `buckets` buckets (more if needed to stay under maxLoadFactor()). */
        void rehash(std::size_t buckets){
            unique_access lock(_access);
            _data.rehash(buckets);
        }

        /* Reserves room for `count` pairs to prevent rehashing */
        void reserve(std::size_t count){
            unique_access lock(_access);
            _data.reserve(count);
        }


        void operator+=(std::pair<Key, Value> item){
            insert(std::move(item.first), std::move(item.second));//gets lock access inside
        }

        void operator+=(std::vector<std::pair<Key, Value>> items){
            insert(std::move(items));//gets lock access inside
        }

        /* Equal if both hold the same keys with equal values, in any order. */
        bool operator==(const Hashmap& other) const{
            auto locks = readBoth(other);
            return _data == other._data;
        }

        bool operator!=(const Hashmap& other) const{
            return !(*this == other);//gets lock access inside
        }

    };

}//ts
}//ds
}//bstd
