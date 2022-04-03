#pragma once

#include "ExplanationMethod.h"

class ValueMethod : public Explanation::Method
{
public:
    void recompute(const DataMatrix& dataset, NeighbourhoodMatrix& neighbourhoodMatrix) override;
    float computeDimensionRank(const DataMatrix& dataset, int i, int j) override;

private:
    void precomputeGlobalValues(const DataMatrix& dataset);
    void precomputeLocalValues(const DataMatrix& dataset, NeighbourhoodMatrix& neighbourhoodMatrix);

    std::vector<float> _globalValues;

    DataMatrix _localValues;
};