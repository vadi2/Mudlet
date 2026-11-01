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
 * Each tree's heading row must carry its sidebar row's glyph in the same tint.
 * Compared pixel for pixel rather than by file name because the tint is applied
 * at runtime: the right file in the wrong colour is still wrong.
 *
 * Run with: ctest -R EditorTreeHeadingIconTest -V
 */

#include <QImage>
#include <QListWidget>
#include <QPalette>
#include <QTreeWidget>
#include <QtTest/QtTest>
#include <chrono>

#include "EditorCommand.h"
#include "Host.h"
#include "HostDialogs.h"
#include "PixelTestHelper.h"
#include "ProfileSessionTestHelper.h"
#include "dlgTriggerEditor.h"
#include "mudlet.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class EditorTreeHeadingIconTest : public QObject
{
    Q_OBJECT

private:
    TestProfile::Session mSession;
    dlgTriggerEditor* mpEditor = nullptr;
    Host* mpHost = nullptr;
    const QString mProfileName = qsl("EditorTreeHeadingIcon-Test-Profile");

    // The size the window draws both at, so a failure is one a user could see
    static constexpr int scmComparisonSize = 18;

    // dlgTriggerEditor's role on a sidebar row; read rather than the row's
    // position, so a row added to the list cannot re-pair the seven
    static constexpr int scmRole_editorSidebarView = Qt::UserRole + 1;

    QList<QPair<QString, QPair<QTreeWidgetItem*, EditorViewType>>> headings() const
    {
        return {{qsl("Triggers"), {mpEditor->mpTriggerBaseItem, EditorViewType::cmTriggerView}},
                {qsl("Aliases"), {mpEditor->mpAliasBaseItem, EditorViewType::cmAliasView}},
                {qsl("Scripts"), {mpEditor->mpScriptsBaseItem, EditorViewType::cmScriptView}},
                {qsl("Timers"), {mpEditor->mpTimerBaseItem, EditorViewType::cmTimerView}},
                {qsl("Keys"), {mpEditor->mpKeyBaseItem, EditorViewType::cmKeysView}},
                {qsl("Buttons"), {mpEditor->mpActionBaseItem, EditorViewType::cmActionView}},
                {qsl("Variables"), {mpEditor->mpVarBaseItem, EditorViewType::cmVarsView}}};
    }

    QListWidgetItem* sidebarRowFor(const EditorViewType view) const
    {
        QListWidget* pSidebar = mpEditor->mpListWidget_editorSidebar;
        for (int row = 0, rows = pSidebar ? pSidebar->count() : 0; row < rows; ++row) {
            QListWidgetItem* pItem = pSidebar->item(row);
            if (static_cast<EditorViewType>(pItem->data(scmRole_editorSidebarView).toInt()) == view) {
                return pItem;
            }
        }
        return nullptr;
    }

private slots:
    void initTestCase()
    {
        if (TestProfile::Session::configCannotBeRedirected()) {
            QSKIP(TestProfile::Session::configCannotBeRedirectedReason());
        }

        QVERIFY2(mSession.start(mProfileName), qPrintable(mSession.why()));
        mpHost = mSession.host();

        mudlet::self()->slot_showTriggerDialog();
        QTest::qWait(100ms);
        mpEditor = HostDialogs::of(mpHost).mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");
        mpEditor->resize(1000, 800);
        // The variables tree and its heading row are built only when first shown
        mpEditor->slot_showVariables();
        QTest::qWait(100ms);
        mpEditor->slot_showTriggers();
        QTest::qWait(50ms);

        for (const auto& heading : headings()) {
            QVERIFY2(heading.second.first != nullptr, qPrintable(qsl("The %1 tree has no heading row to read").arg(heading.first)));
            QVERIFY2(sidebarRowFor(heading.second.second) != nullptr, qPrintable(qsl("The sidebar has no row for %1").arg(heading.first)));
        }
    }

    void cleanupTestCase()
    {
        mpEditor = nullptr;
        mpHost = nullptr;
        mSession.stop();
    }

    void test_everyHeadingCarriesItsSidebarRowsGlyph()
    {
        QStringList mismatched;
        QStringList measured;
        for (const auto& heading : headings()) {
            const QImage onTheHeading = TestPixels::drawnAt(heading.second.first->icon(0), scmComparisonSize);
            const QImage inTheSidebar = TestPixels::drawnAt(sidebarRowFor(heading.second.second)->icon(), scmComparisonSize);
            measured << qsl("%1 %2").arg(heading.first, TestPixels::inkOf(onTheHeading));
            if (onTheHeading.isNull() || inTheSidebar.isNull()) {
                mismatched << qsl("%1: one of the two has no picture at all").arg(heading.first);
                continue;
            }
            if (onTheHeading != inTheSidebar) {
                mismatched << qsl("%1: the heading is inked %2 and the sidebar row %3").arg(heading.first, TestPixels::inkOf(onTheHeading), TestPixels::inkOf(inTheSidebar));
            }
        }
        qInfo().noquote() << qsl("  headings at %1px: %2").arg(QString::number(scmComparisonSize), measured.join(qsl(", ")));

        QVERIFY2(mismatched.isEmpty(), qPrintable(qsl("a tree's heading row does not carry its sidebar row's glyph: %1").arg(mismatched.join(qsl("; ")))));
    }

    // Nothing re-derives a QIcon once it is set on a tree item, so a heading the
    // restyle misses keeps the old theme's ink for the rest of the session
    void test_aThemeChangeRetintsTheHeadingsWithTheSidebar()
    {
        const QPalette savedPalette = QApplication::palette();
        const QImage before = TestPixels::drawnAt(mpEditor->mpTriggerBaseItem->icon(0), scmComparisonSize);

        QPalette movedPalette(savedPalette);
        movedPalette.setColor(QPalette::Window, savedPalette.color(QPalette::Window).lightness() < 128 ? QColor(0xec, 0xec, 0xec) : QColor(0x2c, 0x2c, 0x2e));
        movedPalette.setColor(QPalette::WindowText, savedPalette.color(QPalette::Window).lightness() < 128 ? QColor(Qt::black) : QColor(Qt::white));
        QApplication::setPalette(movedPalette);
        QTest::qWait(100ms);

        const QImage after = TestPixels::drawnAt(mpEditor->mpTriggerBaseItem->icon(0), scmComparisonSize);
        qInfo().noquote() << qsl("  the Triggers heading was inked %1 and is now %2").arg(TestPixels::inkOf(before), TestPixels::inkOf(after));

        QStringList mismatched;
        for (const auto& heading : headings()) {
            const QImage onTheHeading = TestPixels::drawnAt(heading.second.first->icon(0), scmComparisonSize);
            const QImage inTheSidebar = TestPixels::drawnAt(sidebarRowFor(heading.second.second)->icon(), scmComparisonSize);
            if (onTheHeading != inTheSidebar) {
                mismatched << qsl("%1: the heading is inked %2 and the sidebar row %3").arg(heading.first, TestPixels::inkOf(onTheHeading), TestPixels::inkOf(inTheSidebar));
            }
        }

        QApplication::setPalette(savedPalette);
        QTest::qWait(100ms);

        // Checked before the pair: a restyle that did nothing would leave the
        // two agreeing
        QVERIFY2(before != after, qPrintable(qsl("the theme moved and the heading's picture did not: it is still inked %1").arg(TestPixels::inkOf(after))));
        QVERIFY2(mismatched.isEmpty(), qPrintable(qsl("a theme change reached the sidebar and left a heading behind: %1").arg(mismatched.join(qsl("; ")))));
    }
};

#include "EditorTreeHeadingIconTest.moc"
MUDLET_GROUPED_TEST_MAIN(EditorTreeHeadingIconTest)
