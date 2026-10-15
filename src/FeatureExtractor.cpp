#include "FeatureExtractor.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

FeatureVector FeatureExtractor::extract(
    const unsigned char* data,
    std::size_t size)
{
    FeatureVector features{};
    if (!data || size == 0) {
        return features;
    }

    std::array<std::uint64_t, 256> frequencies{};
    std::uint64_t zeroCount = 0;
    std::uint64_t ffCount = 0;
    std::uint64_t printableCount = 0;
    unsigned int minimumByte = 255;
    unsigned int maximumByte = 0;
    long double runningMean = 0.0L;
    long double sumSquaredDifferences = 0.0L;

    for (std::size_t i = 0; i < size; ++i) {
        const unsigned int value = data[i];
        ++frequencies[value];

        if (value == 0)
            ++zeroCount;
        if (value == 0xFF)
            ++ffCount;
        if (value >= 0x20 && value <= 0x7E)
            ++printableCount;

        minimumByte = std::min(minimumByte, value);
        maximumByte = std::max(maximumByte, value);

        const long double delta = static_cast<long double>(value) - runningMean;
        runningMean += delta / static_cast<long double>(i + 1);
        const long double deltaAfterMean =
            static_cast<long double>(value) - runningMean;
        sumSquaredDifferences += delta * deltaAfterMean;
    }

    const long double total = static_cast<long double>(size);
    const long double expectedFrequency = total / 256.0L;
    long double entropy = 0.0L;
    long double chiSquare = 0.0L;
    std::uint64_t uniqueCount = 0;
    std::uint64_t dominantCount = 0;

    for (const std::uint64_t frequency : frequencies) {
        if (frequency == 0)
            continue;

        ++uniqueCount;
        dominantCount = std::max(dominantCount, frequency);

        const long double probability =
            static_cast<long double>(frequency) / total;
        entropy -= probability * std::log2(probability);

        const long double difference =
            static_cast<long double>(frequency) - expectedFrequency;
        chiSquare += difference * difference / expectedFrequency;
    }

    features.entropy = static_cast<double>(entropy);
    features.meanByteValue = static_cast<double>(runningMean);
    features.standardDeviation = static_cast<double>(
        std::sqrt(sumSquaredDifferences / total));
    features.zeroBytePercentage = static_cast<double>(zeroCount * 100.0L / total);
    features.ffBytePercentage = static_cast<double>(ffCount * 100.0L / total);
    features.printableBytePercentage =
        static_cast<double>(printableCount * 100.0L / total);
    features.uniqueByteRatio = static_cast<double>(uniqueCount) / 256.0;
    features.dominantBytePercentage =
        static_cast<double>(dominantCount * 100.0L / total);
    features.byteRange = static_cast<double>(maximumByte - minimumByte);
    features.chiSquare = static_cast<double>(chiSquare);

    return features;
}

FeatureVector FeatureExtractor::extract(
    const std::vector<unsigned char>& data)
{
    return extract(data.data(), data.size());
}

double FeatureExtractor::calculateEntropy(
    const std::vector<unsigned char>& data)
{
    return extract(data).entropy;
}