#pragma once

#include <cstddef>
#include <vector>

struct FeatureVector {
    double entropy = 0.0;
    double meanByteValue = 0.0;
    double standardDeviation = 0.0;
    double zeroBytePercentage = 0.0;
    double ffBytePercentage = 0.0;
    double printableBytePercentage = 0.0;
    double uniqueByteRatio = 0.0;
    double dominantBytePercentage = 0.0;
    double byteRange = 0.0;
    double chiSquare = 0.0;
};

class FeatureExtractor
{
public:
    // Unique-byte ratio is distinct byte values divided by the 256-value alphabet.
    // Printable bytes are ASCII 0x20 through 0x7E, inclusive.
    static FeatureVector extract(
        const unsigned char* data,
        std::size_t size
    );

    static FeatureVector extract(
        const std::vector<unsigned char>& data
    );

    // Shannon entropy, in bits per byte; retained for existing callers.
    static double calculateEntropy(
        const std::vector<unsigned char>& data
    );
};