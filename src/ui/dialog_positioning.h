#pragma once

// Pure geometry helpers that place a secondary Settings dialog next to its
// anchor window instead of centering it on top of the anchor. Window
// management (CreateWindowExW / SetWindowPos) stays in the caller so the
// placement rules are covered by tests/dialog_positioning_test.cpp.
//
// Reference implementation: zencrop_ocr_pxipin src/core/Utils.cpp
// (PositionWindowNearAnchor), adapted to return a position instead of calling
// SetWindowPos, and with the inter-window gap passed in as a scaled value
// (never a raw pixel constant).

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// Anchors a dialog to the preferred side of `anchor`, trying
// Right -> Left -> Below -> Above. When none of the sides fits, the side with
// the largest remaining space wins (the right side wins ties).
// Side-by-side placements are vertically centred against the anchor; stacked
// placements are left aligned. The result is always clamped into `workArea`;
// a dialog larger than the work area sticks to its top-left corner.
POINT ComputeAnchoredDialogPos(const RECT& anchor, const RECT& workArea,
                               int width, int height, int gap);

// Returns the anchor's new left coordinate that lets a `dialogWidth` wide
// dialog sit next to it without overlap, moving the anchor as little as
// possible (it only ever shifts to the left). Returns anchor.left unchanged
// when side-by-side placement cannot fit inside the work area at all.
int ComputeAnchorShiftForSideBySide(const RECT& anchor, const RECT& workArea,
                                    int dialogWidth, int gap);
