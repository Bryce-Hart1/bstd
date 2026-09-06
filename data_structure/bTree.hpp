#ifndef BSTD_DS_BTREE_HPP
#define BSTD_DS_BTREE_HPP

#include <cstddef>
#include <utility>

namespace bstd {
namespace ds {

/**
 * @author Bryce Hart Refined @date Sept 5th
 * A classic B-tree.
 *
 * @tparam T     key type, needs a default constructor and operator<
 * @tparam Order maximum number of children a node may keep (>= 3)
 *
 * A settled node holds at most Order-1 keys and Order children. During an
 * insert a node is allowed to hold one extra key; the parent notices the
 * overflow and splits the node before returning, so the extra key never
 * survives past the end of Insert.
 */
template <class T, std::size_t Order>
class BTree {
    static_assert(Order >= 3, "a B-tree needs an order of at least 3");

private:
    using usize = std::size_t;

    //nested node type: it reuses T and Order from the tree instead of
    //re-declaring them, which is what made the original shadow its own
    //template parameters
    struct Node {
        usize number_of_keys;        //keys currently stored, 0 .. Order
        T     keys[Order];           //one spare slot for the transient overflow
        Node* children[Order + 1];   //children[i] holds keys smaller than keys[i]

        //builds an empty leaf
        Node() : number_of_keys(0), keys(), children() {}

        //nodes own their subtrees, so copying one would double free it
        Node(const Node&)            = delete;
        Node& operator=(const Node&) = delete;

        //a node is a leaf while it has no first child
        bool isLeaf() const { return children[0] == nullptr; }

        //true once the node holds the one key too many that forces a split
        bool isOverfull() const { return number_of_keys == Order; }

        /**
         * Inserts a key into the subtree rooted at this node.
         *
         * @param  value key to insert
         * @return true when this node overflowed and its parent must split it
         */
        bool Insert(const T& value) {
            if (isLeaf()) {
                //slide the bigger keys one slot right and drop value in the gap
                usize i = number_of_keys;
                while (i > 0 && value < keys[i - 1]) {
                    keys[i] = std::move(keys[i - 1]);
                    --i;
                }
                keys[i] = value;
                ++number_of_keys;
            } else {
                //find the child that should own the key and recurse into it
                usize i = 0;
                while (i < number_of_keys && keys[i] < value) ++i;

                if (children[i]->Insert(value)) {
                    //the child overflowed: split it and pull its median up here
                    T mid;
                    Node* right = children[i]->split(&mid);

                    for (usize j = number_of_keys; j > i; --j)
                        keys[j] = std::move(keys[j - 1]);
                    for (usize k = number_of_keys + 1; k > i + 1; --k)
                        children[k] = children[k - 1];

                    keys[i]         = std::move(mid);
                    children[i + 1] = right;   //the new half sits right of the median
                    ++number_of_keys;
                }
            }
            return isOverfull();
        }

        /**
         * Splits this node in two around its median key.
         * The node keeps the left half; the right half becomes a new node.
         *
         * @param  med out parameter, receives the median key for the parent
         * @return the newly allocated right sibling, owned by the caller
         */
        Node* split(T* med) {
            const usize n   = number_of_keys;
            const usize mid = n / 2;

            *med = std::move(keys[mid]);      //this key moves up to the parent
            Node* right = new Node();

            //hand every key above the median, with its left child, to the new node
            for (usize i = mid + 1; i < n; ++i) {
                right->keys[right->number_of_keys]     = std::move(keys[i]);
                right->children[right->number_of_keys] = children[i];
                ++right->number_of_keys;
                children[i] = nullptr;        //ownership has moved, do not free twice
            }

            //the node's last child follows the last key that moved
            right->children[right->number_of_keys] = children[n];
            children[n] = nullptr;

            //keys 0 .. mid-1 and children 0 .. mid stay here
            number_of_keys = mid;
            return right;
        }

        //walks down the leftmost spine, every leaf sits at the same depth
        usize getHeight() const {
            usize height = 1;
            const Node* current = this;
            while (!current->isLeaf()) {
                current = current->children[0];
                ++height;
            }
            return height;
        }

        //frees the whole subtree, deleting a null child is a harmless no-op
        ~Node() {
            for (usize i = 0; i <= Order; ++i) delete children[i];
        }
    };

    Node* root;
    usize count;

public:
    BTree();
    BTree(const BTree&)            = delete;   //the tree owns raw nodes
    BTree& operator=(const BTree&) = delete;
    BTree(BTree&& other) noexcept;
    BTree& operator=(BTree&& other) noexcept;
    ~BTree();

    void  Insert(const T& value);
    bool  Contains(const T& value) const;
    usize size() const;
    usize getHeight() const;
    bool  empty() const;
};

//starts an empty tree
template <class T, std::size_t Order>
BTree<T, Order>::BTree() : root(nullptr), count(0) {}

//takes over another tree's nodes and leaves it empty
template <class T, std::size_t Order>
BTree<T, Order>::BTree(BTree&& other) noexcept
    : root(other.root), count(other.count) {
    other.root  = nullptr;
    other.count = 0;
}

/**
 * Move assignment.
 *
 * @param  other tree to take the nodes from, left empty afterwards
 * @return this tree
 */
template <class T, std::size_t Order>
BTree<T, Order>& BTree<T, Order>::operator=(BTree&& other) noexcept {
    if (this != &other) {
        delete root;                //drop what we already hold
        root  = other.root;
        count = other.count;
        other.root  = nullptr;
        other.count = 0;
    }
    return *this;
}

/**
 * Inserts a key into the tree, duplicates are allowed.
 *
 * @param value key to insert
 */
template <class T, std::size_t Order>
void BTree<T, Order>::Insert(const T& value) {
    if (root == nullptr) {
        root = new Node();
        root->keys[0]        = value;
        root->number_of_keys = 1;
    } else if (root->Insert(value)) {
        //the root overflowed, so the tree grows one level upwards
        T mid;
        Node* right   = root->split(&mid);
        Node* newRoot = new Node();

        newRoot->keys[0]        = std::move(mid);
        newRoot->number_of_keys = 1;
        newRoot->children[0]    = root;
        newRoot->children[1]    = right;
        root = newRoot;
    }
    ++count;
}

/**
 * Looks a key up.
 *
 * @param  value key to search for
 * @return true when the key is in the tree
 */
template <class T, std::size_t Order>
bool BTree<T, Order>::Contains(const T& value) const {
    const Node* current = root;
    while (current != nullptr) {
        usize i = 0;
        while (i < current->number_of_keys && current->keys[i] < value) ++i;
        if (i < current->number_of_keys && !(value < current->keys[i])) return true;
        current = current->children[i];   //null on a leaf, which ends the loop
    }
    return false;
}

//number of keys inserted so far
template <class T, std::size_t Order>
std::size_t BTree<T, Order>::size() const { return count; }

//number of levels, 0 for an empty tree
template <class T, std::size_t Order>
std::size_t BTree<T, Order>::getHeight() const {
    return root == nullptr ? 0 : root->getHeight();
}

//true while nothing has been inserted
template <class T, std::size_t Order>
bool BTree<T, Order>::empty() const { return count == 0; }

//deleting the root recursively frees every node below it
template <class T, std::size_t Order>
BTree<T, Order>::~BTree() { delete root; }

}  // namespace ds
}  // namespace bstd

#endif  // BSTD_DS_BTREE_HPP