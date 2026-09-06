#include "tsVector.hpp"

namespace bstd{
namespace ds{

template <typename T>
using vec = threadsafe::vec<T>;

namespace ts{

    template <typename T> class stack{
        friend class vec<T>;
        private:
        vec<T> _stack;

        stack(){

        }
        ~stack(){

        }


        T front(){
            return _stack.at(_stack.endItr());
        }

        public:
        void pop(){
            _stack.remove(_stack.endItr);
        }

        void push(T value){

        }

        std::size_t size(){
            return _stack.size();
        }


        


    };











}
}

}