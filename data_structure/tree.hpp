#pragma once
#include <algorithm>
#include <cstddef>
#include <functional>
#include <iterator>
#include <memory>
#include <queue>
#include <type_traits>
#include <utility>
#include <vector>

namespace bstd{
namespace ds{

    /**
    * A self balancing binary search tree (AVL) that holds unique values, like `std::set`.
    * Two values are the same when neither is `Compare`-less than the other, so `insert` of a
    * value that is already in the tree does nothing.
    * `insert`, `erase` and `contains` are O(log n).
    * @attention Not threadsafe, see `bstd::ds::ts` for the threadsafe containers.
    * @details every node owns its children and also points back at its parent. That is what lets the
    * iterator walk the tree in order without a stack.
    * @version Works in: >= Cpp2020
    */
template<typename Type, typename Compare = std::less<Type>>
class avlTree{

    private:
    struct node{
        Type data;
        node* parent = nullptr;
        std::size_t height = 1;
        std::unique_ptr<node> left = nullptr;
        std::unique_ptr<node> right = nullptr;

        explicit node(const Type& value) : data(value){}

    };

    public:

    /**
    * Forward iterator over the values in order (smallest to largest by `Compare`).
    * Values are read only, changing one in place could break the ordering.
    * `erase` only invalidates iterators to the value it removed.
    */
    class const_iterator {
    public:
        using iterator_category = std::forward_iterator_tag;
        using value_type = Type;
        using difference_type  = std::ptrdiff_t;
        using pointer = const Type*;
        using reference = const Type&;

        const_iterator() = default;

        reference operator*() const {
            return current_->data;
        }
        pointer operator->() const {
            return &current_->data;
        }

        const_iterator& operator++(){
            if(current_->right){
                current_ = current_->right.get();
                while(current_->left){
                    current_ = current_->left.get();
                }
            }else{
                // nothing bigger below, so the next value is the closest ancestor we are in the left side of
                const node* child = current_;
                current_ = current_->parent;
                while(current_ && current_->right.get() == child){
                    child = current_;
                    current_ = current_->parent;
                }
            }
            return *this;
        }
        const_iterator operator++(int){
            const_iterator before = *this;
            ++(*this);
            return before;
        }

        bool operator==(const const_iterator&) const = default;

    private:
        friend class avlTree;
        explicit const_iterator(const node* n) : current_(n) {}
        const node* current_ = nullptr;
    };

    using iterator = const_iterator;
    using value_type = Type;
    using size_type = std::size_t;

    avlTree() = default;

    avlTree(const avlTree& other) : _count(other._count), _root(copyNode(other._root.get(), nullptr)), _compare(other._compare) {}

    avlTree(avlTree&& other) noexcept(std::is_nothrow_move_constructible_v<Compare>)
        : _count(other._count), _root(std::move(other._root)), _compare(std::move(other._compare)) {
        other._count = 0;
    }

    avlTree& operator=(const avlTree& other){
        if(this != &other){
            avlTree copy(other);
            swap(copy);
        }
        return *this;
    }

    avlTree& operator=(avlTree&& other) noexcept(std::is_nothrow_move_assignable_v<Compare>){
        if(this != &other){
            _compare = std::move(other._compare);
            _root = std::move(other._root);
            _count = other._count;
            other._count = 0;
        }
        return *this;
    }

    void swap(avlTree& other) noexcept(std::is_nothrow_swappable_v<Compare>){
        using std::swap;
        swap(_count, other._count);
        swap(_root, other._root);
        swap(_compare, other._compare);
    }

    /**
    * Adds `value` to the tree.
    * @return `true` if it was added, `false` if the tree already had it.
    */
    bool insert(const Type& value){
        node* parent = nullptr;
        std::unique_ptr<node>* slot = &_root;
        while(*slot){
            parent = slot->get();
            if(_compare(value, parent->data)){
                slot = &parent->left;
            }else if(_compare(parent->data, value)){
                slot = &parent->right;
            }else{
                return false;
            }
        }
        *slot = std::make_unique<node>(value);
        (*slot)->parent = parent;
        ++_count;
        rebalanceUp(parent);
        return true;
    }

    /**
    * Removes `value` from the tree.
    * @return `true` if it was removed, `false` if the tree did not have it.
    */
    bool erase(const Type& value){
        node* target = find(value);
        if(!target){
            return false;
        }

        // owner of the target, so it is either _root or the parent's left / right
        std::unique_ptr<node>& targetSlot = slotOf(target);
        node* rebalanceFrom = nullptr;

        if(!target->left || !target->right){
            // at most one child, which just takes the target's place
            std::unique_ptr<node> child = target->left ? std::move(target->left) : std::move(target->right);
            rebalanceFrom = target->parent;
            if(child){
                child->parent = target->parent;
            }
            targetSlot = std::move(child);
        }else{
            // two children, so the next value up (smallest on the right side) takes the target's place.
            // The node itself is moved instead of its data so iterators to other values stay valid.
            node* next = target->right.get();
            while(next->left){
                next = next->left.get();
            }
            std::unique_ptr<node>& nextSlot = slotOf(next);
            std::unique_ptr<node> nextOwned = std::move(nextSlot);
            std::unique_ptr<node> nextRight = std::move(nextOwned->right);

            rebalanceFrom = (nextOwned->parent == target) ? nextOwned.get() : nextOwned->parent;
            if(nextRight){
                nextRight->parent = nextOwned->parent;
            }
            nextSlot = std::move(nextRight);

            nextOwned->parent = target->parent;
            nextOwned->left = std::move(target->left);
            nextOwned->right = std::move(target->right);
            if(nextOwned->left){
                nextOwned->left->parent = nextOwned.get();
            }
            if(nextOwned->right){
                nextOwned->right->parent = nextOwned.get();
            }
            nextOwned->height = target->height;
            targetSlot = std::move(nextOwned);
        }

        --_count;
        rebalanceUp(rebalanceFrom);
        return true;
    }

    /**
    * Depth first, in order.
    * @return every value from smallest to largest.
    */
    std::vector<Type> listAllDFS() const{
        std::vector<Type> values;
        values.reserve(_count);
        for(const Type& value : *this){
            values.push_back(value);
        }
        return values;
    }

    /**
    * Breadth first, one level at a time from the root down and left to right in each level.
    * @return every value in that order.
    */
    std::vector<Type> listAllBFS() const{
        std::vector<Type> values;
        values.reserve(_count);
        if(!_root){
            return values;
        }
        std::queue<const node*> listOfNext;
        listOfNext.push(_root.get());
        while(!listOfNext.empty()){
            const node* current = listOfNext.front();
            listOfNext.pop();
            values.push_back(current->data);
            if(current->left){
                listOfNext.push(current->left.get());
            }
            if(current->right){
                listOfNext.push(current->right.get());
            }
        }
        return values;
    }

    bool contains(const Type& value) const{
        return find(value) != nullptr;
    }

    std::size_t size() const noexcept{
        return _count;
    }

    bool empty() const noexcept{
        return _count == 0;
    }

    /**
    * Removes every value.
    */
    void clear() noexcept{
        _root.reset();
        _count = 0;
    }

    const_iterator begin() const{
        const node* first = _root.get();
        while(first && first->left){
            first = first->left.get();
        }
        return const_iterator(first);
    }
    const_iterator end() const{
        return const_iterator(nullptr);
    }
    const_iterator cbegin() const{
        return begin();
    }
    const_iterator cend() const{
        return end();
    }

    void operator+=(const Type& value){
        insert(value);
    }

    private:
    std::size_t _count = 0;
    std::unique_ptr<node> _root;
    Compare _compare{};

    private:

    static std::size_t heightOf(const node* n) noexcept{
        return n ? n->height : 0;
    }

    static void updateHeight(node* n) noexcept{
        n->height = 1 + std::max(heightOf(n->left.get()), heightOf(n->right.get()));
    }

    // positive when the left side is taller, negative when the right side is
    static std::ptrdiff_t balanceOf(const node* n) noexcept{
        return static_cast<std::ptrdiff_t>(heightOf(n->left.get())) - static_cast<std::ptrdiff_t>(heightOf(n->right.get()));
    }

    static std::unique_ptr<node> copyNode(const node* source, node* parent){
        if(!source){
            return nullptr;
        }
        std::unique_ptr<node> copy = std::make_unique<node>(source->data);
        copy->parent = parent;
        copy->height = source->height;
        copy->left = copyNode(source->left.get(), copy.get());
        copy->right = copyNode(source->right.get(), copy.get());
        return copy;
    }

    node* find(const Type& value) const{
        node* current = _root.get();
        while(current){
            if(_compare(value, current->data)){
                current = current->left.get();
            }else if(_compare(current->data, value)){
                current = current->right.get();
            }else{
                return current;
            }
        }
        return nullptr;
    }

    // the unique_ptr that owns `n`
    std::unique_ptr<node>& slotOf(node* n) noexcept{
        if(!n->parent){
            return _root;
        }
        return n->parent->left.get() == n ? n->parent->left : n->parent->right;
    }

    // `x`'s right child takes its place and `x` becomes that child's left. Returns the new top of the subtree.
    node* rotateLeft(node* x) noexcept{
        std::unique_ptr<node>& slot = slotOf(x);
        std::unique_ptr<node> top = std::move(slot);
        std::unique_ptr<node> pivot = std::move(top->right);
        node* parent = top->parent;

        top->right = std::move(pivot->left);
        if(top->right){
            top->right->parent = top.get();
        }
        pivot->parent = parent;
        top->parent = pivot.get();
        updateHeight(top.get());
        pivot->left = std::move(top);
        updateHeight(pivot.get());

        node* newTop = pivot.get();
        slot = std::move(pivot);
        return newTop;
    }

    // mirror of rotateLeft
    node* rotateRight(node* x) noexcept{
        std::unique_ptr<node>& slot = slotOf(x);
        std::unique_ptr<node> top = std::move(slot);
        std::unique_ptr<node> pivot = std::move(top->left);
        node* parent = top->parent;

        top->left = std::move(pivot->right);
        if(top->left){
            top->left->parent = top.get();
        }
        pivot->parent = parent;
        top->parent = pivot.get();
        updateHeight(top.get());
        pivot->right = std::move(top);
        updateHeight(pivot.get());

        node* newTop = pivot.get();
        slot = std::move(pivot);
        return newTop;
    }

    // fixes the height of `n` and rotates if its sides differ by more than one. Returns the new top of the subtree.
    node* rebalance(node* n) noexcept{
        updateHeight(n);
        const std::ptrdiff_t balance = balanceOf(n);
        if(balance > 1){
            if(balanceOf(n->left.get()) < 0){
                rotateLeft(n->left.get());
            }
            return rotateRight(n);
        }
        if(balance < -1){
            if(balanceOf(n->right.get()) > 0){
                rotateRight(n->right.get());
            }
            return rotateLeft(n);
        }
        return n;
    }

    // rebalances `n` and every ancestor of it
    void rebalanceUp(node* n) noexcept{
        while(n){
            n = rebalance(n)->parent;
        }
    }

};

}}
