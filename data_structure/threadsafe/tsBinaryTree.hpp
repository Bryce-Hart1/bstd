#include <iostream>
#include <vector>
#include <mutex>
#include <memory>
#include <shared_mutex>
#include <atomic>
#include <concepts>



namespace bstd{
namespace ds{
namespace ts{


using sizeT = std::size_t;



/**
 * Bryce Hart mutexlock that is used in helper functions throughout.
 * depends on std shared mutex.
 */
struct mutexLock{
    private:
    std::shared_mutex mtx;

    public:
    void unlock(){
        this->mtx.unlock();
    }


    void lock(){
        this->mtx.lock();
    }

};

/**
 * Bryce Hart
 * spinlock. Smaller but burns cpu
 * 
 */
struct spinLock{
    private:
    std::atomic_flag atomic_flag = ATOMIC_FLAG_INIT;

    public:
    void lock(){
        while(atomic_flag.test_and_set(std::memory_order_acquire)){/*spin infinitely*/}
    }

    void unlock(){
        atomic_flag.clear(std::memory_order_release);
    }


};

    template <typename numType>


    class binTree{
        struct node{
            std::mutex _nodeLock;
            numType _value;
            std::unique_ptr<numType> left = nullptr;
            std::unique_ptr<numType> right = nullptr;
            sizeT _count = 0;
            node(numType value){
                this->_count = 0;
                this->_value = value;
            }
        };
        private:
            node root;
            sizeT size;


        bool findHelper(node* ptr, numType target){
            if(ptr == nullptr){
                return false;
            }
            if(ptr->_value){
                return true;
            }
            if(ptr->left != nullptr){
                findHelper(ptr->_left, target);
            }
            if(ptr->_right != nullptr){
                findHelper(ptr->_right, target);
            }

        }

        bool hasChildren(node* ptr){
            if(ptr->_left != nullptr || ptr->_right != nullptr){
                return true;
            }
            return false;
        }

        node* returnHelper(node* ptr, numType target){
            if(ptr == nullptr){
                return nullptr;
            }
            if(ptr->_value){
                return ptr;
            }
            if(ptr->_left != nullptr){
                returnHelper(ptr->_left, target);
            }else{
                returnHelper(ptr->_right, target);
            }
            return nullptr;
        }
        void traverse_with_vector(std::vector<numType>& vec, node* current){
            if(current == nullptr){
                return;
            }
            traverse_with_vector(vec, current->left);
            vec.push_back(current->value);
            traverse_with_vector(vec, current->right);
        }

        public:

            template <typename Type>
            requires std::integral<Type> 
            binTree(Type rootValue){
                std::unique_lock<std::mutex> lock(root.nodeLock);
                root.value = rootValue;
                root.count++;
            }
            ~binTree(){
                delete[] this->root;
            }

            template <typename Type>
            requires std::integral<Type>
            void add(Type value){
                auto current = this->root;
                root++;
                while(current.left != nullptr && current.right != nullptr){
                    break; //temp
                }
            }

            //center the tree based off of the median element
            void center(){
                try{
                    if(this->isEmpty()){
                        std::runtime_error("Binary Tree is empty");
                    }
                std::vector<numType> list = traverse_with_vector(list, this->root);
                delete[] root;
                }catch(const std::exception &e){
                    std::cerr << e.what() << '\n';
                }
                if(this->size == 1){
                    return;
                }

                const sizeT middleIndex = (this->size / 2);
                node newRoot{};

            }

            bool find(numType value){
                if(findHelper(this->root, value)){
                    return true;
                }
                return false;
            }


            bool isEmpty(){
                if(this->size == 0){
                    return true;
                }
                return false;
            }

            std::vector<numType> orderedList(){
                std::vector<numType> vec;
                traverse_with_vector(vec, root);
            }

            void remove(numType remove){
                node* current = returnHelper(this->root, remove);
                if(current != nullptr){
                    if(!hasChildren(current)){ //first case is a leaf
                        delete current;
                    }
                    if(current == this->root){ //is the root
                        //add this
                    }

                }
            }



            

            
    };
}

}
}
