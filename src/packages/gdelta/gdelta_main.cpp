#include <iostream>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <string_view>
#include <vector>
#include <fstream>
#include <filesystem>
#include <optional>
#include <chrono>
#include <iomanip>

#include "gdelta.h"
#include "fsop.h"

namespace fs = std::filesystem;

static constexpr int Chunk = 512 << 10;
static constexpr size_t BufferSize = Chunk + 20 * 1024;
static constexpr const char* programName = "gdelta";

enum class Mode {
    Random,
    File
};

struct CommandLineArgs {
    Mode mode;
    std::optional<std::string> inputFile;
    std::optional<std::string> outputFile;
};

void printUsage() {
    std::cout << "Usage:\n";
    std::cout << "  " << programName << " --random              (Test with random data)\n";
    std::cout << "  " << programName << " --file <input-file> [output-file] (Delta encode a file)\n";
    std::cout << "\nExamples:\n";
    std::cout << "  " << programName << " --random\n";
    std::cout << "  " << programName << " --file input.bin\n";
    std::cout << "  " << programName << " --file input.bin delta.bin\n";
}

std::optional<CommandLineArgs> parseArgs(int argc, char** argv) {
    if (argc < 2) {
        printUsage();
        return std::nullopt;
    }

    std::string_view mode = argv[1];

    if (mode == "--random") {
        return CommandLineArgs{Mode::Random, std::nullopt, std::nullopt};
    } else if (mode == "--file") {
        if (argc < 3) {
            std::cerr << "Error: --file requires an input file path\n";
            printUsage();
            return std::nullopt;
        }
        std::optional<std::string> outputFile;
        if (argc >= 4) {
            outputFile = argv[3];
        }
        return CommandLineArgs{Mode::File, argv[2], outputFile};
    } else {
        std::cerr << "Error: Unknown mode '" << mode << "'\n";
        printUsage();
        return std::nullopt;
    }
}

void testRandomData() {
    std::cout << "=== Testing with Random Data ===\n";

    auto newBuffer = std::make_unique<uint8_t[]>(BufferSize);
    auto baseBuffer = std::make_unique<uint8_t[]>(BufferSize);
    auto deltaBuffer = std::make_unique<uint8_t[]>(BufferSize);
    auto outBuffer = std::make_unique<uint8_t[]>(BufferSize);

    // Generate random data
    for (int i = 0; i < Chunk; i++) {
        if (i % 488 != 0) {
            newBuffer[i] = baseBuffer[i] = static_cast<uint8_t>(rand() % 256);
        } else {
            newBuffer[i] = 2;
            baseBuffer[i] = 3;
        }
    }

    uint32_t dSize = 0;
    uint8_t* deltaBufPtr = deltaBuffer.get();

    auto start = std::chrono::high_resolution_clock::now();
    gencode(newBuffer.get(), Chunk,
            baseBuffer.get(), Chunk,
            &deltaBufPtr, &dSize);
    auto end = std::chrono::high_resolution_clock::now();

    std::cout << "Encoding time: " 
              << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() 
              << " us\n";

    uint32_t outSize = 0;
    uint8_t* outBufPtr = outBuffer.get();

    start = std::chrono::high_resolution_clock::now();
    gdecode(deltaBufPtr, dSize,
            baseBuffer.get(), Chunk,
            &outBufPtr, &outSize);
    end = std::chrono::high_resolution_clock::now();

    std::cout << "Decoding time: " 
              << std::chrono::duration_cast<std::chrono::microseconds>(end - start).count() 
              << " us\n";

    // Verify
    if (outSize != Chunk || std::memcmp(newBuffer.get(), outBufPtr, Chunk) != 0) {
        std::cerr << "ERROR: Decode verification failed!\n";
        return;
    }

    std::cout << "✓ Decode verification passed\n";
    std::cout << "Original size: " << Chunk << " bytes\n";
    std::cout << "Delta size: " << dSize << " bytes\n";
    std::cout << "Compression ratio: " << std::fixed << std::setprecision(2) 
              << (Chunk / static_cast<double>(dSize)) << "x\n";
}

void testFileData(std::string_view inputPath, std::optional<std::string_view> outputPath) {
    std::cout << "=== Testing with File Data ===\n";
    std::cout << "Input file: " << inputPath << "\n";

    // Read input file
    std::ifstream inputFile(inputPath.data(), std::ios::binary);
    if (!inputFile.is_open()) {
        std::cerr << "ERROR: Cannot open input file: " << inputPath << "\n";
        return;
    }

    // Get file size
    inputFile.seekg(0, std::ios::end);
    size_t fileSize = inputFile.tellg();
    inputFile.seekg(0, std::ios::beg);

    std::cout << "File size: " << fileSize << " bytes\n";

    if (fileSize > BufferSize / 2) {
        std::cerr << "ERROR: File too large (max " << BufferSize / 2 << " bytes)\n";
        return;
    }

    auto newBuffer = std::make_unique<uint8_t[]>(BufferSize);
    auto baseBuffer = std::make_unique<uint8_t[]>(BufferSize);
    auto deltaBuffer = std::make_unique<uint8_t[]>(BufferSize);

    // Read file into buffer
    inputFile.read(reinterpret_cast<char*>(newBuffer.get()), fileSize);
    inputFile.close();

    // Create base buffer (simple: shifted version)
    std::memcpy(baseBuffer.get(), newBuffer.get(), fileSize);
    for (size_t i = 0; i < fileSize; i++) {
        baseBuffer[i] = static_cast<uint8_t>((baseBuffer[i] + 1) % 256);
    }

    uint32_t dSize = 0;
    uint8_t* deltaBufPtr = deltaBuffer.get();

    auto start = std::chrono::high_resolution_clock::now();
    gencode(newBuffer.get(), static_cast<uint32_t>(fileSize),
            baseBuffer.get(), static_cast<uint32_t>(fileSize),
            &deltaBufPtr, &dSize);
    auto end = std::chrono::high_resolution_clock::now();

    std::cout << "Encoding time: " 
              << std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count() 
              << " ms\n";
    std::cout << "Delta size: " << dSize << " bytes\n";
    std::cout << "Compression ratio: " << std::fixed << std::setprecision(2) 
              << (fileSize / static_cast<double>(dSize)) << "x\n";

    // Write output if specified
    if (outputPath) {
        std::ofstream outFile(outputPath->data(), std::ios::binary);
        if (!outFile.is_open()) {
            std::cerr << "ERROR: Cannot open output file: " << *outputPath << "\n";
            return;
        }
        outFile.write(reinterpret_cast<const char*>(deltaBufPtr), dSize);
        outFile.close();
        std::cout << "✓ Delta written to: " << *outputPath << "\n";
    }
}

int main(int argc, char** argv) {
    srand(static_cast<unsigned int>(time(nullptr)));

    auto args = parseArgs(argc, argv);
    if (!args) {
        return 1;
    }

    try {
        switch (args->mode) {
            case Mode::Random:
                testRandomData();
                break;
            case Mode::File:
                if (!args->inputFile) {
                    std::cerr << "ERROR: Input file not specified\n";
                    return 1;
                }
                testFileData(*args->inputFile, args->outputFile);
                break;
        }
    } catch (const std::exception& e) {
        std::cerr << "ERROR: " << e.what() << "\n";
        return 1;
    }

    return 0;
}