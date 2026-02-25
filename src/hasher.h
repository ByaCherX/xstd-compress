
#pragma once

#include <cstdint>

namespace msp {
// Abstract class for hash
class Hasher {
 public:
    virtual uint64_t Hash(int32_t value) const = 0;
    virtual uint64_t Hash(int64_t value) const = 0;
    virtual uint64_t Hash(float value) const  = 0;
    virtual uint64_t Hash(double value) const = 0;
    /// Batch compute hashes for int32_t and int64_t values, 
    /// which is more efficient than calling Hash() for each value.
    virtual void Hashes(const int32_t* values, int num_values, uint64_t* hashes) const = 0;
    virtual void Hashes(const int64_t* values, int num_values, uint64_t* hashes) const = 0;

    
    virtual void Hashes(const void* values, size_t length, uint64_t* hash) const = 0;

    virtual ~Hasher() = default;
};

class XXHasher : public Hasher {
 public:
    uint64_t Hash(int32_t value) const override;
    uint64_t Hash(int64_t value) const override;
    uint64_t Hash(float value) const override;
    uint64_t Hash(double value) const override;
    void Hashes(const int32_t* values, int num_values, uint64_t* hashes) const override;
    void Hashes(const int64_t* values, int num_values, uint64_t* hashes) const override;
    
    /// @brief Compute hash for a data block using streaming API.
    /// The data block is divided into chunks of configurable size (default 512KB).
    /// All chunks are processed through a single hash state for streaming computation.
    ///
    /// @param values pointer to the data block to hash.
    /// @param length total size of the data block in bytes.
    /// @param hash pointer to a single uint64_t where the computed hash will be stored.
    void Hashes(const void* values, size_t length, uint64_t* hash) const override;

    /// @brief Set the chunk size for data block hashing (in bytes).
    /// @param chunk_size size of each chunk (must be between 1KB and 128MB).
    /// Default is 512KB (524288 bytes).
    void SetChunkSize(uint32_t chunk_size);

    /// @brief Get the current chunk size.
    uint32_t GetChunkSize() const;

    static constexpr int kBloomHashSeed = 0;
    static constexpr uint32_t kDefaultChunkSize = 512 << 10;
    static constexpr uint32_t kMinChunkSize     = 1   << 10;
    static constexpr uint32_t kMaxChunkSize     = 128 << 20;

 private:
    uint32_t chunk_size_ = kDefaultChunkSize;
};
} // namespace msp