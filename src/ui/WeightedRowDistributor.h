#pragma once

#include <juce_graphics/juce_graphics.h>

namespace coolsynth::ui
{
    // Distributes a row's width among N weighted sections separated by fixed gaps.
    // The last section absorbs rounding residuals, so the produced rectangles always
    // sum to exactly the row width - no leftover slack on the right edge.
    //
    // This is the primitive that lets a layout scale continuously: change the row
    // width and every section grows or shrinks in proportion.
    struct WeightedRowDistributor
    {
        WeightedRowDistributor(juce::Rectangle<int>& rowRef,
                               float totalWeight,
                               int sectionCount,
                               int gapPx) noexcept
            : row(rowRef)
            , remainingWidth(juce::jmax(0, rowRef.getWidth() - juce::jmax(0, sectionCount - 1) * gapPx))
            , remainingWeight(totalWeight)
            , gap(gapPx)
            , sectionsLeft(sectionCount)
        {
        }

        juce::Rectangle<int> take(float weight) noexcept
        {
            const int width = (sectionsLeft <= 1 || remainingWeight <= 0.0f)
                ? remainingWidth
                : juce::roundToInt(static_cast<float>(remainingWidth) * (weight / remainingWeight));

            remainingWidth -= width;
            remainingWeight -= weight;
            auto rect = row.removeFromLeft(width);
            if (--sectionsLeft > 0)
                row.removeFromLeft(gap);
            return rect;
        }

    private:
        juce::Rectangle<int>& row;
        int   remainingWidth;
        float remainingWeight;
        int   gap;
        int   sectionsLeft;
    };
}
