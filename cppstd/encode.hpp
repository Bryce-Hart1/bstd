#include <vector>
#include <map>
#include <algorithm>
#include <utility>
#include <ranges>
#include <string>
#include <queue>
#include <memory>
#include <stdexcept>
#include "bitBuffer.hpp"

namespace bstd {
namespace encode {



class huffmanTree {
    using u8 = uint8_t;
    using size_t = std::size_t;

    //node def
    struct Node {
        char ch    = '\0';
        int  freq  = 0;
        std::shared_ptr<Node> left, right;

        // Leaf constructor
        Node(char c, int f) : ch(c), freq(f) {}
        // Internal constructor
        Node(int f, std::shared_ptr<Node> l, std::shared_ptr<Node> r)
            : freq(f), left(std::move(l)), right(std::move(r)) {}

        bool isLeaf() const {
            return !left && !right; }
    };

    std::shared_ptr<Node> root;
    std::map<char, bstd::bit::BitBuffer> codeTable;

    void buildCodes(const std::shared_ptr<Node>& node, bstd::bit::BitBuffer code) {
        if (!node) return;
        if (node->isLeaf()) {
            if (code.empty()) code.push(false);   // single-symbol edge case
            codeTable[node->ch] = std::move(code);
            return;
        }
        auto lc = code; lc.push(false);  // left  = 0
        auto rc = code; rc.push(true);   // right = 1
        buildCodes(node->left,  std::move(lc));
        buildCodes(node->right, std::move(rc));
    }

    // Tree serialization (pre-order):  
    // leaf -> 1 followed by 8 bits of the char
    // internal -> 0, then left subtree, then right subtree
    void serializeNode(const std::shared_ptr<Node>& node, std::vector<bool>& bits) const {
        if (!node) return;
        if (node->isLeaf()) {
            bits.push_back(true);
            u8 c = static_cast<u8>(node->ch);
            for (int i = 7; i >= 0; --i)
                bits.push_back((c >> i) & 1u);
        } else {
            bits.push_back(false);
            serializeNode(node->left,  bits);
            serializeNode(node->right, bits);
        }
    }

    std::shared_ptr<Node> deserializeNode(const bstd::bit::BitBuffer& bits, size_t& idx) {
        if (idx >= bits.size())
            throw std::runtime_error("Truncated tree bitstream");

        if (bits[idx++]) {          // leaf
            if (idx + 8 > bits.size())
                throw std::runtime_error("Truncated char in tree bitstream");
            char c = 0;
            for (int i = 7; i >= 0; --i)
                c |= static_cast<char>(bits[idx++]) << i;
            return std::make_shared<Node>(c, 0);
        }
        // internal node
        auto left  = deserializeNode(bits, idx);
        auto right = deserializeNode(bits, idx);
        return std::make_shared<Node>(0, std::move(left), std::move(right));
    }

public:
    //  Constructor 1 – build from (char, frequency) pairs
    explicit huffmanTree(std::vector<std::pair<char, int>> pairs) {
        if (pairs.empty())
            throw std::invalid_argument("Cannot build Huffman tree from empty input");

        // Min-heap ordered by frequency
        auto cmp = [](const std::shared_ptr<Node>& a,
                      const std::shared_ptr<Node>& b) {
            return a->freq > b->freq;
        };
        std::priority_queue<std::shared_ptr<Node>,
                            std::vector<std::shared_ptr<Node>>,
                            decltype(cmp)> pq(cmp);

        for(auto& [ch, freq] : pairs)
            pq.push(std::make_shared<Node>(ch, freq));

        // Single-symbol edge case: wrap in a dummy internal node
        if(pq.size() == 1) {
            auto only = pq.top(); pq.pop();
            root = std::make_shared<Node>(only->freq, only, nullptr);
        }else{
            while (pq.size() > 1) {
                auto lo = pq.top(); pq.pop();   // lowest freq
                auto hi = pq.top(); pq.pop();   // second lowest
                pq.push(std::make_shared<Node>(lo->freq + hi->freq,
                                               std::move(lo), std::move(hi)));
            }
            root = pq.top();
        }
        buildCodes(root, {});
    }

    // Constructor 2 – rebuild from a serialized bitstream
    explicit huffmanTree(const bstd::bit::BitBuffer& bits) {
        size_t idx = 0;
        root = deserializeNode(bits, idx);
        buildCodes(root, {});
    }

    /** Returns the Huffman bit-code for a single character. */
    bstd::bit::BitBuffer what_is_char(char x) const {
        auto it = codeTable.find(x);
        if (it == codeTable.end()){
            throw std::runtime_error(std::string("character [") + x + "] not found in tree");
        }
        return it->second;
    }

    /** Serialize the tree structure to bits (for storage/transmission). */
    std::vector<bool> serialize() const {
        std::vector<bool> bits;
        serializeNode(root, bits);
        return bits;
    }

    /** Decode a raw bitstream back into a string using this tree. */
    std::string decode(const std::vector<bool>& bits) const {
        if (!root) return {};
        std::string result;
        auto node = root;
        for (bool bit : bits) {
            node = bit ? node->right : node->left;
            if (!node)
                throw std::runtime_error("Invalid bitstream: no node at bit");
            if (node->isLeaf()) {
                result += node->ch;
                node = root;// walk back to root for next symbol
            }
        }
        return result;
    }
};

//Free functions
bstd::bit::BitBuffer Huffman_Encode(const huffmanTree& tree, const std::string& encodeStr) {
    bstd::bit::BitBuffer rtn;
    for (char c : encodeStr) {
        rtn.push(tree.what_is_char(c));
    }
    return rtn;
}
std::string Huffman_Decode(const huffmanTree& tree, const std::vector<bool>& decodeStr) {
    return tree.decode(decodeStr);
}

bstd::bit::BitBuffer LM77_Encode(const std::string& incoming){

}

std::string LM77_Decode(const bstd::bit::BitBuffer& incoming){
    for(const bool& bit : incoming){ 
        if(bit){ //back reference
            
        }else{ //Literal

        }
    }
}

}//namespace encode
} // namespace bstd