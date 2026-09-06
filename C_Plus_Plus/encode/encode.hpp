#include <vector>
#include <map>
#include <utility>
#include <string>
#include <queue>
#include <memory>
#include <stdexcept>
#include <bitset>
namespace bstd {
namespace encode {



class huffmanTree {

private:

class BitBuffer {
    using u8 = u_int8_t;
    using size_t = std::size_t;

private:
    std::vector<u8> _bytes; 
    size_t _bitCount = 0;   // total bits written



    // Which byte index does bit i live in?
    static constexpr size_t byteIndex(size_t i) { return i / 8; }
    // Which bit within that byte? (MSB-first)
    static constexpr size_t bitOffset(size_t i) { return 7 - (i % 8); }

public:
    //  Proxy — returned by operator[] for read/write access
    //nested class Bitref
    class BitRef {
        u8&    byte;
        size_t offset;   // 0–7, where 7 = MSB

    public:
        BitRef(u8& b, size_t o) : byte(b), offset(o) {}

        // Write: buf[i] = true
        BitRef& operator=(bool val) {
            if (val) byte |=  (1u << offset);
            else byte &= ~(1u << offset);
            return *this;
        }

        // Copy-assign between two proxies
        BitRef& operator=(const BitRef& other) {
            return *this = static_cast<bool>(other);
        }

        // Read: bool x = buf[i]
        operator bool() const {
            return (byte >> offset) & 1u;
        }
    };

    // Construction
    BitBuffer() = default;

    // Pre-allocate for n bits (all zero)
    explicit BitBuffer(size_t n): _bytes((n + 7) / 8, 0), _bitCount(n){}

    // Build from raw bytes (all bits counted)
    explicit BitBuffer(std::vector<u8> raw): _bytes(std::move(raw)), _bitCount(_bytes.size() * 8){}


    explicit BitBuffer(std::string_view constString){
        std::bitset<8> byte{}; 
        unsigned char itr = 0;
        std::vector<unsigned char> send{};
        for(const char c: constString){
            if(itr == 8){
                u8 current = static_cast<unsigned char>(byte.to_ulong());
                send.push_back(current);
                itr = 0;
            }
            if(c == '0'){
                byte.set(itr) = false;
            }else{
                byte.set(itr) = true;
            }
            itr++;
        }
        _bytes = (std::move(send));
        _bitCount = (_bytes.size() * 8);
    }

    explicit BitBuffer(std::string String){
        std::string_view view(String);
        BitBuffer _x(view);
        
    }

    //  Bit pushing
    void push(bool bit) {
        if (_bitCount % 8 == 0)
            _bytes.push_back(0);
        if (bit){
            _bytes.back() |= (1u << bitOffset(_bitCount));
        }
        ++_bitCount;
    }

    void push(u8 byte, int nBits = 8) {
        if (nBits < 1 || nBits > 8)
            throw std::out_of_range("nBits must be 1–8");
        for (int i = nBits - 1; i >= 0; --i)
            push(static_cast<bool>((byte >> i) & 1u));
    }

    // Append every bit from another BitBuffer obj
    void push(const BitBuffer& _other) {
        for (size_t i = 0; i < _other._bitCount; ++i)
            push(static_cast<bool>(_other[i]));
    }

    //  Element access
    BitRef operator[](size_t i) {
        if (i >= _bitCount) throw std::out_of_range("BitBuffer index out of range");
        return { _bytes[byteIndex(i)], bitOffset(i) };
    }

    bool operator[](size_t i) const {
        if (i >= _bitCount) throw std::out_of_range("BitBuffer index out of range");
        return (_bytes[byteIndex(i)] >> bitOffset(i)) & 1u;
    }
    void operator+=(const bool b){
        _bytes.push_back(b);
    }

    bool at(size_t i) const { 
        return (*this)[i]; }   // explicit checked read

    //  Byte-level access (for file I/O, sockets, etc.)
    const u8* data() const {
        return _bytes.data();
    }

    const std::vector<u8>& rawBytes()const {
        return _bytes; 
    }
    size_t byteSize() const{ 
        return _bytes.size();
    }

    // Capacity / state
    std::size_t size() const {
         return _bitCount;
    }
    bool isEmpty() const{
         return (_bitCount == 0);
    }

    void reserve(size_t nBits){
        _bytes.reserve((nBits + 7) / 8);
    }

    void clear() { 
        _bytes.clear(); 
        _bitCount = 0;}

    //  Iteration (read-only)
    struct Iterator {
        const BitBuffer& buf;
        size_t           idx;

        bool operator*() const {
            return buf[idx]; 
        }
        //increment
        Iterator& operator++() {
            ++idx; return *this; 
        }
        //does not equal
        bool operator!=(const Iterator& o) const {
            return idx != o.idx; }
    };

    Iterator begin() const { return { *this, 0 }; }
    Iterator end()   const { return { *this, _bitCount }; }

/**
 * Utility 
 */
    //Readable string
    std::string toString() const{
        std::string s;
        for (size_t i = 0; i < _bitCount; ++i) {
            if (i > 0 && i % 8 == 0) s += ' ';
            s += ((*this)[i] ? '1' : '0');
        }
        // show padding bits in final byte
        size_t pad = (8 - _bitCount % 8) % 8;
        if (pad) {
            s += '|';
            for (size_t i = 0; i < pad; ++i) s += '.';
        }
        return s;
    }

    // How many padding bits exist in the last byte
    size_t paddingBits() const {
        return _bitCount == 0 ? 0 : (8 - _bitCount % 8) % 8;
    }
    /**
    * Readable bitset utility
     */
    std::vector<std::bitset<8>> toBitset() const{
        std::vector<std::bitset<8>> r;
        for(const unsigned char b : _bytes){
            r.emplace_back(static_cast<unsigned long>(b));
        }
        return r;
    }
};








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
    std::map<char, BitBuffer> codeTable;

    void buildCodes(const std::shared_ptr<Node>& node, BitBuffer code) {
        if (!node) return;
        if (node->isLeaf()) {
            if (code.isEmpty()) code.push(false);   // single-symbol edge case
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

    std::shared_ptr<Node> deserializeNode(const BitBuffer& bits, size_t& idx) {
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
    explicit huffmanTree(const BitBuffer& bits) {
        size_t idx = 0;
        root = deserializeNode(bits, idx);
        buildCodes(root, {});
    }

    /** Returns the Huffman bit-code for a single character. */
    BitBuffer what_is_char(char x) const {
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


}//namespace encode
} // namespace bstd