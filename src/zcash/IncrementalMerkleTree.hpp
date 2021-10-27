#pragma once
#include <array>
#include <deque>
#include <optional>

#include "uint256.h"
#include "serialize.h"

#include "Zcash.h"
#include "zcash/util.h"
#include "vector_types.h"

namespace libzcash {

class MerklePath {
public:
    std::vector<v_bools> authentication_path;
    v_bools index;

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        std::vector<v_uint8> pathBytes;
        uint64_t indexInt;
        if (ser_action == SERIALIZE_ACTION::Read)
        {
            READWRITE(pathBytes);
            READWRITE(indexInt);
            MerklePath &us = *(const_cast<MerklePath*>(this));
            for (size_t i = 0; i < pathBytes.size(); i++) {
                us.authentication_path.push_back(convertBytesVectorToVector(pathBytes[i]));
                us.index.push_back((indexInt >> ((pathBytes.size() - 1) - i)) & 1);
            }
        } else {
            assert(authentication_path.size() == index.size());
            pathBytes.resize(authentication_path.size());
            for (size_t i = 0; i < authentication_path.size(); i++) {
                pathBytes[i].resize((authentication_path[i].size()+7)/8);
                for (unsigned int p = 0; p < authentication_path[i].size(); p++) {
                    pathBytes[i][p / 8] |= static_cast<uint8_t>(authentication_path[i][p]) << (7-(p % 8));
                }
            }
            indexInt = convertVectorToInt(index);
            READWRITE(pathBytes);
            READWRITE(indexInt);
        }
    }

    bool operator==(const MerklePath &rhs) const
    {
        return (index == rhs.index) && (authentication_path == rhs.authentication_path);
    }

    MerklePath() { }

    MerklePath(std::vector<v_bools> authentication_path, v_bools index)
    : authentication_path(authentication_path), index(index) { }
};

template<size_t Depth, typename Hash>
class EmptyMerkleRoots {
public:
    EmptyMerkleRoots() {
        empty_roots.at(0) = Hash::uncommitted();
        for (size_t d = 1; d <= Depth; d++) {
            empty_roots.at(d) = Hash::combine(empty_roots.at(d-1), empty_roots.at(d-1), d-1);
        }
    }
    Hash empty_root(size_t depth) {
        return empty_roots.at(depth);
    }
    template <size_t D, typename H>
    friend bool operator==(const EmptyMerkleRoots<D, H>& a,
                           const EmptyMerkleRoots<D, H>& b);
private:
    std::array<Hash, Depth+1> empty_roots;
};

template<size_t Depth, typename Hash>
bool operator==(const EmptyMerkleRoots<Depth, Hash>& a,
                const EmptyMerkleRoots<Depth, Hash>& b) {
    return a.empty_roots == b.empty_roots;
}

template<size_t Depth, typename Hash>
class IncrementalWitness;

template<size_t Depth, typename Hash>
class IncrementalMerkleTree {

friend class IncrementalWitness<Depth, Hash>;

public:
    static_assert(Depth >= 1);

    IncrementalMerkleTree() { }

    size_t DynamicMemoryUsage() const {
        return 32 + // left
               32 + // right
               parents.size() * 32; // parents
    }

    size_t size() const;

    void append(Hash obj);
    Hash root() const {
        return root(Depth, std::deque<Hash>());
    }
    Hash last() const;

    IncrementalWitness<Depth, Hash> witness() const {
        return IncrementalWitness<Depth, Hash>(*this);
    }

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(left);
        READWRITE(right);
        READWRITE(parents);

        wfcheck();
    }

    static Hash empty_root() {
        return emptyroots.empty_root(Depth);
    }

    template <size_t D, typename H>
    friend bool operator==(const IncrementalMerkleTree<D, H>& a,
                           const IncrementalMerkleTree<D, H>& b);

private:
    static EmptyMerkleRoots<Depth, Hash> emptyroots;
    std::optional<Hash> left;
    std::optional<Hash> right;

    // Collapsed "left" subtrees ordered toward the root of the tree.
    std::vector<std::optional<Hash>> parents;
    MerklePath path(std::deque<Hash> filler_hashes = std::deque<Hash>()) const;
    Hash root(size_t depth, std::deque<Hash> filler_hashes = std::deque<Hash>()) const;
    bool is_complete(size_t depth = Depth) const;
    size_t next_depth(size_t skip) const;
    void wfcheck() const;
};

template<size_t Depth, typename Hash>
bool operator==(const IncrementalMerkleTree<Depth, Hash>& a,
                const IncrementalMerkleTree<Depth, Hash>& b) {
    return (a.emptyroots == b.emptyroots &&
            a.left == b.left &&
            a.right == b.right &&
            a.parents == b.parents);
}

template <size_t Depth, typename Hash>
class IncrementalWitness {
friend class IncrementalMerkleTree<Depth, Hash>;

public:
    // Required for Unserialize()
    IncrementalWitness() {}

    MerklePath path() const {
        return tree.path(partial_path());
    }

    // Return the element being witnessed (should be a note
    // commitment!)
    Hash element() const {
        return tree.last();
    }

    uint64_t position() const {
        return tree.size() - 1;
    }

    Hash root() const {
        return tree.root(Depth, partial_path());
    }

    void append(Hash obj);

    ADD_SERIALIZE_METHODS;

    template <typename Stream>
    inline void SerializationOp(Stream& s, const SERIALIZE_ACTION ser_action)
    {
        READWRITE(tree);
        READWRITE(filled);
        READWRITE(cursor);

        cursor_depth = tree.next_depth(filled.size());
    }

    template <size_t D, typename H>
    friend bool operator==(const IncrementalWitness<D, H>& a,
                           const IncrementalWitness<D, H>& b);

private:
    IncrementalMerkleTree<Depth, Hash> tree;
    std::vector<Hash> filled;
    std::optional<IncrementalMerkleTree<Depth, Hash>> cursor;
    size_t cursor_depth = 0;
    std::deque<Hash> partial_path() const;
    IncrementalWitness(IncrementalMerkleTree<Depth, Hash> tree) : tree(tree) {}
};

template<size_t Depth, typename Hash>
bool operator==(const IncrementalWitness<Depth, Hash>& a,
                const IncrementalWitness<Depth, Hash>& b) {
    return (a.tree == b.tree &&
            a.filled == b.filled &&
            a.cursor == b.cursor &&
            a.cursor_depth == b.cursor_depth);
}

class SHA256Compress : public uint256 {
public:
    SHA256Compress() : uint256() {}
    SHA256Compress(uint256 contents) : uint256(contents) { }

    static SHA256Compress combine(
        const SHA256Compress& a,
        const SHA256Compress& b,
        size_t depth
    );

    static SHA256Compress uncommitted() {
        return SHA256Compress();
    }
};

class PedersenHash : public uint256 {
public:
    PedersenHash() : uint256() {}
    PedersenHash(uint256 contents) : uint256(contents) { }

    static PedersenHash combine(
        const PedersenHash& a,
        const PedersenHash& b,
        size_t depth
    );

    static PedersenHash uncommitted();
};

template<size_t Depth, typename Hash>
EmptyMerkleRoots<Depth, Hash> IncrementalMerkleTree<Depth, Hash>::emptyroots;

} // end namespace `libzcash`

typedef libzcash::IncrementalMerkleTree<INCREMENTAL_MERKLE_TREE_DEPTH, libzcash::SHA256Compress> SproutMerkleTree;
typedef libzcash::IncrementalMerkleTree<INCREMENTAL_MERKLE_TREE_DEPTH_TESTING, libzcash::SHA256Compress> SproutTestingMerkleTree;

typedef libzcash::IncrementalWitness<INCREMENTAL_MERKLE_TREE_DEPTH, libzcash::SHA256Compress> SproutWitness;
typedef libzcash::IncrementalWitness<INCREMENTAL_MERKLE_TREE_DEPTH_TESTING, libzcash::SHA256Compress> SproutTestingWitness;

typedef libzcash::IncrementalMerkleTree<SAPLING_INCREMENTAL_MERKLE_TREE_DEPTH, libzcash::PedersenHash> SaplingMerkleTree;
typedef libzcash::IncrementalMerkleTree<INCREMENTAL_MERKLE_TREE_DEPTH_TESTING, libzcash::PedersenHash> SaplingTestingMerkleTree;

typedef libzcash::IncrementalWitness<SAPLING_INCREMENTAL_MERKLE_TREE_DEPTH, libzcash::PedersenHash> SaplingWitness;
typedef libzcash::IncrementalWitness<INCREMENTAL_MERKLE_TREE_DEPTH_TESTING, libzcash::PedersenHash> SaplingTestingWitness;
