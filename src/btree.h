#pragma once

// ---------------------------------------------------------------------------
// btree.h — In-memory order-N B+ Tree for the Xstd file catalog.
//
// Key properties:
//   • All values live in leaf nodes; internal nodes hold only separator keys.
//   • Leaf nodes are doubly-linked for O(n) ordered scan.
//   • Template parameters: Key, Value, Order (max keys per node), Compare.
//   • Order must be even and >= 4.
//
// Supported operations:
//   Insert(key, value)           O(log n)
//   Find(key) → optional<Value> O(log n)
//   Erase(key) → bool           O(log n)
//   PrefixScan(prefix, out)      O(log n + k)  — string keys; visits all keys starting with prefix
//   Begin / End (in-order leaf iteration)
//
// Serialisation:
//   Serialise(buf)   → appends encoded tree to byte vector
//   Deserialise(buf) → rebuilds tree from bytes
// ---------------------------------------------------------------------------

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace xstd {

// ---------------------------------------------------------------------------
// Serialisation helpers (little-endian)
// ---------------------------------------------------------------------------
namespace detail {

inline void WriteUint32(std::vector<uint8_t>& buf, uint32_t v) {
    for (int i = 0; i < 4; ++i) buf.push_back(static_cast<uint8_t>(v >> (i * 8)));
}
inline uint32_t ReadUint32(const uint8_t* p) {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8)
         | (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}
inline void WriteString(std::vector<uint8_t>& buf, const std::string& s) {
    WriteUint32(buf, static_cast<uint32_t>(s.size()));
    buf.insert(buf.end(), s.begin(), s.end());
}
inline std::string ReadString(const uint8_t*& p, const uint8_t* end) {
    if (p + 4 > end) throw std::runtime_error("BTree deserialise: truncated string length");
    uint32_t len = ReadUint32(p); p += 4;
    if (p + len > end) throw std::runtime_error("BTree deserialise: truncated string data");
    std::string s(reinterpret_cast<const char*>(p), len); p += len;
    return s;
}

} // namespace detail

// ---------------------------------------------------------------------------
// BTree<Key, Value, Order, Compare>
// ---------------------------------------------------------------------------
template<
    typename Key,
    typename Value,
    int      Order   = 64,    // max keys per node (must be even, >= 4)
    typename Compare = std::less<Key>
>
class BTree {
    static_assert(Order >= 4 && Order % 2 == 0, "Order must be even and >= 4");

    static constexpr int kOrder     = Order;
    static constexpr int kMinKeys   = Order / 2;   // min keys in non-root node

    // -----------------------------------------------------------------------
    // Node types
    // -----------------------------------------------------------------------
    struct Node {
        bool     is_leaf{false};
        int      num_keys{0};
        Key      keys[kOrder]{};        // separator keys (internal: kOrder-1 max; leaf: kOrder max)
        Node*    parent{nullptr};

        Node() = default;
        virtual ~Node() = default;
    };

    struct InternalNode : Node {
        Node* children[kOrder + 1]{};   // kOrder keys → kOrder+1 children

        InternalNode() { Node::is_leaf = false; }
    };

    struct LeafNode : Node {
        Value     values[kOrder]{};
        LeafNode* prev{nullptr};
        LeafNode* next{nullptr};

        LeafNode() { Node::is_leaf = true; }
    };

public:
    using KeyValuePair = std::pair<Key, Value>;

    // -----------------------------------------------------------------------
    // Construction & destruction
    // -----------------------------------------------------------------------
    BTree() : root_(MakeLeaf()), size_(0) {}

    ~BTree() { DestroyNode(root_); }

    BTree(const BTree&) = delete;
    BTree& operator=(const BTree&) = delete;

    // -----------------------------------------------------------------------
    // Insert
    // -----------------------------------------------------------------------
    void Insert(const Key& key, const Value& value) {
        auto [leaf, idx] = FindLeaf(key);
        // Check for duplicate key.
        if (idx < leaf->num_keys && !cmp_(leaf->keys[idx], key) && !cmp_(key, leaf->keys[idx])) {
            leaf->values[idx] = value;   // overwrite
            return;
        }
        InsertInLeaf(leaf, idx, key, value);
        ++size_;
        if (leaf->num_keys == kOrder) SplitLeaf(leaf);
    }

    // -----------------------------------------------------------------------
    // Find
    // -----------------------------------------------------------------------
    [[nodiscard]] std::optional<Value> Find(const Key& key) const {
        auto [leaf, idx] = FindLeaf(key);
        if (idx < leaf->num_keys && !cmp_(leaf->keys[idx], key) && !cmp_(key, leaf->keys[idx]))
            return leaf->values[idx];
        return std::nullopt;
    }

    // -----------------------------------------------------------------------
    // Erase
    // -----------------------------------------------------------------------
    bool Erase(const Key& key) {
        auto [leaf, idx] = FindLeaf(key);
        if (idx >= leaf->num_keys || cmp_(leaf->keys[idx], key) || cmp_(key, leaf->keys[idx]))
            return false;   // not found
        EraseFromLeaf(leaf, idx);
        --size_;
        // Note: We use a lazy-delete strategy — no rebalancing after deletion.
        // For a catalog-only workload (files are mostly added, rarely removed)
        // this is acceptable and keeps the implementation simple.
        // A full rebalance can be added later if needed.
        return true;
    }

    // -----------------------------------------------------------------------
    // Range / prefix scan
    // -----------------------------------------------------------------------

    /// Calls @p visitor(key, value) for every entry in ascending key order.
    void ScanAll(std::function<void(const Key&, const Value&)> visitor) const {
        LeafNode* leaf = LeftmostLeaf();
        while (leaf) {
            for (int i = 0; i < leaf->num_keys; ++i)
                visitor(leaf->keys[i], leaf->values[i]);
            leaf = leaf->next;
        }
    }

    /// For string keys: calls @p visitor for every entry whose key starts with @p prefix.
    void PrefixScan(const Key& prefix,
                    std::function<void(const Key&, const Value&)> visitor) const
        requires std::is_same_v<Key, std::string>
    {
        if (prefix.empty()) { ScanAll(visitor); return; }
        // Seek to first key >= prefix.
        auto [leaf, idx] = FindLeaf(prefix);
        while (leaf) {
            for (int i = (leaf == LeafAtIdx(leaf, idx) ? idx : 0); i < leaf->num_keys; ++i) {
                if (leaf->keys[i].substr(0, prefix.size()) != prefix) return;
                visitor(leaf->keys[i], leaf->values[i]);
            }
            idx  = 0;
            leaf = leaf->next;
        }
    }

    /// Collects all key-value pairs whose key starts with @p prefix.
    [[nodiscard]] std::vector<KeyValuePair> PrefixScanCollect(const Key& prefix) const
        requires std::is_same_v<Key, std::string>
    {
        std::vector<KeyValuePair> out;
        if (prefix.empty()) {
            ScanAll([&](const Key& k, const Value& v){ out.emplace_back(k, v); });
            return out;
        }
        auto [leaf, start_idx] = FindLeaf(prefix);
        LeafNode* cur  = leaf;
        int       idx  = start_idx;
        while (cur) {
            for (int i = idx; i < cur->num_keys; ++i) {
                if (cur->keys[i].substr(0, prefix.size()) != prefix) return out;
                out.emplace_back(cur->keys[i], cur->values[i]);
            }
            idx  = 0;
            cur  = cur->next;
        }
        return out;
    }

    // -----------------------------------------------------------------------
    // Accessors
    // -----------------------------------------------------------------------
    [[nodiscard]] std::size_t Size() const noexcept { return size_; }
    [[nodiscard]] bool        Empty() const noexcept { return size_ == 0; }

    // -----------------------------------------------------------------------
    // Serialisation
    // Encoding: DFS pre-order traversal.
    //   Each node is preceded by a 1-byte tag: 'I' = internal, 'L' = leaf, 'X' = null.
    //   Leaf node: [tag 'L'][uint32 num_keys]([key][value])...
    //   Internal node: [tag 'I'][uint32 num_keys]([key])...[child]*
    //
    // Key and Value serialisation must be provided as callbacks since BTree is
    // generic. For the Xstd catalog (string keys, FileMetadata values) the
    // catalog.h layer handles the callbacks.
    // -----------------------------------------------------------------------
    using KeySerializer   = std::function<void(std::vector<uint8_t>&, const Key&)>;
    using KeyDeserializer = std::function<Key(const uint8_t*&, const uint8_t*)>;
    using ValSerializer   = std::function<void(std::vector<uint8_t>&, const Value&)>;
    using ValDeserializer = std::function<Value(const uint8_t*&, const uint8_t*)>;

    void Serialise(std::vector<uint8_t>&  buf,
                   const KeySerializer&   ks,
                   const ValSerializer&   vs) const {
        SerialiseNode(root_, buf, ks, vs);
    }

    void Deserialise(std::span<const uint8_t> data,
                     const KeyDeserializer&   kd,
                     const ValDeserializer&   vd) {
        DestroyNode(root_);
        size_          = 0;
        first_leaf_    = nullptr;
        const uint8_t* p   = data.data();
        const uint8_t* end = p + data.size();
        root_ = DeserialiseNode(p, end, nullptr, kd, vd);
        // Rebuild the leaf linked list.
        RebuildLeafLinks(root_);
    }

private:
    // -----------------------------------------------------------------------
    // Internal helpers
    // -----------------------------------------------------------------------
    Compare   cmp_;
    Node*     root_{nullptr};
    LeafNode* first_leaf_{nullptr};
    std::size_t size_{0};

    // -- Allocation --
    static LeafNode*     MakeLeaf()     { return new LeafNode();     }
    static InternalNode* MakeInternal() { return new InternalNode(); }

    static void DestroyNode(Node* n) {
        if (!n) return;
        if (!n->is_leaf) {
            auto* in = static_cast<InternalNode*>(n);
            for (int i = 0; i <= in->num_keys; ++i) DestroyNode(in->children[i]);
        }
        delete n;
    }

    // -- Leaf traversal --
    [[nodiscard]] LeafNode* LeftmostLeaf() const {
        if (!root_) return nullptr;
        Node* n = root_;
        while (!n->is_leaf) n = static_cast<InternalNode*>(n)->children[0];
        return static_cast<LeafNode*>(n);
    }

    // Workaround helper for PrefixScan (returns same leaf).
    static LeafNode* LeafAtIdx(LeafNode* l, int /*idx*/) { return l; }

    // -- FindLeaf: returns leaf + insertion index --
    struct FindResult { LeafNode* leaf; int idx; };

    [[nodiscard]] FindResult FindLeaf(const Key& k) const {
        Node* n = root_;
        while (!n->is_leaf) {
            auto* in = static_cast<InternalNode*>(n);
            int i = 0;
            while (i < in->num_keys && cmp_(in->keys[i], k)) ++i;
            n = in->children[i];
        }
        auto* leaf = static_cast<LeafNode*>(n);
        int i = 0;
        while (i < leaf->num_keys && cmp_(leaf->keys[i], k)) ++i;
        return {leaf, i};
    }

    // -- InsertInLeaf (shift elements right by one) --
    static void InsertInLeaf(LeafNode* leaf, int idx, const Key& k, const Value& v) {
        for (int i = leaf->num_keys; i > idx; --i) {
            leaf->keys[i]   = std::move(leaf->keys[i - 1]);
            leaf->values[i] = std::move(leaf->values[i - 1]);
        }
        leaf->keys[idx]   = k;
        leaf->values[idx] = v;
        ++leaf->num_keys;
    }

    static void EraseFromLeaf(LeafNode* leaf, int idx) {
        for (int i = idx + 1; i < leaf->num_keys; ++i) {
            leaf->keys[i - 1]   = std::move(leaf->keys[i]);
            leaf->values[i - 1] = std::move(leaf->values[i]);
        }
        --leaf->num_keys;
    }

    // -- Split leaf: creates new right leaf, promotes middle key to parent --
    void SplitLeaf(LeafNode* leaf) {
        auto* right = MakeLeaf();
        int   mid   = kOrder / 2;

        // Copy top half to right leaf.
        right->num_keys = leaf->num_keys - mid;
        for (int i = 0; i < right->num_keys; ++i) {
            right->keys[i]   = std::move(leaf->keys[mid + i]);
            right->values[i] = std::move(leaf->values[mid + i]);
        }
        leaf->num_keys = mid;

        // Maintain leaf linked list.
        right->next = leaf->next;
        right->prev = leaf;
        if (leaf->next) leaf->next->prev = right;
        leaf->next = right;

        // Promote first key of right leaf to parent.
        InsertInParent(leaf, right->keys[0], right);
        right->parent = leaf->parent;
    }

    // -- InsertInParent: inserts (key, right) into parent of left --
    void InsertInParent(Node* left, const Key& key, Node* right) {
        if (left == root_) {
            auto* new_root  = MakeInternal();
            new_root->keys[0]     = key;
            new_root->children[0] = left;
            new_root->children[1] = right;
            new_root->num_keys    = 1;
            left->parent          = new_root;
            right->parent         = new_root;
            root_                 = new_root;
            return;
        }
        auto* parent = static_cast<InternalNode*>(left->parent);
        // Find position of `left` among children.
        int k = 0;
        while (k <= parent->num_keys && parent->children[k] != left) ++k;
        // Shift keys and children right to make room.
        for (int i = parent->num_keys; i > k; --i) {
            parent->keys[i]       = std::move(parent->keys[i - 1]);
            parent->children[i + 1] = parent->children[i];
        }
        parent->keys[k]       = key;
        parent->children[k + 1] = right;
        ++parent->num_keys;
        right->parent = parent;

        if (parent->num_keys == kOrder) SplitInternal(parent);
    }

    // -- Split internal node --
    void SplitInternal(InternalNode* node) {
        auto* right = MakeInternal();
        int   mid   = kOrder / 2;
        Key   push_up = std::move(node->keys[mid]);

        right->num_keys = node->num_keys - mid - 1;
        for (int i = 0; i < right->num_keys; ++i)
            right->keys[i] = std::move(node->keys[mid + 1 + i]);
        for (int i = 0; i <= right->num_keys; ++i) {
            right->children[i] = node->children[mid + 1 + i];
            if (right->children[i]) right->children[i]->parent = right;
        }
        node->num_keys = mid;

        InsertInParent(node, push_up, right);
        right->parent = node->parent;
    }

    // -- Serialisation helpers --
    static void SerialiseNode(const Node*          n,
                               std::vector<uint8_t>& buf,
                               const KeySerializer&  ks,
                               const ValSerializer&  vs) {
        if (!n) { buf.push_back('X'); return; }
        if (n->is_leaf) {
            buf.push_back('L');
            const auto* leaf = static_cast<const LeafNode*>(n);
            detail::WriteUint32(buf, static_cast<uint32_t>(leaf->num_keys));
            for (int i = 0; i < leaf->num_keys; ++i) {
                ks(buf, leaf->keys[i]);
                vs(buf, leaf->values[i]);
            }
        } else {
            buf.push_back('I');
            const auto* in = static_cast<const InternalNode*>(n);
            detail::WriteUint32(buf, static_cast<uint32_t>(in->num_keys));
            for (int i = 0; i < in->num_keys; ++i) ks(buf, in->keys[i]);
            for (int i = 0; i <= in->num_keys; ++i) SerialiseNode(in->children[i], buf, ks, vs);
        }
    }

    Node* DeserialiseNode(const uint8_t*& p, const uint8_t* end,
                          Node* parent,
                          const KeyDeserializer& kd,
                          const ValDeserializer& vd) {
        if (p >= end) throw std::runtime_error("BTree: unexpected end of data");
        uint8_t tag = *p++;
        if (tag == 'X') return nullptr;
        if (tag == 'L') {
            auto* leaf = MakeLeaf();
            leaf->parent   = parent;
            uint32_t nk    = detail::ReadUint32(p); p += 4;
            leaf->num_keys = static_cast<int>(nk);
            for (int i = 0; i < leaf->num_keys; ++i) {
                leaf->keys[i]   = kd(p, end);
                leaf->values[i] = vd(p, end);
                ++size_;
            }
            return leaf;
        }
        if (tag == 'I') {
            auto* in  = MakeInternal();
            in->parent   = parent;
            uint32_t nk  = detail::ReadUint32(p); p += 4;
            in->num_keys = static_cast<int>(nk);
            for (int i = 0; i < in->num_keys; ++i) in->keys[i] = kd(p, end);
            for (int i = 0; i <= in->num_keys; ++i) {
                in->children[i] = DeserialiseNode(p, end, in, kd, vd);
                if (in->children[i]) in->children[i]->parent = in;
            }
            return in;
        }
        throw std::runtime_error("BTree: unknown node tag");
    }

    void RebuildLeafLinks(Node* n) {
        // In-order DFS to rebuild prev/next links.
        std::vector<LeafNode*> leaves;
        CollectLeaves(n, leaves);
        for (std::size_t i = 0; i < leaves.size(); ++i) {
            leaves[i]->prev = (i > 0) ? leaves[i - 1] : nullptr;
            leaves[i]->next = (i + 1 < leaves.size()) ? leaves[i + 1] : nullptr;
        }
        first_leaf_ = leaves.empty() ? nullptr : leaves[0];
    }

    static void CollectLeaves(Node* n, std::vector<LeafNode*>& out) {
        if (!n) return;
        if (n->is_leaf) { out.push_back(static_cast<LeafNode*>(n)); return; }
        auto* in = static_cast<InternalNode*>(n);
        for (int i = 0; i <= in->num_keys; ++i) CollectLeaves(in->children[i], out);
    }
};

} // namespace xstd
