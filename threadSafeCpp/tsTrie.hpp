
#include <memory>
#include <string>
#include <mutex>
#include <vector>
#include <shared_mutex>
#include <thread>
#include <syncstream>
#include <variant>
#include <cassert>
#include <type_traits>
#include <iomanip>
#include <exception>
#include <iostream>
#include <optional>

using sizeT = std::size_t;
using string = std::string;
using optFlag = std::optional<bool>;
namespace bstd{
namespace ds{
namespace ts{

class Trie{
    public:
    /**
     * @version - 1.0
     * @brief node class is wrapped inside Trie class and not accessable by user
     * 
     * 
     * uses a hybrid design of spinlocks on deep nodes to save memory, and mutexes on high contention 
     * points in the tree. I have a factory in node to help determine this, with an option to set this within the
     * trie constructor and at what depth. I will pick a safe, default depth.
     * There is alot to this class so I will attempt to explain it the best I can:
     * @struct mutexLock a mutex wrapper, used for higher nodes 
     * @struct spinLock is a spinLock as the name suggest 
     * @struct node acts as its own unit, creates its own lock based on level
     * @details has methods for adding words, removing words, removing all instances of word, clear entire tree, 
     * and create vector of all words (returned as a string)
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
struct node {
    char value;
    bool isEndpoint;
    bool isRoot;
    sizeT count;
    std::vector<std::unique_ptr<node>> childrenNodes;
    std::variant<mutexLock, spinLock> nodeLock;
    node(char val) : value(val), isEndpoint(false), isRoot(false), count(0) {};

    struct lockGuard {
        std::variant<mutexLock, spinLock>& _lock;
        bool _owns;

        explicit lockGuard(std::variant<mutexLock, spinLock>& l) : _lock(l), _owns(true) {
            std::visit([](auto& lk) { lk.lock(); }, _lock);
        }

        ~lockGuard() {
            if (_owns){ //if owner
                std::visit([](auto& lk) { lk.unlock(); }, _lock);
            }
        }

        lockGuard(const lockGuard&) = delete;
        lockGuard& operator=(const lockGuard&) = delete;

        lockGuard(lockGuard&& other) noexcept : _lock(other._lock), _owns(other._owns) {
            other._owns = false; //transfer ownership, prevent double unlock
        }

            lockGuard& operator=(lockGuard&&) = delete;
            }; //end of lockguard
        };// end of node

    private:
        std::unique_ptr<node> v_root; 
        std::atomic<sizeT> v_nodeCount;
        std::atomic<sizeT> wordCount;
        sizeT v_mutexCutoff; 

        void setEndpointTrue(node& n){
            node::lockGuard guard(n.nodeLock);
            n.isEndpoint = true;
        }

        void increment(node& n){
            node::lockGuard guard(n.nodeLock);
            n.count++;
        }

        void getAllWords(std::vector<std::string>& put, std::string prefix, node* current){
            if(current == nullptr) return;
    
            std::string currentWord = prefix + current->value; 
    
            if(current->isEndpoint){
                put.push_back(currentWord);
                }
            for(auto& child : current->childrenNodes){
                getAllWords(put, currentWord, child.get()); 
            }
        }

        // returns true if the current node should be deleted by its parent
        bool removeHelper(node* current, const string& word, sizeT depth){
            if(depth == word.size()){ //base case
                if(!current->isEndpoint) return false; // word doesn't exist
        
                node::lockGuard guard(current->nodeLock);
                current->isEndpoint = false;
                current->count = 0;
        
                // tell parent to delete us only if we have no children(no other words run through this node)
                return current->childrenNodes.empty();
            }

                // find the child matching the next character
            char ch = word[depth];
            node* next = findChildNode(*current, ch);
    
            if(next == nullptr) return false; // word doesn't exist in trie

            bool shouldDeleteChild = removeHelper(next, word, depth + 1);

            if(shouldDeleteChild){
                node::lockGuard guard(current->nodeLock);
                // erase the unique_ptr from the vector, which triggers child's destructor
                // which cascades down through all of that child's unique_ptr children
                auto itr = std::find_if(current->childrenNodes.begin(), 
                               current->childrenNodes.end(),
                               [ch](const std::unique_ptr<node>& n){ 
                                   return n->value == ch; 
                               });
                if(itr != current->childrenNodes.end()){
                    current->childrenNodes.erase(itr);
                }
            }

            // tell parent to delete us if now a dead non word
            return !current->isEndpoint && current->childrenNodes.empty();
            }


    public: //user methods
        Trie() {
            v_root = std::make_unique<node>('*');
            v_root->isEndpoint = false;
            v_mutexCutoff = 3; // safe default
        }

        Trie(string initial) : Trie() {
            add(initial);
        }

        Trie(sizeT HIGH_CONTENTION_CUTOFF) : Trie() {
            try {
                if (HIGH_CONTENTION_CUTOFF < 3){
                    throw std::invalid_argument("HIGH_CONTENTION_CUTOFF cannot be below 2");
                }
                this->v_mutexCutoff = HIGH_CONTENTION_CUTOFF;
            } catch (const std::exception& error) {
                std::cerr << error.what() << '\n';
                std::cout << "Trie creation failed" << '\n';
            }
        }

        Trie(string initial, sizeT HIGH_CONTENTION_CUTOFF) : Trie(HIGH_CONTENTION_CUTOFF) {
                add(initial);
        }

        //deletes all of roots children
        void clear(){
            node::lockGuard guard(v_root->nodeLock);
            v_root.get()->childrenNodes.clear(); //clear the vec and the children go out of scope
            wordCount.store(0);
            v_nodeCount.store(0);
        }

        bool getIsEndPoint(node* thisNode) const{
            if(thisNode->isEndpoint){
                return true;
            }
            return false;
        }

    
        sizeT getWordCount(){
            return this->wordCount;
        }


        bool find(string word) {
            node::lockGuard guard(v_root->nodeLock);
            node* current = v_root.get();
            for (char c : word) {
                node* next = findChildNode(*current, c);
                if (next == nullptr)
                    return false;
                current = next;
            }
            return current->isEndpoint;
        }

    //returns a node at requested position, if not returns nullptr
        node* findChildNode(node& n, const char lookingFor){
        
            for(int i = 0; i < n.childrenNodes.size(); i++){
                if(lookingFor == n.childrenNodes.at(i)->value){
                    return n.childrenNodes.at(i).get();
                }
            }
            return nullptr;
        }

        sizeT getChildCount(node n) const{
            return n.childrenNodes.size();
        }


            //adds entire word to the trie. 
        void add(std::string word){
            node* current = v_root.get();
            
            for(sizeT i = 0; i < word.length(); i++){
                char ch = word[i];
                node::lockGuard guard(current->nodeLock);  // locks here
        
                node* thisChild = nullptr;
            for(auto& c : current->childrenNodes){
                if(c->value == ch){
                    thisChild = c.get();
                    break;
                }
            }
            if(thisChild == nullptr){
                current->childrenNodes.emplace_back(std::make_unique<node>(ch));
                thisChild = current->childrenNodes.back().get();
        }
        
        current = thisChild;
    }  // guard goes out of scope here it will unlock
    
    setEndpointTrue(*current);
    increment(*current);
    wordCount++;
    }

    std::vector<string> getWords(){
        std::vector<string> r;
        r.reserve(this->getWordCount());
        for(auto& child : v_root->childrenNodes){
            getAllWords(r, "", child.get());
        }
        return r;
    }

    //return false if the remove fails
    optFlag remove(string toRemove) {
        if(!find(toRemove)){
            return false;}

        removeHelper(v_root.get(), toRemove, 0);
        wordCount.fetch_sub(1);
        return true;
    }

    optFlag remove(string toRemove, bool removeAll) {
        if(!find(toRemove)){
            return false;}
        //start traversal
        node* current = v_root.get();
            for (char c : toRemove) {
                current = findChildNode(*current, c);
            if(current == nullptr){ //return early if we ever hit nullptr
                return false;
            }
            }

            if(!current->isEndpoint){ //if its not a endpoint, dont do anything
                return false;
            }

            if(removeAll){
                //remove all insertions of this word
                sizeT countToRemove = current->count;
                removeHelper(v_root.get(), toRemove, 0);
                wordCount.fetch_sub(countToRemove);
            }else{
                //remove only one occurrence
                current->count--;
                if (current->count == 0) {
                    //no more occurrences, trim node
                    removeHelper(v_root.get(), toRemove, 0);
                }
                wordCount.fetch_sub(1);
            }

            return true;
        }



    }; //end of trie

}
}

}//bstd