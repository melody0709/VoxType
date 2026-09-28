// Unit tests for src/ui/dialog_positioning.cpp: secondary Settings dialogs
// anchor to the side of the Settings window instead of covering it.
//
// Uses an explicit VT_CHECK macro rather than assert(): the project builds
// with CMAKE_BUILD_TYPE=Release, where NDEBUG turns assert() into a no-op.

#include "dialog_positioning.h"

#include <iostream>

namespace {

int g_failures = 0;

void Check(bool condition, const char* expression, const char* caseName) {
    if (!condition) {
        ++g_failures;
        std::cout << "FAIL [" << caseName << "] " << expression << "\n";
    }
}

#define VT_CHECK(caseName, expr) Check((expr), #expr, caseName)

RECT MakeRect(int left, int top, int right, int bottom) {
    return RECT{ left, top, right, bottom };
}

RECT DialogRect(const POINT& pos, int width, int height) {
    return MakeRect(pos.x, pos.y, pos.x + width, pos.y + height);
}

bool Overlaps(const RECT& a, const RECT& b) {
    return a.left < b.right && a.right > b.left && a.top < b.bottom && a.bottom > b.top;
}

constexpr int kGap = 12;

// Geometry behind the reported bug: 1920x1040 work area with the 850x740
// Settings window centred on it (1920x1080 @ 150%).
const RECT kWork1920 = MakeRect(0, 0, 1920, 1040);
const RECT kAnchorCentred = MakeRect(535, 150, 1385, 890);

// The 2560x1400 work area from the same report at 150% scale.
const RECT kWork2560 = MakeRect(0, 0, 2560, 1400);
const RECT kAnchorWide = MakeRect(855, 330, 1705, 1070);

void TestRightSidePreferred() {
    const char* name = "right-side-preferred";
    // Qwen dialog 780x900: 1705 + 12 + 780 = 2497 fits inside 2560.
    const POINT pos = ComputeAnchoredDialogPos(kAnchorWide, kWork2560, 780, 900, kGap);
    VT_CHECK(name, pos.x == 1705 + kGap);
    VT_CHECK(name, pos.y == 250);  // 330 + (740 - 900) / 2
    VT_CHECK(name, !Overlaps(DialogRect(pos, 780, 900), kAnchorWide));
}

void TestLeftSideFallback() {
    const char* name = "left-side-fallback";
    // Right side would end at 1612 > 1600; the left side has 838 px of room.
    const RECT work = MakeRect(0, 0, 1600, 1040);
    const RECT anchor = MakeRect(850, 150, 1250, 890);
    const POINT pos = ComputeAnchoredDialogPos(anchor, work, 350, 300, kGap);
    VT_CHECK(name, pos.x == 850 - kGap - 350);
    VT_CHECK(name, pos.y == 370);  // 150 + (740 - 300) / 2
    VT_CHECK(name, !Overlaps(DialogRect(pos, 350, 300), anchor));
}

void TestBelowFallback() {
    const char* name = "below-fallback";
    const RECT work = MakeRect(0, 0, 1300, 1040);
    const RECT anchor = MakeRect(400, 100, 900, 500);
    const POINT pos = ComputeAnchoredDialogPos(anchor, work, 800, 300, kGap);
    VT_CHECK(name, pos.x == 400);
    VT_CHECK(name, pos.y == 500 + kGap);
    VT_CHECK(name, !Overlaps(DialogRect(pos, 800, 300), anchor));
}

void TestAboveFallback() {
    const char* name = "above-fallback";
    const RECT work = MakeRect(0, 0, 1300, 1040);
    const RECT anchor = MakeRect(400, 700, 900, 1030);
    const POINT pos = ComputeAnchoredDialogPos(anchor, work, 800, 300, kGap);
    VT_CHECK(name, pos.x == 400);
    VT_CHECK(name, pos.y == 700 - kGap - 300);
    VT_CHECK(name, !Overlaps(DialogRect(pos, 800, 300), anchor));
}

void TestLargestSpaceWhenNothingFits() {
    const char* name = "largest-space";
    // 1920 @ 150%: the Qwen dialog fits on no side of the centred Settings
    // window; the right side has the most room, so the dialog clamps to the
    // right edge of the work area and still overlaps the anchor.
    const POINT pos = ComputeAnchoredDialogPos(kAnchorCentred, kWork1920, 780, 900, kGap);
    VT_CHECK(name, pos.x == 1920 - 780);
    VT_CHECK(name, pos.y == 70);  // 150 + (740 - 900) / 2
    VT_CHECK(name, Overlaps(DialogRect(pos, 780, 900), kAnchorCentred));
    const RECT dlg = DialogRect(pos, 780, 900);
    VT_CHECK(name, dlg.left >= kWork1920.left && dlg.right <= kWork1920.right);
    VT_CHECK(name, dlg.top >= kWork1920.top && dlg.bottom <= kWork1920.bottom);
}

void TestDialogLargerThanWorkArea() {
    const char* name = "oversized-dialog";
    const RECT work = MakeRect(0, 0, 800, 600);
    const RECT anchor = MakeRect(10, 10, 400, 300);
    const POINT pos = ComputeAnchoredDialogPos(anchor, work, 1000, 900, kGap);
    VT_CHECK(name, pos.x == 0);
    VT_CHECK(name, pos.y == 0);
}

void TestAnchorShiftMinimum() {
    const char* name = "anchor-shift-minimum";
    // 850 + 12 + 780 = 1642 <= 1920, so the Settings window only needs to
    // move to x = 278 for the Qwen dialog to fit beside it.
    const int shifted = ComputeAnchorShiftForSideBySide(kAnchorCentred, kWork1920, 780, kGap);
    VT_CHECK(name, shifted == 1920 - kGap - 780 - 850);
    const RECT shiftedAnchor = MakeRect(shifted, 150, shifted + 850, 890);
    const POINT pos = ComputeAnchoredDialogPos(shiftedAnchor, kWork1920, 780, 900, kGap);
    VT_CHECK(name, pos.x == shifted + 850 + kGap);
    VT_CHECK(name, !Overlaps(DialogRect(pos, 780, 900), shiftedAnchor));
}

void TestAnchorShiftImpossible() {
    const char* name = "anchor-shift-impossible";
    const RECT work = MakeRect(0, 0, 1366, 768);
    const RECT anchor = MakeRect(258, 14, 1108, 754);
    const int shifted = ComputeAnchorShiftForSideBySide(anchor, work, 780, kGap);
    VT_CHECK(name, shifted == 258);  // 850 + 12 + 780 does not fit: stay put
}

void TestAnchorShiftNoopWhenAlreadyClear() {
    const char* name = "anchor-shift-noop";
    const RECT anchor = MakeRect(40, 150, 890, 890);
    const int shifted = ComputeAnchorShiftForSideBySide(anchor, kWork1920, 780, kGap);
    VT_CHECK(name, shifted == 40);  // already clear on the right: no movement
}

void TestFittingSidesNeverOverlap() {
    const char* name = "fitting-sides-clear-anchor";
    // The four fitting cases must all clear the anchor: that is what keeps the
    // anchor-shift path from running when space is available.
    VT_CHECK(name, !Overlaps(DialogRect(ComputeAnchoredDialogPos(kAnchorWide, kWork2560, 780, 900, kGap), 780, 900), kAnchorWide));

    const RECT work2 = MakeRect(0, 0, 1600, 1040);
    const RECT anchor2 = MakeRect(850, 150, 1250, 890);
    VT_CHECK(name, !Overlaps(DialogRect(ComputeAnchoredDialogPos(anchor2, work2, 350, 300, kGap), 350, 300), anchor2));

    const RECT work3 = MakeRect(0, 0, 1300, 1040);
    const RECT anchor3 = MakeRect(400, 100, 900, 500);
    VT_CHECK(name, !Overlaps(DialogRect(ComputeAnchoredDialogPos(anchor3, work3, 800, 300, kGap), 800, 300), anchor3));

    const RECT anchor4 = MakeRect(400, 700, 900, 1030);
    VT_CHECK(name, !Overlaps(DialogRect(ComputeAnchoredDialogPos(anchor4, work3, 800, 300, kGap), 800, 300), anchor4));
}

}  // namespace

int main() {
    TestRightSidePreferred();
    TestLeftSideFallback();
    TestBelowFallback();
    TestAboveFallback();
    TestLargestSpaceWhenNothingFits();
    TestDialogLargerThanWorkArea();
    TestAnchorShiftMinimum();
    TestAnchorShiftImpossible();
    TestAnchorShiftNoopWhenAlreadyClear();
    TestFittingSidesNeverOverlap();

    if (g_failures != 0) {
        std::cout << "dialog_positioning_test: " << g_failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "dialog_positioning_test: all checks passed\n";
    return 0;
}
