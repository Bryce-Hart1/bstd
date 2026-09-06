
#include "tsVector.hpp"
#include <string>
#include <functional>
#include <cstring>
#include <vector>
template <typename T>
using vec = threadsafe::vec<T>;
using size_t = std::size_t;
using strView = std::string_view;

/**
 * @attention this class wraps over threadsafe vector, making a threadsafe hashmap
 * uses std::hash to make a most types into a string hash, this removed the need for me to import 
 * any external user made hash function.
 * 
 * 
 * 
 */
namespace ds{
namespace ts{

    template <typename key, typename value>
    class hashmap{
        friend class vec<T>;
        friend class std::vector;
        private:
        struct _set{
            size_t _hashedKey;
            std::vector<value> _valueArr;
        };

        vec<_set> _map;
        size_t _bucketCount;
        size_t _keySize;
        size_t _valueSize;

        template <typename T>
        size_t hashBytes(const T& value){
            static_assert(std::is_trivially_copyable_v<T> "T type (key) must be a parsable type");
            strView bytes (reinterpret_cast<char*>(&value), sizeof(T));
            return std::hash<strView>{} value;
        }

        public:

        T at(key k){
            size_t hashedKey = hashBytes(k);
        }

        void clear(){
            _map.clear();
        }

        bool find(T key){
            size_t hashedKey= hashBytes(key);

        }

        bool isEmpty(){
            return _map.isEmpty();
        }

        void insert(key k, value v){
            size_t hashedKey = hashBytes(k);
        }


        size_t startInd(){

        }

        void swap(){

        }

    };



}
}