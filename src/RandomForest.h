#pragma once

#include <vector>
#include <random>

class RandomForest
{
public:
    RandomForest(
        int numberOfTrees = 50,
        int maxDepth = 6,
        int minSamplesSplit = 2
    );

    void train(
        const std::vector<std::vector<double>>& X,
        const std::vector<double>& y
    );

    double predict(
        const std::vector<double>& features
    ) const;

    bool isTrained() const;

private:
    struct TreeNode {
        bool isLeaf = true;
        double prediction = 0.0;

        int featureIndex = -1;
        double threshold = 0.0;

        TreeNode* left = nullptr;
        TreeNode* right = nullptr;
    };

    TreeNode* buildTree(
        const std::vector<std::vector<double>>& X,
        const std::vector<double>& y,
        int depth
    );

    double predictTree(
        const TreeNode* node,
        const std::vector<double>& features
    ) const;

    void deleteTree(TreeNode* node);

    int numberOfTrees;
    int maxDepth;
    int minSamplesSplit;

    std::vector<TreeNode*> trees;

    bool trained = false;

    mutable std::mt19937 rng;
};