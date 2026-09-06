#include <functional>
#include <memory>
#include <optional>
#include <queue>
#include <vector>

namespace bstd{
namespace ds{

template<typename Type, typename Compare = std::less<Type>>
class avlTree{





    public:

    bool insert(const Type& value){

    }

    bool erase(const Type& value){

    }
    
    std::vector<Type> listAllDFS(){

    }

    std::vector<Type> listAllBFS(){
        std::queue<std::unique_ptr<node>> listOfNext;
        
    }

    bool contains(const Type& value) const{

    }

    std::size_t size() const noexcept{
        return _count;
    }
    
    void operator+=(const Type& value){
        insert(value); 
    }

    bool operator=(const avlTree tree){

    }



    private:
    struct node{
        Type data;
        node* parent = nullptr;
        std::size_t height = 1;
        std::unique_ptr<node> left = nullptr;
        std::unique_ptr<node> right = nullptr;

        explicit node(const Type& value) : data(value){};

    };

    std::size_t _count = 0;
    std::unique_ptr<node> _root;
    Compare _compare{};
    
    private:
        
    class const_iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = Type;
        using difference_type  = std::ptrdiff_t;
        using pointer = const Type*;
        using reference = const Type&;

        reference operator*() const {
            return current_->data;
        }
        pointer operator->() const {
            return &current_->data;
        }

        const_iterator& operator++(){
            
        }
        const_iterator operator++(int){

        }

        bool operator==(const const_iterator&) const = default;

    private:
        friend class avlTree;
        explicit const_iterator(node* n) : current_(n) {}
        node* current_;
    };



};












}}