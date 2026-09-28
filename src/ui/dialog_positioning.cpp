#include "dialog_positioning.h"

#include <algorithm>

namespace {

// Clamps one axis so [coord, coord + extent) stays inside [workStart, workEnd).
// When the extent does not fit at all the window sticks to workStart.
int ClampAxis(int coord, int extent, int workStart, int workEnd) {
    const int maximum = (std::max)(workStart, workEnd - extent);
    return std::clamp(coord, workStart, maximum);
}

}  // namespace

POINT ComputeAnchoredDialogPos(const RECT& anchor, const RECT& workArea,
                               int width, int height, int gap) {
    // RECT members are LONG; carry them as int so the geometry below stays in
    // one integer type on every compiler.
    const int anchorLeft = static_cast<int>(anchor.left);
    const int anchorTop = static_cast<int>(anchor.top);
    const int anchorRight = static_cast<int>(anchor.right);
    const int anchorBottom = static_cast<int>(anchor.bottom);
    const int workLeft = static_cast<int>(workArea.left);
    const int workTop = static_cast<int>(workArea.top);
    const int workRight = static_cast<int>(workArea.right);
    const int workBottom = static_cast<int>(workArea.bottom);

    const int centeredY = anchorTop + ((anchorBottom - anchorTop) - height) / 2;

    const bool rightFits = anchorRight + gap + width <= workRight;
    const bool leftFits = anchorLeft - gap - width >= workLeft;
    const bool belowFits = anchorBottom + gap + height <= workBottom;
    const bool aboveFits = anchorTop - gap - height >= workTop;

    int x = 0;
    int y = 0;
    if (rightFits) {
        x = anchorRight + gap;
        y = centeredY;
    } else if (leftFits) {
        x = anchorLeft - gap - width;
        y = centeredY;
    } else if (belowFits) {
        x = anchorLeft;
        y = anchorBottom + gap;
    } else if (aboveFits) {
        x = anchorLeft;
        y = anchorTop - gap - height;
    } else {
        // No side fits completely: take the one with the most remaining space.
        // The right side starts as best, so it wins ties.
        int bestSpace = workRight - (anchorRight + gap);
        x = anchorRight + gap;
        y = centeredY;

        const int leftSpace = (anchorLeft - gap) - workLeft;
        if (leftSpace > bestSpace) {
            bestSpace = leftSpace;
            x = anchorLeft - gap - width;
            y = centeredY;
        }
        const int belowSpace = workBottom - (anchorBottom + gap);
        if (belowSpace > bestSpace) {
            bestSpace = belowSpace;
            x = anchorLeft;
            y = anchorBottom + gap;
        }
        const int aboveSpace = (anchorTop - gap) - workTop;
        if (aboveSpace > bestSpace) {
            bestSpace = aboveSpace;
            x = anchorLeft;
            y = anchorTop - gap - height;
        }
    }

    POINT pos = {};
    pos.x = ClampAxis(x, width, workLeft, workRight);
    pos.y = ClampAxis(y, height, workTop, workBottom);
    return pos;
}

int ComputeAnchorShiftForSideBySide(const RECT& anchor, const RECT& workArea,
                                    int dialogWidth, int gap) {
    const int anchorLeft = static_cast<int>(anchor.left);
    const int anchorW = static_cast<int>(anchor.right - anchor.left);
    const int workLeft = static_cast<int>(workArea.left);
    const int workRight = static_cast<int>(workArea.right);

    if (anchorW + gap + dialogWidth > workRight - workLeft) {
        return anchorLeft;
    }
    const int latest = workRight - gap - dialogWidth - anchorW;
    return (std::min)(anchorLeft, (std::max)(workLeft, latest));
}
