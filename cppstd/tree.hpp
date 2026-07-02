#include <vector>
#include <memory>
#include <exception>
#include <stack>
#include <vector>
#include <queue>

/**
 * Tree V1 Bryce Hart
 * tree to add, remove, and scan from 
 * 
 */
namespace bstd{
namespace ds{
template<typename T>
class tree{

    //each node is assigned a number to keep track of its "place" in the tree
    class node{
        private:
        T _data;
        std::vector<std::unique_ptr<node>> _children;
        node(){}
    }

    friend class node;
    private:
    node _root;
    std::size_t _size;
    std::vector<std::unique_ptr<node>> _nodeLocation;

    tree(T root_Data){
        node n();
        n._data = root_Data;
    }

    void check_and_throw_range(std::size_t node_number){
        if(node_number > this->_size || node_number < 0){
            throw std::runtime_error("Node to add " + std::to_string(node_number) + " is out of range.");
        }
    }
    public:

    //must know node number to be able to add
    void add_child_to(std::size_t node_number){
        check_and_throw_range(node_number);
        node found this->_nodeLocation.at(node_number);
    }

    //cut from node node_number. All children of this nodes and beyond this node will be deleted
    void cut(std::size_t node_number){

    }


    std::vector<T> run_dfs(){
        std::vector<T> rtnVec;
        rtnVec.reserve(this->size());
        if (this->_root == nullptr) {
            return rtnVec;
        }
        std::stack<node> next;
        next.push(this->_root);
        while(!next.empty()){
            node current = next.top();
            next.pop();
            rtnVec.push_back(current._data);
            for(auto& child : current._children){
                next.push(child);
            }
        }        
        return rtnVec;
    }

    std::vector<T> run_bfs() {
        std::vector<T> rtnVec;
        rtnVec.reserve(this->size());
        if (this->_root == nullptr) {
            return rtnVec;
        }

        std::queue<node> q;
        q.push(this->_root);

        while (!q.empty()) {
            node current = q.front();
            q.pop();
            rtnVec.push_back(current._data); 
            for (auto& child : current._children) {
                q.push(child);
            }
        }

        return rtnVec;
    }

    //input number of node to find
    std::unique_ptr<node> find(std::size_t node_to_find){
        check_and_throw_range(node_to_find);
            if (this->_root == nullptr) {
                return nullptr;
            }

            std::queue<node> q;
            q.push(this->_root);

            while (!q.empty()) {
                node current = q.front();
                q.pop();
                if(current._number == node_to_find){
                    return std::unique_ptr<current>;
                }
                for (auto& child : current._children) {
                    q.push(child);
                }
            }       
        return nullptr;
    }

        std::unique_ptr<node> find_first_instance_of(T _val){
            if (this->_root == nullptr) {
                return nullptr;
            }

            std::queue<node> q;
            q.push(this->_root);

            while (!q.empty()) {
                node current = q.front();
                q.pop();
                if(current._data == val){
                    return std::unique_ptr<current>;
                }
                for (auto& child : current._children) {
                    q.push(child);
                }
            }            
            return nullptr;
    }

    std::size_t place_in_tree(){
        
    }

    std::size_t size(){
        return this->_size();
    }


};
}
}