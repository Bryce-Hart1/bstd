#pragma once

#include <algorithm>
#include <memory>
#include <mutex>       
#include <optional>
#include <shared_mutex>  
#include <string>
#include <utility>        
#include <vector>

namespace bstd{
namespace ds{
namespace ts{

class Trie{
using sizeT = std::size_t;
using string = std::string;
using optFlag = std::optional<bool>;

    /**
     * @author Bryce Hart @date Aug 15 2026
     * @version 2.0
     * @brief A thread safe trie. The node type is private and never escapes the class. 
     * 2.0 provides a much smaller node size (40 now instead of 104º
     * 
     *
     * CONCURRENCY MODEL: one reader/writer lock over the whole structure
     * 
     * @details  
     * A single std::shared_mutex (v_mtx) guards the entire tree. Every public
     * method acquires it on entry and holds it until it returns:
     *
     *   readers (find, getWords, getWordCount, ...) -> std::shared_lock  (many at once)
     *   writers (add, remove, clear)                -> std::unique_lock  (exclusive)
     *
     * The invariant this buys us: the shape of the tree cannot change while any
     * thread holds a pointer into it. A reader's node* stays valid for its whole
     * traversal because no writer can be running at all. That is what the old
     * per-node locking could not provide -- it released the parent's lock before
     * descending, so a concurrent emplace_back could reallocate childrenNodes
     * under a reader, and a concurrent erase could free the node a reader was
     * standing on.
     *
     * ---------------------------------------------------------------------------
     * LOCKING DISCIPLINE -- read this before adding a method
     * ---------------------------------------------------------------------------
     * std::shared_mutex is NOT recursive. Acquiring it twice on one thread is a
     * deadlock. So the class is split in two layers:
     *
     *   public methods   -> acquire the lock, then immediately delegate.
     *   *_unlocked names -> assume the caller already holds the lock, and must
     *                       NEVER touch v_mtx themselves.
     *
     * All internal recursion and all cross-calls between operations go through
     * the _unlocked layer. (This is exactly why remove() cannot call find():
     * it calls findNode_unlocked() instead.)
     *
     * Never take a shared lock and then try to "upgrade" to exclusive --
     * shared_mutex has no upgrade, and unlock-then-relock reopens the very race
     * window this design closes. If an operation might write, take it exclusive
     * from the start.
     *
     * ---------------------------------------------------------------------------
     * TRADEOFF
     * ---------------------------------------------------------------------------
     * Writes serialize globally: two threads adding words in disjoint subtrees
     * block each other. Reads are now genuinely parallel. For the usual trie
     * workload (built once or occasionally, queried heavily) this is a win.
     * Revisit only if profiling shows real write contention.
     */

    private:
        /**
         * @struct node
         * A single character in the tree. Carries no lock of its own -- all
         * synchronization lives in Trie::v_mtx. Children are owned by unique_ptr,
         * so erasing a child cascades destruction through its whole subtree.
         *
         * @var count how many times the word ending at this node was inserted.
         *      Invariant: isEndpoint == true  <=>  count > 0.
         */
        struct node {
            char value;
            bool isEndpoint;
            sizeT count;
            std::vector<std::unique_ptr<node>> childrenNodes;

            explicit node(char val) : value(val), isEndpoint(false), count(0){}
        };

        mutable std::shared_mutex v_mtx; // mutable so const readers can still lock
        std::unique_ptr<node> v_root;
        sizeT v_nodeCount;  // live nodes, excluding the root sentinel
        sizeT wordCount;  // total insertions, counting duplicates

        // Every function below assumes v_mtx is already held by the caller.
        // Because of that they are also free to be plain non-atomic reads/writes.

        /// @return child of n holding lookingFor, or nullptr. Const overload.
        static const node* findChild_unlocked(const node& n, char lookingFor){
            for(const auto& child : n.childrenNodes){
                if(child->value == lookingFor){
                    return child.get();
                }
            }
            return nullptr;
        }

        /// Non-const overload. Delegates to the const version to avoid duplicating
        /// the scan; the const_cast is safe because we were handed a non-const node.
        static node* findChild_unlocked(node& n, char lookingFor){
            return const_cast<node*>(findChild_unlocked(static_cast<const node&>(n), lookingFor));
        }

        /// Walk the full path for word. @return the terminal node, or nullptr if
        /// the path does not exist. An empty word yields nullptr -- the root is a
        /// sentinel, not a word.
        const node* findNode_unlocked(const string& word) const {
            if(word.empty()) return nullptr;

            const node* current = v_root.get();
            for(char c : word){
                current = findChild_unlocked(*current, c);
                if(current == nullptr) return nullptr;
            }
            return current;
        }

        node* findNode_unlocked(const string& word){
            return const_cast<node*>(std::as_const(*this).findNode_unlocked(word));
        }

        /// Depth first collection of every complete word in the subtree rooted at
        /// current. prefix is threaded through by reference and push/pop'd around
        /// the recursion, so the whole traversal reuses one buffer instead of
        /// building a fresh concatenated string at every node.
        void getAllWords_unlocked(std::vector<string>& put, string& prefix, const node* current) const {
            if(current == nullptr) return;

            prefix.push_back(current->value);

            if(current->isEndpoint){
                put.push_back(prefix);
            }
            for(const auto& child : current->childrenNodes){
                getAllWords_unlocked(put, prefix, child.get());
            }

            prefix.pop_back();  // restore for our sibling
        }

        /// @return number of nodes in the subtree rooted at n, including n itself.
        /// Used to keep v_nodeCount honest when a subtree is about to be erased.
        static sizeT countSubtree_unlocked(const node* n){
            if(n == nullptr) return 0;

            sizeT total = 1;
            for(const auto& child : n->childrenNodes){
                total += countSubtree_unlocked(child.get());
            }
            return total;
        }

        /**
         * Recursive removal.
         * @param removeAll true -> drop every insertion of the word,
         *                  false -> drop a single occurrence.
         * @return true if current is now dead (not an endpoint, no children) and
         *         should therefore be erased by its parent.
         */
        bool removeHelper_unlocked(node* current, const string& word, sizeT depth, bool removeAll){
            if(depth == word.size()){  // base case: this is the word's terminal node
                if(!current->isEndpoint) return false;   // word isn't actually here

                if(removeAll){
                    current->count = 0;
                }else if(current->count > 0){
                    current->count--;
                }

                if(current->count > 0){
                    return false;  // duplicates remain, node stays an endpoint
                }

                current->isEndpoint = false;
                // only ask the parent to erase us if no other word runs through here
                return current->childrenNodes.empty();
            }

            char ch = word[depth];
            node* next = findChild_unlocked(*current, ch);
            if(next == nullptr) return false;  // word doesn't exist in trie

            if(removeHelper_unlocked(next, word, depth + 1, removeAll)){
                auto itr = std::find_if(current->childrenNodes.begin(),
                                        current->childrenNodes.end(),
                                        [ch](const std::unique_ptr<node>& n){
                                            return n->value == ch;
                                        });
                if(itr != current->childrenNodes.end()){
                    // count the subtree before it dies, then erase -- dropping the
                    // unique_ptr cascades destruction through every descendant
                    v_nodeCount -= countSubtree_unlocked(itr->get());
                    current->childrenNodes.erase(itr);
                }
            }

            // tell our own parent to erase us if we just became a dead branch
            return !current->isEndpoint && current->childrenNodes.empty();
        }

        /// Insert one occurrence. Empty words are ignored: the root is a sentinel,
        /// so there is no node that could represent "".
        void add_unlocked(const string& word){
            if(word.empty()) return;

            node* current = v_root.get();
            for(char ch : word){
                node* thisChild = findChild_unlocked(*current, ch);
                if(thisChild == nullptr){
                    current->childrenNodes.emplace_back(std::make_unique<node>(ch));
                    thisChild = current->childrenNodes.back().get();
                    v_nodeCount++;
                }
                current = thisChild;
            }

            current->isEndpoint = true;
            current->count++;
            wordCount++;
        }

        /// Shared body of both remove() overloads. @return false if the word isn't
        /// present, true once it has been removed.
        optFlag remove_unlocked(const string& toRemove, bool removeAll){
            const node* target = findNode_unlocked(toRemove);

            // count == 0 should be impossible for an endpoint, but check it so a
            // corrupted invariant can never underflow wordCount
            if(target == nullptr || !target->isEndpoint || target->count == 0){
                return false;
            }

            const sizeT removed = removeAll ? target->count : 1;
            removeHelper_unlocked(v_root.get(), toRemove, 0, removeAll);
            wordCount -= removed;
            return true;
        }

    public:
        /// The root is a sentinel holding no real character; traversal always
        /// starts at its children.
        Trie() : v_root(std::make_unique<node>('*')), v_nodeCount(0), wordCount(0) {}

        explicit Trie(const string& initial) : Trie() {
            add(initial);
        }

        // Non-copyable (unique_ptr children) and non-movable (a shared_mutex can
        // be neither copied nor moved). Spelled out so the compiler error is
        // obvious rather than a wall of template noise.
        Trie(const Trie&)            = delete;
        Trie& operator=(const Trie&) = delete;
        Trie(Trie&&)                 = delete;
        Trie& operator=(Trie&&)      = delete;

        /// Drop every word. Erasing the root's children cascades through the tree.
        void clear(){
            std::unique_lock lock(v_mtx);
            v_root->childrenNodes.clear();
            wordCount   = 0;
            v_nodeCount = 0;
        }

        /// Insert one occurrence of word. Inserting the same word twice leaves a
        /// count of 2, and it then takes two single removes to clear it.
        void add(const string& word){
            std::unique_lock lock(v_mtx);
            add_unlocked(word);
        }

        /// @return true if word was inserted at least once and not yet removed.
        bool find(const string& word) const {
            std::shared_lock lock(v_mtx);
            const node* target = findNode_unlocked(word);
            return target != nullptr && target->isEndpoint;
        }

        /// @return how many times word is currently held (0 if absent).
        sizeT countOf(const string& word) const {
            std::shared_lock lock(v_mtx);
            const node* target = findNode_unlocked(word);
            return (target != nullptr && target->isEndpoint) ? target->count : 0;
        }

        /// @return number of direct children of the node at the end of prefix.
        /// An empty prefix asks about the root, i.e. how many distinct first
        /// characters the trie holds. 0 if the prefix isn't in the tree.
        sizeT getChildCount(const string& prefix) const {
            std::shared_lock lock(v_mtx);

            const node* target = prefix.empty() ? v_root.get() : findNode_unlocked(prefix);
            return target == nullptr ? 0 : target->childrenNodes.size();
        }

        /// Total insertions, duplicates included.
        sizeT getWordCount() const {
            std::shared_lock lock(v_mtx);
            return wordCount;
        }

        /// Live nodes, excluding the root sentinel.
        sizeT getNodeCount() const {
            std::shared_lock lock(v_mtx);
            return v_nodeCount;
        }

        /// Snapshot of every distinct word. Duplicates appear once. Returns by
        /// value on purpose: the strings are copied out while the lock is held, so
        /// nothing pointing into the tree ever escapes it.
        std::vector<string> getWords() const {
            std::shared_lock lock(v_mtx);

            std::vector<string> r;
            string prefix;
            for(const auto& child : v_root->childrenNodes){
                getAllWords_unlocked(r, prefix, child.get());
            }
            return r;
        }

        /// Remove a single occurrence. @return false if the word isn't present.
        optFlag remove(const string& toRemove){
            std::unique_lock lock(v_mtx);
            return remove_unlocked(toRemove, false);
        }

        /// @param removeAll true drops every occurrence at once, false drops one.
        optFlag remove(const string& toRemove, bool removeAll){
            std::unique_lock lock(v_mtx);
            return remove_unlocked(toRemove, removeAll);
        }

    }; //end of trie

}
}
}//bstd end
