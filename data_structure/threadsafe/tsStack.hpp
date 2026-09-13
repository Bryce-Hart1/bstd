#include <shared_mutex>
#include <vector>

namespace bstd{
namespace ds{
namespace ts{

    /**
    * @author Bryce Hart @date 9/12/26
    *
    * A threadsafe stack class.
    * Uses a `shared mutex` that is shared between the whole class for read and write access.
    * if you feel there is anything missing / should be added to this class, open an issue on 
    * github.
    * @attention no move or copy constructors because `std::shared mutex`
    * is not movable or copyable.    
    * @details uses `std::vector` under the hood so most functions are built around trying 
    * to make it as close to it as possible as far as capacities.
    * @version Works in: >= Cpp2020
    */
    template <typename T> class Stack{
        private:
        std::vector<T> _data;
        mutable std::shared_mutex _access;
        using unique_access = std::unique_lock<std::shared_mutex>;
        using shared_access = std::shared_lock<std::shared_mutex>;


        public:

        /**
        * Default threadsafe Stack constructor.
        */
        Stack(){}

        /**
        * @param `reserve` amount of size for the stack to prematurely allocate.
         */
        explicit Stack(std::size_t reserve){_data.reserve(reserve);}

        /**
        * @attention no move or copy constructors because `std::shared mutex`
        * is not move or copyable.
        */
        Stack(const Stack&) = delete;
        Stack& operator=(const Stack&) = delete;
        Stack(Stack&&) = delete;
        Stack& operator=(Stack&&) = delete;


        /* Clears all elements inside the vector. */
        void clear(){
            _data.clear();
        }
        /**
        * @returns item at the top of the stack
        * does not pop the top item, just peeks
        */
        T front() const{
            shared_access lock(_access);
            return _data.back();
        }


        /**
        * check if the stack is completely empty.
        * @returns `true` if the stack has any elements, `false` if not.
         */
        const bool isEmpty(){
            shared_access lock(_access);
            return _data.empty();
        }

        /**
        * pops top of stack.
        * deletes top item in stack without returning item.
        */
        void pop(){
            unique_access lock(_access);
            _data.pop_back();
        }

        /**
        * push an element to the top of the stack
        * @param value data to be pushed to the top of the stack.
        */
        void push(T value){
            unique_access lock(_access);
            _data.push_back(std::move(value));
        }

        /**
        * push a `std::vector` of items onto the stack.
        * front of vector is pushed first.
         */
        void push(std::vector<T> values){
            unique_access lock(_access);
            for(T v : values){
                push(v);
            }
        }

        /**
        * @returns amount of items in the stack
        */
        std::size_t size() const{
            shared_access lock(_access);
            return _data.size();
        }

        /* Reserves space inside the stack to prevent relocation */
        void reserve(std::size_t& space){
            _data.reserve(space);
        }


        void operator+=(T value){
            push(value);//gets lock access inside
        }

        void operator+=(std::vector<T> values){
            unique_access lock(_access);
            push(values);
        }

        auto operator<=>(const Stack& other){
            shared_access lock(_access);
            return _data <=> other._data;
        }

        bool operator==(const Stack& other){
            shared_access lock(_access);
            return other._data == _data;
        }

        bool operator!=(const Stack& other){
            shared_access lock(_access);
            return other._data != _data;
        }

    };

}
}

}