/***************************************************************************
 *   Copyright (C) 2026 by Mudlet Developers - mudlet@mudlet.org           *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

/*
 * The main and detached toolbars use the flat-bar recipe in both appearances: left to the
 * platform, a hovered button shows nothing on native macOS and a checked one swallows its menu
 * half. A profile's Lua sheet lands on the same widget, so it has to come after the recipe.
 *
 * Run with: ctest -R MainToolBarStyleTest -V
 */

#include <QAction>
#include <QApplication>
#include <QHoverEvent>
#include <QImage>
#include <QToolBar>
#include <QToolButton>
#include <QtTest/QtTest>

#include "Host.h"
#include "PixelTestHelper.h"
#include "ProfileSessionTestHelper.h"
#include "TDetachedWindow.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

class MainToolBarStyleTest : public QObject
{
    Q_OBJECT

private:
    TestProfile::Session mSession;
    const QString mFirstHostname = qsl("MainToolBarStyle-First");
    const QString mSecondHostname = qsl("MainToolBarStyle-Second");

    // A wash is composited rather than blitted, so it agrees with the token mix to a level or two
    static constexpr int scmInkTolerance = 2;
    // Past the window's width Qt moves the bar's tail into the overflow menu, where nothing can be sampled
    static constexpr int scmWindowWidth = 1600;
    static constexpr int scmWindowHeight = 600;

    static QToolBar* theBar() { return mudlet::self()->findChild<QToolBar*>(qsl("mpMainToolBar")); }

    static QToolButton* buttonNamed(const QString& objectName) { return theBar() ? theBar()->findChild<QToolButton*>(objectName) : nullptr; }

    static QString saidAs(const QColor& colour) { return colour.name(QColor::HexArgb); }

    static QString saidAs(const QPoint& at) { return qsl("%1,%2").arg(QString::number(at.x()), QString::number(at.y())); }

    // Past the rounded corner and above the glyph and the word, where only the fill is drawn
    static QPoint washSpotIn(const QRect& button) { return QPoint(button.left() + uiDesign::scmToolBarButtonRadius + 2, button.top() + 2); }

    // splitButtonMenuHalfStyleSheet() draws the seam on the leading edge of a scmInputDropDownWidth
    // menu half, inside the one-pixel border the bar's rule gives every button
    static int seamColumnOf(const QRect& button) { return button.right() - uiDesign::scmInputDropDownWidth; }

    // QStyleOption::initFrom() takes State_MouseOver from WA_UnderMouse, which QApplication sets
    // only while dispatching a real enter or leave, so the offscreen platform never sets it
    static void hoverOver(QWidget* pWidget, const bool over)
    {
        const QPoint at = pWidget->rect().center();
        if (over) {
            QTest::mouseMove(pWidget, at);
        }
        QCoreApplication::processEvents();
        pWidget->setAttribute(Qt::WA_UnderMouse, over);
        if (over) {
            QEnterEvent entered{QPointF(at), QPointF(at), QPointF(pWidget->mapToGlobal(at))};
            QApplication::sendEvent(pWidget, &entered);
            QHoverEvent hovered(QEvent::HoverEnter, QPointF(at), QPointF(at), QPointF(-1, -1));
            QApplication::sendEvent(pWidget, &hovered);
        } else {
            QEvent left(QEvent::Leave);
            QApplication::sendEvent(pWidget, &left);
            QHoverEvent unhovered(QEvent::HoverLeave, QPointF(-1, -1), QPointF(-1, -1), QPointF(at));
            QApplication::sendEvent(pWidget, &unhovered);
        }
        pWidget->update();
        QCoreApplication::processEvents();
    }

    static QString appearanceSaidAs(const enums::Appearance appearance) { return appearance == enums::Appearance::dark ? qsl("dark") : qsl("light"); }

    void aHoveredButtonIsWashed(const QString& appearance)
    {
        QToolButton* pHovered = buttonNamed(qsl("triggers_action"));
        QToolButton* pResting = buttonNamed(qsl("aliases_action"));
        QVERIFY2(pHovered && pResting, "the bar has no Triggers or Aliases button to point at");
        QVERIFY2(pHovered->isVisible() && pResting->isVisible(), "the window is too narrow for the bar, so the buttons this case reads are in the overflow menu rather than on it");

        hoverOver(pHovered, true);
        const QImage bar = TestPixels::shot(theBar());
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();

        const QColor washed = TestPixels::sampled(bar, washSpotIn(pHovered->geometry()));
        const QColor wanted = TestPixels::composited(tokens.hoverWash, tokens.page);
        const QColor resting = TestPixels::sampled(bar, washSpotIn(pResting->geometry()));
        hoverOver(pHovered, false);

        qInfo().noquote() << qsl("  %1: the hovered button reads %2 against an expected %3, and one at rest %4 against the bar's %5")
                                     .arg(appearance, saidAs(washed), saidAs(wanted), saidAs(resting), saidAs(tokens.page));

        QVERIFY2(TestPixels::readsAs(washed, wanted, scmInkTolerance),
                 qPrintable(qsl("%1: a button under the pointer is not washed in the hover tone: %2 at %3 where the wash over the bar's own surface is %4")
                                    .arg(appearance, saidAs(washed), saidAs(washSpotIn(pHovered->geometry())), saidAs(wanted))));
        QVERIFY2(
                TestPixels::readsAs(resting, tokens.page, scmInkTolerance),
                qPrintable(qsl("%1: a button nothing is pointing at is drawn on something other than the bar's own surface: %2 rather than %3").arg(appearance, saidAs(resting), saidAs(tokens.page))));
    }

    void aSwitchedOnSplitButtonReadsAsTwoAreas(const QString& appearance)
    {
        QToolButton* pButton = buttonNamed(qsl("mute"));
        QAction* pAction = mudlet::self()->findChild<QAction*>(qsl("muteMedia"));
        QVERIFY2(pButton && pAction, "the bar has no Sound button, or no muteMedia action behind it");
        QVERIFY2(pButton->isVisible(), "the window is too narrow for the bar, so the Sound button is in the overflow menu rather than on it");
        QVERIFY2(pButton->popupMode() == QToolButton::MenuButtonPopup, "the Sound button no longer keeps a menu half, so there is nothing here to tell from its body");

        pAction->setChecked(true);
        QCoreApplication::processEvents();
        QVERIFY2(pButton->isChecked(), "checking the mute action did not switch the button it is the default action of on");

        const QImage bar = TestPixels::shot(theBar());
        const uiDesign::ThemeTokens tokens = uiDesign::themeTokens();
        const QRect box = pButton->geometry();

        const QColor body = TestPixels::sampled(bar, washSpotIn(box));
        const QColor wantedBody = TestPixels::composited(tokens.accentWash, tokens.page);

        // The seam is one pixel wide and rounding the sub-control's box can move it a pixel
        const int seamAt = seamColumnOf(box);
        QColor seam;
        int seamFoundAt = -1;
        QStringList aroundTheSeam;
        for (int x = seamAt - 1; x <= seamAt + 1; ++x) {
            const QColor read = TestPixels::sampled(bar, QPoint(x, box.center().y()));
            aroundTheSeam << qsl("%1:%2").arg(QString::number(x), saidAs(read));
            if (seamFoundAt < 0 && TestPixels::readsAs(read, tokens.border, scmInkTolerance)) {
                seam = read;
                seamFoundAt = x;
            }
        }

        pAction->setChecked(false);
        QCoreApplication::processEvents();

        qInfo().noquote() << qsl("  %1: the switched-on Sound button reads %2 against an expected %3, with the seam wanted at x=%4 in %5 and the columns around it %6")
                                     .arg(appearance, saidAs(body), saidAs(wantedBody), QString::number(seamAt), saidAs(tokens.border), aroundTheSeam.join(qsl(", ")));

        QVERIFY2(TestPixels::readsAs(body, wantedBody, scmInkTolerance),
                 qPrintable(qsl("%1: a switched-on button on the bar is not washed in the accent: %2 at %3 where the accent's wash over the bar's surface is %4")
                                    .arg(appearance, saidAs(body), saidAs(washSpotIn(box)), saidAs(wantedBody))));
        QVERIFY2(seamFoundAt >= 0,
                 qPrintable(qsl("%1: the Sound button's menu half is not told from its body at all - the seam the recipe draws in %2 is nowhere within a pixel of x=%3 on a button at %4; the "
                                "columns there read %5")
                                    .arg(appearance, saidAs(tokens.border), QString::number(seamAt), TestPixels::describe(box), aroundTheSeam.join(qsl(", ")))));
        QVERIFY2(!TestPixels::readsAs(seam, wantedBody, scmInkTolerance),
                 qPrintable(qsl("%1: the seam on the Sound button's menu half is the same wash its body is, %2, so the two halves read as one target").arg(appearance, saidAs(seam))));
    }

private slots:
    void initTestCase()
    {
        if (TestProfile::Session::configCannotBeRedirected()) {
            QSKIP(TestProfile::Session::configCannotBeRedirectedReason());
        }

        QVERIFY2(mSession.startMudlet(), qPrintable(mSession.why()));
        mudlet::self()->setInterfaceLanguage(qsl("en_US"));

        // Two of them, because slot_tabDetachRequested() refuses index 0
        QVERIFY2(mSession.openProfile(mFirstHostname), qPrintable(mSession.why()));
        QVERIFY2(mSession.openProfile(mSecondHostname), qPrintable(mSession.why()));

        // With a profile loaded the shipped default hides the bar, and a hidden bar cannot be sampled
        mudlet::self()->setToolBarVisibility(enums::visibleAlways);
        mudlet::self()->resize(scmWindowWidth, scmWindowHeight);
        mudlet::self()->show();
        QVERIFY(QTest::qWaitForWindowExposed(mudlet::self()));
        QCoreApplication::processEvents();

        QVERIFY2(theBar(), "the main window has no toolbar named mpMainToolBar");
        QVERIFY2(theBar()->isVisible() && theBar()->width() > 0, "the main window's toolbar is not on show, so nothing below can be read off a grab of it");
    }

    void cleanupTestCase()
    {
        if (mudlet::self()) {
            mudlet::self()->setAppearance(enums::Appearance::systemSetting);
        }
        mSession.stop();
    }

    void test_theBarCarriesTheRecipeAndTheProfilesSheetComesLast()
    {
        const QString sheet = theBar()->styleSheet();
        QVERIFY2(sheet.contains(qsl("QToolBar#mpMainToolBar {")), qPrintable(qsl("the bar's sheet does not draw the bar itself: %1").arg(sheet.left(200))));
        QVERIFY2(sheet.contains(qsl("QToolButton:hover")), "the bar's sheet says nothing about a button under the pointer, so the platform style draws that");
        QVERIFY2(sheet.contains(qsl("QToolButton:checked")), "the bar's sheet says nothing about a button that is switched on, so Sound and Full Screen are drawn by the platform style");
        QVERIFY2(sheet.contains(qsl("[popupMode=\"1\"]")), "the bar's sheet never picks out the buttons whose trailing half opens a menu");
        QVERIFY2(sheet.contains(qsl("::menu-button")), "the bar's sheet draws no menu half on a split button, so its two click areas read as one");

        Host* pHost = mudlet::self()->getActiveHost();
        QVERIFY2(pHost, "no profile is active, so no profile stylesheet can reach the bar");
        const QString marker = qsl("/* profile-sheet-marker */");
        pHost->setProfileStyleSheet(marker);
        const QString composed = theBar()->styleSheet();
        pHost->setProfileStyleSheet(QString());

        QVERIFY2(composed.endsWith(marker),
                 qPrintable(qsl("a profile's own stylesheet does not come last on the bar, so it cannot override the design's rules on the widget they share: the sheet ends \"%1\"")
                                    .arg(composed.right(120))));
        QVERIFY2(composed.contains(qsl("QToolBar#mpMainToolBar {")), "a profile's stylesheet replaced the design's rules on the bar rather than being composed after them");
    }

    void test_aHoveredButtonIsWashed() { aHoveredButtonIsWashed(qsl("as started")); }

    void test_aSwitchedOnSplitButtonReadsAsTwoAreas() { aSwitchedOnSplitButtonReadsAsTwoAreas(qsl("as started")); }

    void test_bothAppearances()
    {
        for (const enums::Appearance appearance : {enums::Appearance::dark, enums::Appearance::light}) {
            TestPixels::setAppearanceAndWait(appearance);
            aHoveredButtonIsWashed(appearanceSaidAs(appearance));
            if (QTest::currentTestFailed()) {
                break;
            }
            aSwitchedOnSplitButtonReadsAsTwoAreas(appearanceSaidAs(appearance));
            if (QTest::currentTestFailed()) {
                break;
            }
        }
        TestPixels::setAppearanceAndWait(enums::Appearance::systemSetting);
    }

    void test_aDetachedWindowsBarCarriesTheRecipe()
    {
        QVERIFY2(mudlet::self()->getDetachedWindows().isEmpty(), "a detached window was left over from an earlier test");
        mudlet::self()->slot_tabDetachRequested(1, QPoint(200, 200));
        TDetachedWindow* pDetachedWindow = mudlet::self()->getDetachedWindows().value(mSecondHostname);
        QVERIFY2(pDetachedWindow, qPrintable(qsl("detaching tab 1 produced no window for '%1' - the tab order is not what this test assumes").arg(mSecondHostname)));

        QToolBar* pDetachedBar = pDetachedWindow->findChild<QToolBar*>(qsl("detachedMainToolBar"));
        const QString sheet = pDetachedBar ? pDetachedBar->styleSheet() : QString();
        mudlet::self()->slot_tabReattachRequested(mSecondHostname);

        QVERIFY2(pDetachedBar, "the detached window has no toolbar named detachedMainToolBar");
        QVERIFY2(sheet.contains(qsl("QToolBar#detachedMainToolBar {")), qPrintable(qsl("a detached window's bar is left for the platform style to draw: %1").arg(sheet.left(200))));
        QVERIFY2(sheet.contains(qsl("QToolButton:checked")), "a detached window's bar says nothing about a button that is switched on, so its Sound button is drawn by the platform style");
    }
};

#include "MainToolBarStyleTest.moc"
MUDLET_GROUPED_TEST_MAIN(MainToolBarStyleTest)
