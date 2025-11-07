# Feature Callout System - Technical Design

## Overview

A generic, reusable feature callout system for Mudlet that allows highlighting UI elements with overlay pointers, explanatory text, and optional guided tours. Built using Qt6 and C++20, following Mudlet's architecture patterns.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Application Layer                         │
│  (mudlet, Host, dlgTriggerEditor, etc.)                     │
└───────────────────┬─────────────────────────────────────────┘
                    │ uses
                    ▼
┌─────────────────────────────────────────────────────────────┐
│              TFeatureCalloutManager                          │
│  • Manages lifecycle of all callouts                         │
│  • Tracks shown/dismissed state (per profile)               │
│  • Provides convenience API                                  │
└───────────────────┬─────────────────────────────────────────┘
                    │ creates/manages
                    ▼
┌──────────────────────────────────┐  ┌──────────────────────┐
│      TCalloutOverlay             │  │  TCalloutPopup       │
│  • Transparent widget overlay    │  │  • Callout box UI    │
│  • Spotlight/dimming effect      │  │  • Arrow pointer     │
│  • Event filtering               │  │  • Dismiss button    │
│  • Tracks target geometry        │  │  • Rich text content │
└──────────────────────────────────┘  └──────────────────────┘
```

## Core Classes

### 1. TFeatureCallout (Data Structure)

**File:** `src/TFeatureCallout.h`

Represents a single callout configuration. Plain struct with no Qt inheritance.

```cpp
#ifndef MUDLET_TFEATURECALLOUT_H
#define MUDLET_TFEATURECALLOUT_H

#include <QColor>
#include <QMargins>
#include <QString>
#include <functional>

class QWidget;

// Where the callout popup should appear relative to the target
enum class CalloutPosition {
    Auto,         // Automatically choose best position
    TopLeft,
    TopCenter,
    TopRight,
    BottomLeft,
    BottomCenter,
    BottomRight,
    LeftTop,
    LeftCenter,
    LeftBottom,
    RightTop,
    RightCenter,
    RightBottom
};

// Visual style of the callout
enum class CalloutStyle {
    Default,      // Standard callout with arrow
    Spotlight,    // Dim everything except target
    Minimal,      // Tooltip-like, no arrow
    Attention     // Animated border, no dimming
};

struct TFeatureCallout {
    // Required fields
    QString id;                    // Unique identifier (e.g., "trigger-editor-pattern-nav")
    QWidget* targetWidget;         // Widget to point at
    QString message;               // HTML-formatted message content

    // Optional fields
    QString title;                 // Optional title/heading
    CalloutPosition position = CalloutPosition::Auto;
    CalloutStyle style = CalloutStyle::Default;

    // Behavioral options
    bool isDismissible = true;     // Can user close it?
    bool allowPermanentDismiss = true; // Show "Don't show again"?
    int autoHideMs = 0;            // Auto-hide after N ms (0 = manual)
    bool blockInteraction = false; // Block clicks to underlying UI?
    bool trackTarget = true;       // Update position when target moves?

    // Visual customization
    QColor backgroundColor = QColor(255, 255, 255);
    QColor textColor = QColor(0, 0, 0);
    QColor borderColor = QColor(100, 149, 237); // Cornflower blue
    int borderWidth = 2;
    int cornerRadius = 8;
    QMargins padding = QMargins(16, 12, 16, 12);

    // Spotlight effect settings (when style == Spotlight)
    QColor overlayColor = QColor(0, 0, 0, 180); // Semi-transparent black
    int spotlightPadding = 8;      // Padding around target in spotlight

    // Callbacks
    std::function<void()> onShow;      // Called when shown
    std::function<void()> onDismiss;   // Called when dismissed
    std::function<void()> onAction;    // Called when action button clicked
    std::function<bool()> shouldShow;  // Conditional display check

    // Action button (optional)
    QString actionButtonText;      // If set, shows an action button
};

#endif // MUDLET_TFEATURECALLOUT_H
```

---

### 2. TCalloutOverlay

**File:** `src/TCalloutOverlay.h`

Transparent overlay that covers the parent widget, providing dimming/spotlight effects.

```cpp
#ifndef MUDLET_TCALLOUTOVERLAY_H
#define MUDLET_TCALLOUTOVERLAY_H

#include "TFeatureCallout.h"
#include <QWidget>
#include <QPointer>
#include <QTimer>

class TCalloutPopup;

class TCalloutOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit TCalloutOverlay(QWidget* parent, const TFeatureCallout& callout);
    ~TCalloutOverlay() override;

    // Show/hide the callout
    void showCallout();
    void hideCallout(bool permanent = false);

    // Update position when target moves
    void updatePosition();

    // Access the callout configuration
    const TFeatureCallout& callout() const { return mCallout; }
    QString calloutId() const { return mCallout.id; }

signals:
    void dismissed(const QString& calloutId, bool permanent);
    void actionClicked(const QString& calloutId);

protected:
    void paintEvent(QPaintEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private slots:
    void slot_autoHide();
    void slot_dismiss();
    void slot_dismissPermanently();
    void slot_actionClicked();

private:
    void setupCalloutPopup();
    void positionCalloutPopup();
    QRect calculateTargetRect() const;
    QPoint calculatePopupPosition(const QRect& targetRect, const QSize& popupSize) const;
    void drawSpotlight(QPainter& painter, const QRect& targetRect);
    void drawAttentionBorder(QPainter& painter, const QRect& targetRect);
    void installTargetEventFilter();
    void removeTargetEventFilter();

    TFeatureCallout mCallout;
    QPointer<QWidget> mpTargetWidget;
    TCalloutPopup* mpPopup = nullptr;
    QTimer* mpAutoHideTimer = nullptr;
    QTimer* mpAnimationTimer = nullptr;
    int mAnimationFrame = 0;
    bool mTargetDestroyed = false;
};

#endif // MUDLET_TCALLOUTOVERLAY_H
```

---

### 3. TCalloutPopup

**File:** `src/TCalloutPopup.h`

The actual callout box with message, arrow, and buttons.

```cpp
#ifndef MUDLET_TCALLOUTPOPUP_H
#define MUDLET_TCALLOUTPOPUP_H

#include "TFeatureCallout.h"
#include <QWidget>

class QLabel;
class QPushButton;
class QVBoxLayout;

class TCalloutPopup : public QWidget
{
    Q_OBJECT

public:
    explicit TCalloutPopup(QWidget* parent, const TFeatureCallout& callout);

    // Calculate ideal size based on content
    QSize sizeHint() const override;

    // Set arrow direction and position
    void setArrowDirection(CalloutPosition position);

    // Get the arrow tip position in parent coordinates
    QPoint arrowTipPosition() const;

signals:
    void dismissClicked();
    void dismissPermanentlyClicked();
    void actionClicked();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void setupUI();
    void drawArrow(QPainter& painter);
    void drawBackground(QPainter& painter);
    QPolygon calculateArrowPolygon() const;

    const TFeatureCallout& mCallout;

    // UI Components
    QLabel* mpTitleLabel = nullptr;
    QLabel* mpMessageLabel = nullptr;
    QPushButton* mpActionButton = nullptr;
    QPushButton* mpDismissButton = nullptr;
    QPushButton* mpDismissPermanentlyButton = nullptr;
    QVBoxLayout* mpMainLayout = nullptr;

    // Arrow rendering
    CalloutPosition mArrowPosition = CalloutPosition::Auto;
    static constexpr int ARROW_SIZE = 12;
    static constexpr int ARROW_OFFSET = 20; // Distance from edge
};

#endif // MUDLET_TCALLOUTPOPUP_H
```

---

### 4. TFeatureCalloutManager

**File:** `src/TFeatureCalloutManager.h`

Central manager for all callouts in the application.

```cpp
#ifndef MUDLET_TFEATURECALLOUTMANAGER_H
#define MUDLET_TFEATURECALLOUTMANAGER_H

#include "TFeatureCallout.h"
#include <QObject>
#include <QHash>
#include <QSet>
#include <QPointer>

class TCalloutOverlay;
class Host;
class QWidget;

class TFeatureCalloutManager : public QObject
{
    Q_OBJECT

public:
    explicit TFeatureCalloutManager(QObject* parent = nullptr);
    ~TFeatureCalloutManager() override;

    // Primary API
    void showCallout(const TFeatureCallout& callout);
    void hideCallout(const QString& calloutId);
    void hideAllCallouts();

    // Check if a callout is currently visible
    bool isCalloutVisible(const QString& calloutId) const;

    // Check if user has permanently dismissed a callout
    bool isCalloutPermanentlyDismissed(const QString& calloutId) const;

    // Set profile context for persistent storage
    void setProfileHost(Host* host);

    // Reset dismissal state (useful for testing or user-requested reset)
    void resetDismissedCallouts();
    void resetDismissedCallout(const QString& calloutId);

    // Convenience methods for common patterns
    void showTooltipCallout(QWidget* target, const QString& message,
                           int autoHideMs = 5000);
    void showSpotlightCallout(QWidget* target, const QString& title,
                             const QString& message, const QString& id);

signals:
    void calloutShown(const QString& calloutId);
    void calloutDismissed(const QString& calloutId, bool permanent);
    void calloutActionClicked(const QString& calloutId);

private slots:
    void slot_calloutDismissed(const QString& calloutId, bool permanent);
    void slot_calloutActionClicked(const QString& calloutId);

private:
    void loadDismissedState();
    void saveDismissedState();
    QString settingsKey(const QString& calloutId) const;
    QWidget* findTopLevelParent(QWidget* widget) const;

    // Active overlays, keyed by callout ID
    QHash<QString, TCalloutOverlay*> mActiveCallouts;

    // Permanently dismissed callouts (persisted per profile)
    QSet<QString> mPermanentlyDismissed;

    // Current profile context
    QPointer<Host> mpHost;
};

#endif // MUDLET_TFEATURECALLOUTMANAGER_H
```

---

## Integration Points

### 1. Add Manager Instance to mudlet Class

**File:** `src/mudlet.h` (additions)

```cpp
class mudlet : public QMainWindow, public Ui::main_window
{
    Q_OBJECT

public:
    // ... existing methods ...

    // Access the feature callout manager
    TFeatureCalloutManager* getFeatureCalloutManager() { return mpCalloutManager; }

private:
    // ... existing members ...

    TFeatureCalloutManager* mpCalloutManager = nullptr;
};
```

**File:** `src/mudlet.cpp` (constructor)

```cpp
mudlet::mudlet()
{
    // ... existing initialization ...

    // Initialize callout manager
    mpCalloutManager = new TFeatureCalloutManager(this);
}
```

---

### 2. Profile-Specific Context

**File:** `src/Host.cpp` (when profile loads)

```cpp
void Host::onProfileLoaded()
{
    // ... existing code ...

    // Set profile context for callout manager
    if (auto* pMudlet = mudlet::self()) {
        pMudlet->getFeatureCalloutManager()->setProfileHost(this);
    }
}
```

---

## Usage Examples

### Example 1: Simple Tooltip-Style Callout

```cpp
// In any Mudlet class with access to mudlet::self()
auto* manager = mudlet::self()->getFeatureCalloutManager();

TFeatureCallout callout;
callout.id = qsl("welcome-message");
callout.targetWidget = someWidget;
callout.message = tr("Welcome to the new feature!");
callout.style = CalloutStyle::Minimal;
callout.autoHideMs = 5000; // Auto-hide after 5 seconds

manager->showCallout(callout);
```

---

### Example 2: Spotlight Callout with Action

```cpp
auto* manager = mudlet::self()->getFeatureCalloutManager();

TFeatureCallout callout;
callout.id = qsl("mapper-intro");
callout.targetWidget = mpMapperWidget;
callout.title = tr("Mapper Feature");
callout.message = tr("<p>The mapper helps you navigate the game world.</p>"
                    "<p>Click the button below to learn more.</p>");
callout.style = CalloutStyle::Spotlight;
callout.position = CalloutPosition::RightCenter;
callout.actionButtonText = tr("Take Tour");

// Set up callback for action button
callout.onAction = [this]() {
    startMapperTour();
};

// Only show if user hasn't seen it
callout.shouldShow = [manager]() {
    return !manager->isCalloutPermanentlyDismissed(qsl("mapper-intro"));
};

manager->showCallout(callout);
```

---

### Example 3: Pattern Navigation Hint (Replacing Existing Banner)

```cpp
// In dlgTriggerEditor - replaces the existing pattern navigation banner
void dlgTriggerEditor::showPatternNavigationCallout()
{
    auto* manager = mudlet::self()->getFeatureCalloutManager();

    TFeatureCallout callout;
    callout.id = qsl("trigger-pattern-navigation");
    callout.targetWidget = mTriggerPatternEdit[0]; // First pattern widget
    callout.message = tr("<p>Use <strong>Ctrl+↓</strong> and <strong>Ctrl+↑</strong> "
                        "to navigate between patterns quickly.</p>");
    callout.style = CalloutStyle::Default;
    callout.position = CalloutPosition::TopRight;
    callout.allowPermanentDismiss = true;

    // Track in profile settings
    if (!manager->isCalloutPermanentlyDismissed(callout.id)) {
        manager->showCallout(callout);
    }
}
```

---

### Example 4: Multi-Step Tour

```cpp
class MapperTourManager {
public:
    void startTour() {
        mCurrentStep = 0;
        showStep(0);
    }

private:
    void showStep(int stepIndex) {
        auto* manager = mudlet::self()->getFeatureCalloutManager();

        // Hide previous step
        if (stepIndex > 0) {
            manager->hideCallout(qsl("mapper-tour-step-%1").arg(stepIndex - 1));
        }

        if (stepIndex >= mTourSteps.size()) {
            // Tour complete
            emit tourCompleted();
            return;
        }

        const auto& step = mTourSteps[stepIndex];

        TFeatureCallout callout;
        callout.id = qsl("mapper-tour-step-%1").arg(stepIndex);
        callout.targetWidget = step.widget;
        callout.title = tr("Step %1 of %2").arg(stepIndex + 1).arg(mTourSteps.size());
        callout.message = step.message;
        callout.style = CalloutStyle::Spotlight;
        callout.position = step.position;
        callout.actionButtonText = (stepIndex < mTourSteps.size() - 1)
                                   ? tr("Next") : tr("Finish");
        callout.isDismissible = true;
        callout.allowPermanentDismiss = false; // Don't offer "don't show again" mid-tour

        callout.onAction = [this, stepIndex]() {
            showStep(stepIndex + 1);
        };

        callout.onDismiss = [this]() {
            emit tourCancelled();
        };

        manager->showCallout(callout);
        mCurrentStep = stepIndex;
    }

    struct TourStep {
        QWidget* widget;
        QString message;
        CalloutPosition position;
    };

    QVector<TourStep> mTourSteps;
    int mCurrentStep = 0;
};
```

---

## Implementation Details

### Painting the Overlay

**TCalloutOverlay::paintEvent()** pseudocode:

```cpp
void TCalloutOverlay::paintEvent(QPaintEvent* event)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    switch (mCallout.style) {
        case CalloutStyle::Spotlight: {
            // Fill entire overlay with semi-transparent color
            painter.fillRect(rect(), mCallout.overlayColor);

            // Cut out the spotlight area
            QRect targetRect = calculateTargetRect();
            targetRect.adjust(-mCallout.spotlightPadding,
                             -mCallout.spotlightPadding,
                             mCallout.spotlightPadding,
                             mCallout.spotlightPadding);

            painter.setCompositionMode(QPainter::CompositionMode_Clear);
            painter.fillRect(targetRect, Qt::transparent);
            painter.setCompositionMode(QPainter::CompositionMode_SourceOver);

            // Draw subtle border around spotlight
            painter.setPen(QPen(mCallout.borderColor, 2));
            painter.drawRoundedRect(targetRect, 4, 4);
            break;
        }

        case CalloutStyle::Attention: {
            QRect targetRect = calculateTargetRect();
            drawAttentionBorder(painter, targetRect);
            break;
        }

        case CalloutStyle::Default:
        case CalloutStyle::Minimal:
            // No overlay painting needed, just the popup
            break;
    }
}
```

---

### Event Filtering for Target Tracking

```cpp
bool TCalloutOverlay::eventFilter(QObject* watched, QEvent* event)
{
    if (!mCallout.trackTarget || watched != mpTargetWidget) {
        return QWidget::eventFilter(watched, event);
    }

    switch (event->type()) {
        case QEvent::Move:
        case QEvent::Resize:
        case QEvent::Show:
        case QEvent::Hide:
            // Update callout position
            QTimer::singleShot(0, this, &TCalloutOverlay::updatePosition);
            break;

        case QEvent::Destroy:
            // Target is being destroyed, hide the callout
            mTargetDestroyed = true;
            hideCallout(false);
            break;

        default:
            break;
    }

    return QWidget::eventFilter(watched, event);
}
```

---

### Settings Persistence

**TFeatureCalloutManager::saveDismissedState()**

```cpp
void TFeatureCalloutManager::saveDismissedState()
{
    if (!mpHost) {
        // No profile context, save to global settings
        QSettings settings;
        QStringList dismissed;
        for (const QString& id : mPermanentlyDismissed) {
            dismissed << id;
        }
        settings.setValue(qsl("FeatureCallouts/dismissed"), dismissed);
        return;
    }

    // Save to profile-specific settings
    QSettings settings;
    QString prefix = qsl("Profile_%1/FeatureCallouts").arg(mpHost->getName());

    for (const QString& id : mPermanentlyDismissed) {
        settings.setValue(qsl("%1/dismissed/%2").arg(prefix, id), true);
    }
}
```

---

## File Structure

```
src/
├── TFeatureCallout.h              (Data structure)
├── TCalloutOverlay.h/cpp          (Overlay widget)
├── TCalloutPopup.h/cpp            (Popup widget)
├── TFeatureCalloutManager.h/cpp   (Manager)
├── mudlet.h/cpp                   (Integration)
└── Host.h/cpp                     (Profile integration)
```

---

## Lua API (Future Extension)

A Lua API could be added later for user scripts to show callouts:

```lua
-- Show a callout pointing to a label
callout = mudlet.showCallout({
    id = "my-custom-callout",
    target = "myLabelName",  -- Name of a Geyser label
    message = "This is my custom feature!",
    title = "New Feature",
    position = "top-right",
    style = "spotlight",
    autoHideMs = 10000,
    onAction = function()
        print("User clicked the action button!")
    end,
    actionText = "Learn More"
})

-- Dismiss a callout
mudlet.hideCallout("my-custom-callout")

-- Check if dismissed
if mudlet.isCalloutDismissed("my-custom-callout") then
    print("User has seen this before")
end
```

Implementation would add methods to `TLuaInterpreterUI.cpp`.

---

## Advantages of This Design

1. **Separation of Concerns**: Clear division between overlay (visual effects), popup (content), and manager (lifecycle)

2. **Flexible Positioning**: Automatic position calculation with manual override options

3. **Multiple Visual Styles**: From subtle tooltips to full spotlight tours

4. **Profile-Aware**: Dismissal state saved per profile, not globally

5. **Event-Driven**: Responds to target widget movement, resize, destruction

6. **Extensible**: Easy to add new styles, positions, or behaviors

7. **Memory Safe**: Uses QPointer for all widget references

8. **Qt Parent-Child Cleanup**: Overlays are children of their parent windows, automatic cleanup

9. **Callback Support**: Modern C++ lambdas for flexible behavior

10. **Future-Proof**: Can add tour sequencing, Lua API, animations later

---

## Implementation Phases

### Phase 1: Core Classes (Week 1-2)
- Implement TFeatureCallout struct
- Implement TCalloutPopup (without arrows first)
- Implement TCalloutOverlay (basic overlay only)
- Basic positioning logic

### Phase 2: Advanced Features (Week 2-3)
- Arrow rendering with positioning
- Spotlight effect
- Attention animation
- Event filtering for target tracking

### Phase 3: Manager & Integration (Week 3-4)
- TFeatureCalloutManager implementation
- Settings persistence
- Integration with mudlet and Host
- Replace existing banner in dlgTriggerEditor

### Phase 4: Polish & Testing (Week 4-5)
- Cross-platform testing
- Detached window support
- Performance optimization
- Documentation

### Phase 5: Optional Extensions
- Multi-step tour helper classes
- Lua API
- Theme integration
- Accessibility features

---

## Platform Considerations

### macOS
- Test with dark mode and light mode
- Ensure overlays work with macOS window shadows
- Handle retina display scaling

### Linux
- Test with various window managers (KDE, GNOME, i3)
- Handle different compositor behaviors
- Test with Wayland and X11

### Windows
- Handle DPI scaling correctly
- Test with Windows animations enabled/disabled
- Ensure overlays appear above taskbar if needed

---

## Open Questions for Discussion

1. **Animation**: Should we add fade-in/fade-out animations?
2. **Multiple Callouts**: Allow multiple simultaneous callouts or one at a time?
3. **Themes**: Should callouts respect the current Mudlet theme (dark/light)?
4. **Detached Windows**: How to handle callouts on TDetachedWindow?
5. **Modal Blocking**: Should spotlight mode truly block interaction or just visually indicate focus?
6. **Default Styling**: What colors/sizes feel right for Mudlet's UI?

---

## Testing Strategy

### Unit Tests
- Callout configuration validation
- Position calculation algorithms
- Settings persistence/loading

### Integration Tests
- Show/hide callouts in trigger editor
- Profile switching preserves dismissed state
- Widget destruction cleanup

### Manual Tests
- Test all position combinations
- Test all style combinations
- Window resize/move tracking
- Multi-monitor setups
- Accessibility (keyboard navigation, screen readers)

---

## Performance Considerations

1. **Overlay Painting**: Use simple shapes, avoid complex gradients
2. **Event Filtering**: Minimal processing in eventFilter()
3. **Memory**: Maximum one overlay per top-level window
4. **Animations**: Use QTimer sparingly, cap frame rate at 30fps
5. **Settings I/O**: Batch write on app close, not per-dismissal

---

## Accessibility

1. **Keyboard Navigation**: Tab through callout buttons, Esc to dismiss
2. **Screen Readers**: Proper ARIA labels on all UI elements
3. **High Contrast**: Respect system high contrast mode
4. **Focus Management**: Return focus to target after dismissal
5. **Reduced Motion**: Skip animations if system prefers reduced motion

---

## End of Design Document
