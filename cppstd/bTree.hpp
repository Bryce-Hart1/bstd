#include <cstddef>
#include <algorithm>


namespace bstd{
namespace ds{

template <class T,std::size_t Order>
class BTree{
    private:    
using usize = std::size_t;
using ll = long long int;

template<class T,usize Order>
struct Node{
    usize number_of_keys;
    int order;
    int position= -1;
    T* keys;
    Node** children;

    explicit Node (usize order);
    ll Insert (T value);
    Node* split (Node* node, T* med);
    usize getHeight () ;
    ~Node ();
};

template <class T,usize Order>
Node<T,Order>::Node (usize order){
    this->order = order;
    this->number_of_keys = 0;


    this->keys = new T[this->order];
    this->children = new Node*[this->order + 1];

    for (int i = 0; i <= this->order; ++i){
        this->children[i] = nullptr;
    }
}


template <class T,usize Order>
ll Node<T,Order>::Insert (T value)
{

    if (this->children[0] == nullptr){
        this->keys[++this->position] = value;
        ++this->number_of_keys;
        for(int i=this->position; i>0 ; i--)
            if (this->keys[i] < this->keys[i-1])
                std::swap(this->keys[i],this->keys[i-1]);

    }else{
        int i = 0;
        for(; i<this->number_of_keys && value > this->keys[i];){
            i++;
        }
        int check=this->children[i]->Insert(value);
        if(check){
            T mid;
            int TEMP = i;
            Node<T,Order> *newNode = split(this->children[i], &mid); //Split Node

            for(; i<this->number_of_keys && mid > this->keys[i];)
                i++;

            for (int j = this->number_of_keys; j > i ; j--)
                this->keys[j] = this->keys[j - 1];
            this->keys[i] = mid;
            ++this->number_of_keys;
            ++this->position;

            //allocate newNode Split in the correct place
            int k;
            for (k = this->number_of_keys; k > TEMP + 1; k--)
                this->children[k] = this->children[k - 1];
            this->children[k] = newNode;
        }

    }
    if(this->number_of_keys == this->order)
        return 1;
    else return 0;
}

template <class T,usize Order>
Node<T,Order>* Node<T,Order>::split (Node *node, T *med){
    int NumberOfKeys = node->number_of_keys;
    auto *newNode = new Node<T,Order>(order);
    //Node<T,Order> *newParentNode = new Node<T,Order>(order);
    int midValue = NumberOfKeys / 2;
    *med = node->keys[midValue];
    int i;
    //take the values after mid-value
    for (i = midValue + 1; i < NumberOfKeys; ++i){
        newNode->keys[++newNode->position] = node->keys[i];
        newNode->children[newNode->position] = node->children[i];
        ++newNode->number_of_keys;
        --node->position;
        --node->number_of_keys;
        node->children[i] = nullptr;
    }
    newNode->children[newNode->position + 1] = node->children[i];
    node->children[i] = nullptr;

    --node->number_of_keys;
    --node->position;
    return newNode;
}


template <class T,usize Order>
usize Node<T,Order>::getHeight(){
    usize COUNT=1;
    Node<T,Order>* Current=this;
    while(true){
        if(Current->children[0] == nullptr)
            return COUNT;
        Current = Current->children[0];
        COUNT++;
    }
}

template <class T,usize Order>
Node<T,Order>::~Node (){
    delete[]keys;
    // Only delete non-NULL child pointers
    for (int i = 0; i <= this->number_of_keys; ++i)
        delete children[i];
    delete[] children;
}


    Node<T,Order> *root;
    int order;
    int count=0;

public:
    BTree ();
    void Insert (T value);
    ~BTree ();
};


template <class T,std::size_t Order>
BTree<T,Order>::BTree(){
    this->order = Order;
    this->root = nullptr;
    this->count = 0;
}

template <class T,std::size_t Order>
void BTree<T,Order>::Insert (T value){
    count++;
    if (this->root == nullptr){
        this->root = new Node<T,Order>(this->order);
        this->root->keys[++this->root->position]=value;
        this->root->number_of_keys=1;
    }
    else{
        int check=root->Insert(value);
        if(check){
            T mid;
            Node<T,Order> *splitNode = this->root->split(this->root, &mid);
            auto *newNode = new Node<T,Order>(this->order);
            newNode->keys[++newNode->position]=mid;
            newNode->number_of_keys=1;
            newNode->children[0] = root;
            newNode->children[1] = splitNode;
            this->root = newNode;
        }
    }
}


template <class T,std::size_t Order>
BTree<T,Order>::~BTree (){
    delete root;
}


}//ds
}//bstd