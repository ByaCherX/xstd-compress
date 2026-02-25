
#include <hasher.h>

#define XXH_INLINE_ALL
#include <packages/xxhash/xxhash.h>

#include <string>
#include <stdexcept>

namespace msp {

template <typename T>
uint64_t XXHashHelper(T value, uint32_t seed = XXHasher::kBloomHashSeed) {
    return XXH64(reinterpret_cast<const void*>(&value), sizeof(T), seed);
}

template <typename T>
void XXHashesHelper(const T* values, uint32_t seed, int num_values, uint64_t* hashes) {
    for (int i = 0; i < num_values; ++i) {
        hashes[i] = XXHashHelper(values[i], seed);
    }
}

//XXHasher::XXHasher() : chunk_size_(kDefaultChunkSize) {}

uint64_t XXHasher::Hash(int32_t value) const {
    return XXHashHelper(value, kBloomHashSeed);
}

uint64_t XXHasher::Hash(int64_t value) const {
    return XXHashHelper(value, kBloomHashSeed);
}

uint64_t XXHasher::Hash(float value) const {
    return XXHashHelper(value, kBloomHashSeed);
}

uint64_t XXHasher::Hash(double value) const {
    return XXHashHelper(value, kBloomHashSeed);
}

void XXHasher::Hashes(const int32_t* values, int num_values, uint64_t* hashes) const {
    XXHashesHelper(values, kBloomHashSeed, num_values, hashes);
}

void XXHasher::Hashes(const int64_t* values, int num_values, uint64_t* hashes) const {
    XXHashesHelper(values, kBloomHashSeed, num_values, hashes);
}

void XXHasher::SetChunkSize(uint32_t chunk_size) {
    if (chunk_size < kMinChunkSize || chunk_size > kMaxChunkSize) {
        throw std::invalid_argument(
            "Chunk size must be between " + std::to_string(kMinChunkSize) + 
            " and " + std::to_string(kMaxChunkSize) + " bytes"
        );
    }
    chunk_size_ = chunk_size;
}

uint32_t XXHasher::GetChunkSize() const {
    return chunk_size_;
}

void XXHasher::Hashes(const void* values, size_t length, uint64_t* hash) const {
    if (values == nullptr || hash == nullptr || length <= 0) {
        return;
    }

    const uint8_t* data = reinterpret_cast<const uint8_t*>(values);
    uint32_t total_size = static_cast<uint32_t>(length);

    // Create hash state for streaming hash computation
    XXH64_state_t* state = XXH64_createState();
    if (state == nullptr) {
        return;
    }

    // Reset state with seed
    XXH64_reset(state, kBloomHashSeed);

    // Process data in chunks using streaming API
    for (uint32_t offset = 0; offset < total_size; offset += chunk_size_) {
        uint32_t current_chunk_size = std::min(chunk_size_, total_size - offset);
        
        // Update hash state with current chunk
        XXH64_update(state, reinterpret_cast<const void*>(&data[offset]), current_chunk_size);
    }

    // Get final hash digest
    *hash = reinterpret_cast<uint64_t>(XXH64_digest(state));

    // Free the hash state
    XXH64_freeState(state);
}

} // namespace msp