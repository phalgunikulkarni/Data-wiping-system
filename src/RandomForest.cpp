#include "RandomForest.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>

RandomForest::RandomForest(
    int numberOfTrees,
    int maxDepth,
    int minSamplesSplit
)
    : numberOfTrees(numberOfTrees),
      maxDepth(maxDepth),
      minSamplesSplit(minSamplesSplit),
      rng(std::random_device{}())
{
}

void RandomForest::deleteTree(TreeNode* node)
{
    if (!node)
        return;

    deleteTree(node->left);
    deleteTree(node->right);

    delete node;
}

RandomForest::TreeNode* RandomForest::buildTree(
    const std::vector<std::vector<double>>& X,
    const std::vector<double>& y,
    int depth
)
{
    auto* node = new TreeNode();

    if (X.empty() || y.empty()) {
        node->prediction = 0.0;
        return node;
    }

    double mean = 0.0;

    for (double value : y)
        mean += value;

    mean /= static_cast<double>(y.size());

    node->prediction = mean;

    if (depth >= maxDepth ||
        static_cast<int>(X.size()) < minSamplesSplit ||
        X.size() < 2) {
        return node;
    }

    const std::size_t featureCount = X[0].size();

    if (featureCount == 0)
        return node;

    double bestError = std::numeric_limits<double>::max();
    int bestFeature = -1;
    double bestThreshold = 0.0;

    for (std::size_t feature = 0;
         feature < featureCount;
         ++feature) {

        std::vector<double> values;

        for (const auto& row : X) {
            if (feature < row.size())
                values.push_back(row[feature]);
        }

        if (values.size() < 2)
            continue;

        std::sort(values.begin(), values.end());

        values.erase(
            std::unique(values.begin(), values.end()),
            values.end()
        );

        for (std::size_t i = 0; i + 1 < values.size(); ++i) {

            double threshold =
                (values[i] + values[i + 1]) / 2.0;

            std::vector<double> leftY;
            std::vector<double> rightY;

            for (std::size_t j = 0; j < X.size(); ++j) {

                if (feature >= X[j].size())
                    continue;

                if (X[j][feature] <= threshold)
                    leftY.push_back(y[j]);
                else
                    rightY.push_back(y[j]);
            }

            if (leftY.empty() || rightY.empty())
                continue;

            double leftMean = 0.0;
            double rightMean = 0.0;

            for (double value : leftY)
                leftMean += value;

            for (double value : rightY)
                rightMean += value;

            leftMean /= leftY.size();
            rightMean /= rightY.size();

            double error = 0.0;

            for (double value : leftY)
                error += (value - leftMean) *
                         (value - leftMean);

            for (double value : rightY)
                error += (value - rightMean) *
                         (value - rightMean);

            if (error < bestError) {
                bestError = error;
                bestFeature = static_cast<int>(feature);
                bestThreshold = threshold;
            }
        }
    }

    if (bestFeature == -1)
        return node;

    std::vector<std::vector<double>> leftX;
    std::vector<std::vector<double>> rightX;

    std::vector<double> leftY;
    std::vector<double> rightY;

    for (std::size_t i = 0; i < X.size(); ++i) {

        if (bestFeature >= static_cast<int>(X[i].size()))
            continue;

        if (X[i][bestFeature] <= bestThreshold) {
            leftX.push_back(X[i]);
            leftY.push_back(y[i]);
        }
        else {
            rightX.push_back(X[i]);
            rightY.push_back(y[i]);
        }
    }

    if (leftX.empty() || rightX.empty())
        return node;

    node->isLeaf = false;
    node->featureIndex = bestFeature;
    node->threshold = bestThreshold;

    node->left = buildTree(leftX, leftY, depth + 1);
    node->right = buildTree(rightX, rightY, depth + 1);

    return node;
}

void RandomForest::train(
    const std::vector<std::vector<double>>& X,
    const std::vector<double>& y
)
{
    for (TreeNode* tree : trees)
        deleteTree(tree);

    trees.clear();
    trained = false;

    if (X.empty() ||
        y.empty() ||
        X.size() != y.size()) {
        return;
    }

    for (int treeIndex = 0;
         treeIndex < numberOfTrees;
         ++treeIndex) {

        std::vector<std::vector<double>> sampleX;
        std::vector<double> sampleY;

        std::uniform_int_distribution<std::size_t> distribution(
            0,
            X.size() - 1
        );

        for (std::size_t i = 0; i < X.size(); ++i) {

            std::size_t index = distribution(rng);

            sampleX.push_back(X[index]);
            sampleY.push_back(y[index]);
        }

        TreeNode* tree =
            buildTree(sampleX, sampleY, 0);

        trees.push_back(tree);
    }

    trained = !trees.empty();
}

double RandomForest::predict(
    const std::vector<double>& features
) const
{
    if (!trained || trees.empty())
        return 0.0;

    double total = 0.0;

    for (const TreeNode* tree : trees)
        total += predictTree(tree, features);

    return total /
           static_cast<double>(trees.size());
}

double RandomForest::predictTree(
    const TreeNode* node,
    const std::vector<double>& features
) const
{
    if (!node)
        return 0.0;

    if (node->isLeaf)
        return node->prediction;

    if (node->featureIndex < 0 ||
        node->featureIndex >=
            static_cast<int>(features.size())) {
        return node->prediction;
    }

    if (features[node->featureIndex] <= node->threshold)
        return predictTree(node->left, features);

    return predictTree(node->right, features);
}

bool RandomForest::isTrained() const
{
    return trained;
}