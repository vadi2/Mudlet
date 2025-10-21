# PR #7189 Incremental Changes - Room Panning Optimization

This document breaks down all changes from PR #7189 into individual, testable steps.
Apply each change one at a time and test rendering before proceeding to the next.

## Overview
The PR aimed to speed up 2D map panning by 15-150% by reducing object allocations and redundant operations during rendering. However, some changes broke rendering. This systematic approach will help identify which specific change caused the issue.

---

## Change Group 1: Simple Code Cleanup (Low Risk)

### Change 1.1: Add QDebug include
**File:** `src/T2DMap.cpp`
**Location:** After line 46 (after `#include <QtUiTools>`)
**Action:** Add the following line:
```cpp
#include <QDebug>
```
**Risk:** Very low - just adds header
**Test:** Compile and run, verify map rendering works

---

### Change 1.2: CMakeLists.txt comment indentation
**File:** `src/CMakeLists.txt`
**Location:** Line 564
**Action:** Change:
```cmake
#       nanobench
```
To:
```cmake
#         nanobench
```
**Risk:** None - comment only
**Test:** Compile and run, verify map rendering works

---

### Change 1.3: Remove comment block before drawRoom
**File:** `src/T2DMap.cpp`
**Location:** Lines 586-592
**Action:** Remove the following comment block:
```cpp
// This has been refactored to a separate function out of the paintEven() code
// because we need to use it in two places - one for every room that is not the
// player's room and then, AFTER all those have been drawn, once for the
// player's room if it is visible. This is so it is drawn LAST (and any effects,
// or extra markings for it do not get overwritten by the drawing of the other
// rooms)...
```
**Risk:** None - comment only
**Test:** Compile and run, verify map rendering works

---

## Change Group 2: TRoomDB Optimization (Low Risk)

### Change 2.1: Simplify TRoomDB::getRoom()
**File:** `src/TRoomDB.cpp`
**Location:** Lines 54-59 in getRoom() function
**Action:** Replace:
```cpp
    const auto i = rooms.constFind(id);
    if (i != rooms.constEnd() && i.key() == id) {
        return i.value();
    }
    return nullptr;
```
With:
```cpp
    return rooms.value(id, nullptr);
```
**Risk:** Low - QHash::value() is a standard Qt method
**Test:** Compile and run, verify map rendering works, test room lookups

---

## Change Group 3: Function Signature Changes (Preparation)

### Change 3.1: Add parameters to drawRoom() declaration
**File:** `src/T2DMap.h`
**Location:** Line 257
**Action:** Change function declaration from:
```cpp
    void drawRoom(QPainter&, QFont&, QFont&, QPen&, TRoom*, const bool isGridMode, const bool areRoomIdsLegible, const bool showRoomNames, const int, const float, const float, const QMap<int, QPointF>&);
```
To:
```cpp
    void drawRoom(QPainter&, QFont&, QFont&, QPen&, QPen&, QPen&, QBrush&, QBrush&, TRoom*, const bool isGridMode, const bool areRoomIdsLegible, const bool showRoomNames, const int, const float, const float, const QMap<int, QPointF>&);
```
**Note:** Adds 4 new parameters: innerPen, roomBorderPen, brush, innerBrush
**Risk:** Won't compile until implementation is also updated (do together with 3.2)

### Change 3.2: Update drawRoom() definition
**File:** `src/T2DMap.cpp`
**Location:** Lines 587-592
**Action:** Change function definition from:
```cpp
inline void T2DMap::drawRoom(QPainter& painter,
                             QFont& roomVNumFont,
                             QFont& mapNameFont,
                             QPen& pen,
                             TRoom* pRoom,
                             const bool isGridMode,
                             const bool areRoomIdsLegible,
                             const bool showRoomNames,
                             const int speedWalkStartRoomId,
                             const float rx,
                             const float ry,
                             const QMap<int, QPointF>& areaExitsMap)
{
```
To:
```cpp
inline void T2DMap::drawRoom(QPainter& painter,
                             QFont& roomVNumFont,
                             QFont& mapNameFont,
                             QPen& pen,
                             QPen& innerPen,
                             QPen& roomBorderPen,
                             QBrush& brush,
                             QBrush& innerBrush,
                             TRoom* pRoom,
                             const bool isGridMode,
                             const bool areRoomIdsLegible,
                             const bool showRoomNames,
                             const int speedWalkStartRoomId,
                             const float rx,
                             const float ry,
                             const QMap<int, QPointF>& areaExitsMap) {
```
**Note:** Adds 4 new parameters
**Risk:** Medium - need to update all call sites too
**Test:** Will not compile until call sites updated

### Change 3.3: Mark paintRoomExits() as const in header
**File:** `src/T2DMap.h`
**Location:** Line 260
**Action:** Add `const` to the end:
```cpp
    void paintRoomExits(QPainter&, QPen&, QList<int>& exitList, QList<int>& oneWayExits, const TArea*, int, float, QMap<int, QPointF>&) const;
```

### Change 3.4: Mark paintRoomExits() as const in implementation
**File:** `src/T2DMap.cpp`
**Location:** Line 1687
**Action:** Change:
```cpp
void T2DMap::paintRoomExits(QPainter& painter, QPen& pen, QList<int>& exitList, QList<int>& oneWayExits, const TArea* pArea, int zLevel, float exitWidth, QMap<int, QPointF>& areaExitsMap)
```
To:
```cpp
void T2DMap::paintRoomExits(QPainter& painter, QPen& pen, QList<int>& exitList, QList<int>& oneWayExits, const TArea* pArea, int zLevel, float exitWidth, QMap<int, QPointF>& areaExitsMap) const
```

### Change 3.5: Mark drawDoor() as const in header
**File:** `src/T2DMap.h`
**Location:** Line 262
**Action:** Add `const` to the end:
```cpp
    inline void drawDoor(QPainter&, const TRoom&, const QString&, const QLineF&) const;
```

### Change 3.6: Mark drawDoor() as const in implementation
**File:** `src/T2DMap.cpp`
**Location:** Line 1620
**Action:** Change:
```cpp
void T2DMap::drawDoor(QPainter& painter, const TRoom& room, const QString& dirKey, const QLineF& exitLine)
```
To:
```cpp
void T2DMap::drawDoor(QPainter& painter, const TRoom& room, const QString& dirKey, const QLineF& exitLine) const
```

**Test after all signature changes:** Compile and run, verify map rendering works

---

## Change Group 4: paintEvent() Pen/Brush Creation (Medium Risk)

### Change 4.1: Create reusable pens and brushes in paintEvent()
**File:** `src/T2DMap.cpp`
**Location:** After line 1412 (after `bool isPlayerRoomVisible = false;`)
**Action:** Add these lines:
```cpp

    // keep a few brushes and pens around for drawing all rooms in bulk
    // re-creating the objects on a million-room map is costly, as is changing their properties
    QBrush brush, innerBrush;

    QPen roomPen = pen;
    roomPen.setCosmetic(mMapperUseAntiAlias);
    roomPen.setCapStyle(Qt::RoundCap);
    roomPen.setJoinStyle(Qt::RoundJoin);
    QPen innerRoomPen = roomPen;

    const bool shouldDrawBorder = mpHost->mMapperShowRoomBorders && !pDrawnArea->gridMode;
    const int borderWidth = 1 / eSize * mRoomWidth * rSize;
    QPen roomBorderPen = roomPen;

    if (shouldDrawBorder && mRoomWidth >= 12) {
        roomBorderPen.setColor(mpHost->mRoomBorderColor);
    } else if (shouldDrawBorder) {
        auto fadingColor = QColor(mpHost->mRoomBorderColor);
        fadingColor.setAlpha(255 * (mRoomWidth / 12));
        roomBorderPen.setColor(fadingColor);
    }
    roomBorderPen.setWidth(borderWidth);


```

### Change 4.2: Update drawRoom() call sites in paintEvent() - first call
**File:** `src/T2DMap.cpp`
**Location:** Line 1437
**Action:** Change:
```cpp
            drawRoom(painter, roomVNumFont, mapNameFont, pen, room, pDrawnArea->gridMode, isFontBigEnoughToShowRoomVnum, showRoomNames, playerRoomId, rx, ry, areaExitsMap);
```
To:
```cpp
            drawRoom(painter, roomVNumFont, mapNameFont, roomPen, innerRoomPen, roomBorderPen, brush, innerBrush, room, pDrawnArea->gridMode, isFontBigEnoughToShowRoomVnum, showRoomNames, playerRoomId, rx, ry, areaExitsMap);
```

### Change 4.3: Update drawRoom() call sites in paintEvent() - second call
**File:** `src/T2DMap.cpp`
**Location:** Line 1442
**Action:** Change:
```cpp
        drawRoom(painter, roomVNumFont, mapNameFont, pen, pPlayerRoom, pDrawnArea->gridMode, isFontBigEnoughToShowRoomVnum, showRoomNames, playerRoomId, static_cast<float>(playerRoomOnWidgetCoordinates.x()), static_cast<float>(playerRoomOnWidgetCoordinates.y()), areaExitsMap);
```
To:
```cpp
        drawRoom(painter, roomVNumFont, mapNameFont, roomPen, innerRoomPen, roomBorderPen, brush, innerBrush, pPlayerRoom, pDrawnArea->gridMode, isFontBigEnoughToShowRoomVnum, showRoomNames, playerRoomId, static_cast<float>(playerRoomOnWidgetCoordinates.x()), static_cast<float>(playerRoomOnWidgetCoordinates.y()), areaExitsMap);

```

**Test:** Compile and run, verify map rendering works
**Note:** At this point the new parameters are being passed but not used yet in drawRoom()

---

## Change Group 5: Variable Renaming in drawRoom() (Low Risk)

### Change 5.1: Rename isRoomSelected to roomSelected
**File:** `src/T2DMap.cpp`
**Location:** Throughout drawRoom() function
**Action:** Replace all occurrences of `isRoomSelected` with `roomSelected`
- Line 688: Definition
- Line 707: First if condition
- Line 728: Second if condition
- Line 734: Third if condition

**Specifically:**
Line 688:
```cpp
    const bool roomSelected = (mPick && roomClickTestRectangle.contains(mPHighlight)) || mMultiSelectionSet.contains(currentRoomId);
```

Line 707:
```cpp
    } else if (roomSelected) {
```

Line 728:
```cpp
        if (!roomSelected) {
```

Line 734:
```cpp
    if (roomSelected) {
```

**Test:** Compile and run, verify map rendering works

---

## Change Group 6: drawRoom() Internal Refactoring - Part 1 (HIGH RISK)

### Change 6.1: Remove local selectionBg gradient and roomPen
**File:** `src/T2DMap.cpp`
**Location:** Lines 690-695
**Action:** **DELETE** these lines:
```cpp
    QLinearGradient selectionBg(roomRectangle.topLeft(), roomRectangle.bottomRight());
    selectionBg.setColorAt(0.25, roomColor);
    selectionBg.setColorAt(1, Qt::blue);

    QPen roomPen(Qt::transparent);
    roomPen.setWidth(borderWidth);
```

**Risk:** HIGH - This removes local variables that may be needed
**Test:** Compile and run, CAREFULLY verify room borders and selection highlighting

### Change 6.2: Refactor border and selection pen logic
**File:** `src/T2DMap.cpp`
**Location:** Lines 698-715 (after removing the lines from 6.1)
**Action:** Replace:
```cpp
    painter.setBrush(roomColor);

    if (shouldDrawBorder && mRoomWidth >= 12) {
        roomPen.setColor(mpHost->mRoomBorderColor);
    } else if (shouldDrawBorder) {
        auto fadingColor = QColor(mpHost->mRoomBorderColor);
        fadingColor.setAlpha(255 * (mRoomWidth / 12));
        roomPen.setColor(fadingColor);
    }

    if (isRoomSelected) {
        QLinearGradient selectionBg(roomRectangle.topLeft(), roomRectangle.bottomRight());
        selectionBg.setColorAt(0.2, roomColor);
        selectionBg.setColorAt(1, Qt::blue);
        roomPen.setColor(QColor(255, 50, 50));
        painter.setBrush(selectionBg);
    }

    painter.setPen(roomPen);
```

With:
```cpp
    painter.setBrush(roomColor);

    if (shouldDrawBorder && mRoomWidth >= 12) {
        pen.setColor(mpHost->mRoomBorderColor);
    } else if (shouldDrawBorder) {
        auto fadingColor = QColor(mpHost->mRoomBorderColor);
        fadingColor.setAlpha(255 * (mRoomWidth / 12));
        pen.setColor(fadingColor);
    } else if (roomSelected) {
        QLinearGradient selectionBg(roomRectangle.topLeft(), roomRectangle.bottomRight());
        selectionBg.setColorAt(0.2, roomColor);
        selectionBg.setColorAt(1, Qt::blue);
        pen.setColor(QColor(255, 50, 50));
        painter.setBrush(selectionBg);
    } else {
        pen.setColor(Qt::transparent);
    }

    painter.setPen(pen);
```

**Risk:** HIGH - Changes pen color logic flow
**Test:** CRITICAL - Verify room borders, selected rooms highlight correctly, and non-selected rooms render properly

---

## Change Group 7: drawRoom() Internal Refactoring - Part 2 (HIGH RISK)

### Change 7.1: Replace transparentPen in speedwalk section
**File:** `src/T2DMap.cpp`
**Location:** Lines 747-753 (in the speedwalk visualization section)
**Action:** Replace:
```cpp
            gradient.setColorAt(0.799, QColor(150, 100, 100, 100));
            gradient.setColorAt(0.7, QColor(255, 0, 0, 200));
            gradient.setColorAt(0, Qt::white);
            const QPen transparentPen(Qt::transparent);
            QPainterPath diameterPath;
            painter.setBrush(gradient);
            painter.setPen(transparentPen);
            diameterPath.addEllipse(roomCenter, roomRadius, roomRadius);
            painter.drawPath(diameterPath);
```

With:
```cpp
            gradient.setColorAt(0.799, QColor(150, 100, 100, 100));
            gradient.setColorAt(0.7, QColor(255, 0, 0, 200));
            gradient.setColorAt(0, Qt::white);
            pen.setColor(Qt::transparent);
            QPainterPath diameterPath;
            painter.setBrush(gradient);
            diameterPath.addEllipse(roomCenter, roomRadius, roomRadius);
            painter.drawPath(diameterPath);
```

**Risk:** HIGH - Changes pen handling in speedwalk visualization
**Test:** Verify speedwalk path visualization still renders correctly

### Change 7.2: Replace transparentPen in highlight section
**File:** `src/T2DMap.cpp`
**Location:** Lines 823-829 (in the highlight section)
**Action:** Replace:
```cpp
        gradient.setColorAt(0.85, pRoom->highlightColor);
        gradient.setColorAt(0, pRoom->highlightColor2);
        const QPen transparentPen(Qt::transparent);
        QPainterPath diameterPath;
        painter.setBrush(gradient);
        painter.setPen(transparentPen);
        diameterPath.addEllipse(roomCenter, roomRadius, roomRadius);
        painter.drawPath(diameterPath);
```

With:
```cpp
        gradient.setColorAt(0.85, pRoom->highlightColor);
        gradient.setColorAt(0, pRoom->highlightColor2);
        pen.setColor(Qt::transparent);
        QPainterPath diameterPath;
        painter.setBrush(gradient);
        diameterPath.addEllipse(roomCenter, roomRadius, roomRadius);
        painter.drawPath(diameterPath);
```

**Risk:** HIGH - Changes pen handling in room highlighting
**Test:** Verify room highlighting still works correctly

### Change 7.3: Replace QPen() in room ID section
**File:** `src/T2DMap.cpp`
**Location:** Line 840
**Action:** Replace:
```cpp
        painter.setPen(QPen(roomIdColor));
```

With:
```cpp
        pen.setColor(roomIdColor);
```

**Risk:** Medium - Changes how room ID color is set
**Test:** Verify room IDs display with correct colors

### Change 7.4: Replace QPen() in room name section
**File:** `src/T2DMap.cpp`
**Location:** Line 881
**Action:** Replace:
```cpp
        painter.setPen(QPen(roomNameColor));
```

With:
```cpp
        pen.setColor(roomNameColor);
```

**Risk:** Medium - Changes how room name color is set
**Test:** Verify room names display with correct colors

---

## Change Group 8: drawRoom() Exit Stub Drawing Refactoring (VERY HIGH RISK)

### Change 8.1: Remove pen modification and painter.save() before exit stubs
**File:** `src/T2DMap.cpp`
**Location:** Lines 906-913
**Action:** DELETE these lines:
```cpp
    pen = painter.pen();
    pen.setColor(lc);
    pen.setCosmetic(mMapperUseAntiAlias);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    QPen innerPen = pen;
    painter.save();

    QBrush innerBrush = painter.brush();
```

And REPLACE with:
```cpp
    pen.setColor(lc);
```

**Risk:** VERY HIGH - Removes critical pen setup and save/restore
**Test:** CRITICAL - Verify exit stubs (up/down/in/out) render correctly

### Change 8.2: Set innerBrush style instead of creating new brush
**File:** `src/T2DMap.cpp`
**Location:** Line after the pen.setColor(lc); change above
**Action:** Add:
```cpp

    innerBrush.setStyle(Qt::NoBrush);
```

**Risk:** High - Changes brush handling
**Test:** Verify exit stub interiors render correctly

### Change 8.3: Remove local brush in UP exit stub
**File:** `src/T2DMap.cpp`
**Location:** In the UP exit stub section (around line 922)
**Action:** DELETE the line:
```cpp
        QBrush brush = painter.brush();
```

**Risk:** HIGH - Uses passed-in brush instead of local
**Test:** Verify UP exit stub rendering and door backgrounds

### Change 8.4: Remove painter.setPen(pen) in UP exit stub
**File:** `src/T2DMap.cpp`
**Location:** Just before `painter.drawPolygon(poly_up);` in UP section
**Action:** DELETE the line:
```cpp
        painter.setPen(pen);
```

**Risk:** HIGH - Relies on pen already being set
**Test:** Verify UP exit stub borders

### Change 8.5: Remove local brush in DOWN exit stub
**File:** `src/T2DMap.cpp`
**Location:** In the DOWN exit stub section (around line 960)
**Action:** DELETE the line:
```cpp
        QBrush brush = painter.brush();
```

**Risk:** HIGH - Uses passed-in brush instead of local
**Test:** Verify DOWN exit stub rendering and door backgrounds

### Change 8.6: Remove local brush in IN exit stub
**File:** `src/T2DMap.cpp`
**Location:** In the IN exit stub section (around line 1006)
**Action:** DELETE the line:
```cpp
        QBrush brush = painter.brush();
```

**Risk:** HIGH - Uses passed-in brush instead of local
**Test:** Verify IN exit stub rendering and door backgrounds

### Change 8.7: Remove local brush in OUT exit stub
**File:** `src/T2DMap.cpp`
**Location:** In the OUT exit stub section (around line 1054)
**Action:** DELETE the line:
```cpp
        QBrush brush = painter.brush();
```

**Risk:** HIGH - Uses passed-in brush instead of local
**Test:** Verify OUT exit stub rendering and door backgrounds

### Change 8.8: Remove painter.restore() after exit stubs
**File:** `src/T2DMap.cpp`
**Location:** After all exit stub code, before area exits section
**Action:** DELETE the line:
```cpp
    painter.restore();
```

**Risk:** VERY HIGH - Removes painter state restoration
**Test:** CRITICAL - Verify all subsequent rendering (area exits, labels, etc.) still works

---

## Change Group 9: paintRoomExits() Optimization (Medium Risk)

### Change 9.1: Add blank line in paintEvent
**File:** `src/T2DMap.cpp`
**Location:** Line 1381 (before label sizing box comment)
**Action:** Add one blank line

**Risk:** None - formatting only

### Change 9.2: Replace QSetIterator with range-based for loop
**File:** `src/T2DMap.cpp`
**Location:** Lines 1728-1730 in paintRoomExits()
**Action:** Replace:
```cpp
    QSetIterator<int> itRoom2(pArea->getAreaRooms());
    while (itRoom2.hasNext()) {
        const int _id = itRoom2.next();
```

With:
```cpp
    const auto& areaRooms = pArea->getAreaRooms();
    for (auto i = areaRooms.cbegin(), end = areaRooms.cend(); i != end; ++i) {
        const int _id = *i;
```

**Risk:** Medium - Changes iteration method
**Test:** Verify all room exits render correctly, especially in large areas

### Change 9.3: Add qAsConst() wrapper
**File:** `src/T2DMap.cpp`
**Location:** Line 2102 in paintRoomExits()
**Action:** Replace:
```cpp
        for (const int& k : exitList) {
```

With:
```cpp
        for (const int& k : qAsConst(exitList)) {
```

**Risk:** Low - Qt optimization
**Test:** Verify exits render correctly

---

## Testing Strategy

After each change group:
1. **Build** the project
2. **Run** Mudlet
3. **Load a test map** (preferably one with many rooms)
4. **Test these specific scenarios:**
   - Pan around the map - does it render correctly?
   - Select rooms - do they highlight properly?
   - Check room borders - are they visible and correct?
   - Check room IDs - are they displayed correctly?
   - Check room names - are they displayed correctly?
   - Check exit stubs (up/down/in/out) - do they render with correct colors?
   - Check door indicators - do they show correct colors (open/closed/locked)?
   - Check area exits - do they render correctly?
   - Pan with many rooms visible - is it faster? Does rendering break?

5. **If rendering breaks:**
   - Note which change group caused the break
   - Revert that specific change
   - Document which change(s) in that group caused the issue
   - Continue testing remaining changes if desired

---

## Known High-Risk Changes

Based on the PR discussion, the rendering issues likely stem from these specific changes:

1. **Change 6.2** - Refactored border and selection pen logic
2. **Change 8.1** - Removed painter.save()/restore()
3. **Change 8.3-8.7** - Removed local brush declarations in exit stubs
4. **Change 8.8** - Removed painter.restore() after exit stubs

These changes modify how pens and brushes are managed, which affects:
- Door background colors
- Selected room gradients
- Room number colors
- Drawing order precedence

---

## Notes

- The original PR author noted that door backgrounds, selected room gradients, and room number colors had rendering issues
- The problem is likely in how pens and brushes are being reused vs. created locally
- Pay special attention to state management when testing each change
- The performance improvements come from reducing object allocations, but this must not break rendering correctness
