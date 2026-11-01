/***************************************************************************
 *   Copyright (C) 2026 by Vadim Peretokin - vadim.peretokin@mudlet.org    *
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
 * Guards that every word on a designed window clears WCAG contrast against what
 * is painted behind it, in both appearances: an ink and surface that do not pair
 * up are unreadable in one theme only, and nothing shows it until somebody switches.
 *
 * Run with: ctest -R ReadabilityAuditTest -V
 */

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QCheckBox>
#include <QComboBox>
#include <QGroupBox>
#include <QHash>
#include <QImage>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QTreeWidget>
#include <QtTest/QtTest>
#include <chrono>

#include "ChipRow.h"
#include "Host.h"
#include "HostDialogs.h"
#include "MudletApp.h"
#include "PixelTestHelper.h"
#include "ProfileSessionTestHelper.h"
#include "ScriptUnit.h"
#include "TScript.h"
#include "TTrigger.h"
#include "TriggerUnit.h"
#include "dlgActionMainArea.h"
#include "dlgKeysMainArea.h"
#include "dlgAboutDialog.h"
#include "dlgConnectionProfiles.h"
#include "dlgNotepad.h"
#include "dlgPackageExporter.h"
#include "dlgPackageManager.h"
#include "dlgProfilePreferences.h"
#include "dlgScriptsMainArea.h"
#include "dlgTimersMainArea.h"
#include "dlgTriggerEditor.h"
#include "dlgTriggerPatternEdit.h"
#include "dlgTriggersMainArea.h"
#include "dlgVarsMainArea.h"
#include "mudlet.h"
#include "uiDesign.h"

#include "GroupedTest.h"

using namespace std::chrono_literals;

class ReadabilityAuditTest : public QObject
{
    Q_OBJECT

private:
    TestProfile::Session mSession;
    dlgTriggerEditor* mpEditor = nullptr;
    dlgProfilePreferences* mpPreferences = nullptr;
    dlgAboutDialog* mpAbout = nullptr;
    dlgConnectionProfiles* mpConnection = nullptr;
    dlgNotepad* mpNotepad = nullptr;
    dlgPackageManager* mpPackages = nullptr;
    dlgPackageExporter* mpExporter = nullptr;
    Host* mpHost = nullptr;
    QTreeWidgetItem* mpKeyItem = nullptr;
    QTreeWidgetItem* mpTimerItem = nullptr;
    QTreeWidgetItem* mpActionItem = nullptr;

    // Looked up rather than kept: every entry into the Variables view rebuilds
    // the tree, so a row held from one case dangles by the next
    QTreeWidgetItem* auditedVariableRow() const
    {
        QTreeWidgetItem* pBase = mpEditor->treeWidget_variables->topLevelItem(0);
        for (int i = 0; pBase && i < pBase->childCount(); ++i) {
            if (pBase->child(i)->text(0) == qsl("auditedVariable")) {
                return pBase->child(i);
            }
        }
        return nullptr;
    }
    const QString mProfileName = qsl("ReadabilityAudit-Test-Profile");

    static constexpr qreal scmTextFloor = uiDesign::scmTextMinimumRatio;
    static constexpr qreal scmQuietFloor = uiDesign::scmQuietMinimumRatio;
    // Pinned below the measured 41 (editor), 25 (settings) and 37 (About), so a
    // walk that silently stopped finding widgets cannot pass as a clean one
    static constexpr int scmLeastAuditedInTheEditor = 30;
    static constexpr int scmLeastAuditedInTheSettings = 18;
    // Each form alone, measured at 6 (scripts), 7 (keys), 14 (timers),
    // 7 (variables) and 8 (buttons)
    static constexpr int scmLeastAuditedOnTheScriptsForm = 6;
    static constexpr int scmLeastAuditedOnTheKeysForm = 6;
    static constexpr int scmLeastAuditedOnTheTimersForm = 12;
    static constexpr int scmLeastAuditedOnTheVariablesForm = 6;
    static constexpr int scmLeastAuditedOnTheButtonsForm = 6;
    static constexpr int scmLeastAuditedInTheAbout = 28;
    // Measured at 7 and 5: on the Options tab the character name and password
    // have no placeholder, and with no saved sessions the history picker is empty
    static constexpr int scmLeastAuditedOnTheConnectTab = 6;
    static constexpr int scmLeastAuditedOnTheOptionsTab = 4;
    // Measured at 2 (Copy, New) and 1 (Cancel): the selected profile is the one
    // already open, so Remove, Offline and Connect are disabled and skipped
    static constexpr int scmLeastAuditedOnTheAdminRow = 2;
    static constexpr int scmLeastAuditedOnTheButtonRow = 1;
    // Measured at 6: Send selection is disabled with nothing selected, Stop and
    // the progress are hidden with nothing sending, and icon-only buttons have no words
    static constexpr int scmLeastAuditedInTheNotepad = 6;
    // Measured at 4 and 5: Install is hidden in the Installed view, and Website
    // and Report for a package no repository index lists - this profile has none
    static constexpr int scmLeastAuditedOnThePackageList = 3;
    static constexpr int scmLeastAuditedOnThePackageDetails = 4;
    // Measured at 4, 23 and 4; Export is disabled while the package is unnamed,
    // and the walk skips a disabled push button
    static constexpr int scmLeastAuditedOnTheExporterSelection = 3;
    static constexpr int scmLeastAuditedOnTheExporterDetails = 18;
    static constexpr int scmLeastAuditedOnTheExporterFoot = 3;

    // A colour well's fill is a value rather than a surface, and its words are
    // chosen against that fill by generateButtonStyleSheet(), so it is not audited
    static const QList<QRegularExpression>& wellNames()
    {
        static const QList<QRegularExpression> names{QRegularExpression(qsl("colou?r"), QRegularExpression::CaseInsensitiveOption),
                                                     QRegularExpression(qsl("^pushButton_L?(black|red|green|yellow|blue|magenta|cyan|white)(_2)?$")),
                                                     QRegularExpression(qsl("mapInfoBg"))};
        return names;
    }

    static bool aColourWell(const QWidget* pWidget)
    {
        const QString name = pWidget->objectName();
        return std::any_of(wellNames().cbegin(), wellNames().cend(), [&name](const QRegularExpression& pattern) {
            return pattern.match(name).hasMatch();
        });
    }

    void writeFreshEditorThemesFile()
    {
        const QString file = MudletApp::getMudletPath(enums::editorWidgetThemeJsonFile);
        QVERIFY(QDir().mkpath(QFileInfo(file).absolutePath()));
        QFile themes(file);
        QVERIFY(themes.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QVERIFY(themes.write("[]") == 2);
    }

    // The prompt row has regressed before: with no Go-Ahead from the game its
    // words are disabled, and the design inks those in a tone of its own
    void buildThreePatternRows()
    {
        mpEditor->showPatternItems(3);
        const QList<int> types{REGEX_PERL, REGEX_PROMPT, REGEX_COLOR_PATTERN};
        for (int row = 0; row < types.size(); ++row) {
            dlgTriggerPatternEdit* pRow = mpEditor->mTriggerPatternEdit.at(row);
            pRow->comboBox_patternType->setCurrentIndex(types.at(row));
        }
        mpEditor->mTriggerPatternEdit.at(0)->singleLineTextEdit_pattern->setPlainText(qsl("^You (?:see|hear) (.+)$"));
        QCoreApplication::processEvents();

        // Pinned, so a form that went back to one row cannot leave the walk nothing to read
        QVERIFY2(mpEditor->mVisiblePatternCount == types.size(), qPrintable(qsl("the trigger form shows %1 pattern rows rather than three").arg(QString::number(mpEditor->mVisiblePatternCount))));
        QVERIFY2(mpEditor->mTriggerPatternEdit.at(0)->singleLineTextEdit_pattern->isVisible(), "the first row is not a pattern typed into a field");
        QVERIFY2(mpEditor->mTriggerPatternEdit.at(1)->label_prompt->isVisible(), "the second row does not show the words that stand in for the prompt line");
        QVERIFY2(!mpEditor->mTriggerPatternEdit.at(1)->label_prompt->isEnabled(), "the prompt row is available, so the tone an unavailable word is written in is not on show");
        QVERIFY2(mpEditor->mTriggerPatternEdit.at(2)->pushButton_fgColor->isVisible(), "the third row does not show the wells a colour trigger is set from");
    }

    void openPreferences()
    {
        mpPreferences = new dlgProfilePreferences(mudlet::self(), mpHost);
        // As mudlet::showOptionsDialog() does on every show
        mpPreferences->setStyleSheet(mpHost->mProfileStyleSheet);
        mpPreferences->resize(1060, 760);
        mpPreferences->show();
        QVERIFY(QTest::qWaitForWindowExposed(mpPreferences));
    }

    void openAbout()
    {
        mpAbout = new dlgAboutDialog(mudlet::self());
        mpAbout->resize(1080, 680);
        mpAbout->show();
        QVERIFY(QTest::qWaitForWindowExposed(mpAbout));
    }

    // A profile is selected so that the fields carry text for the walk to read
    void openConnection()
    {
        mpConnection = new dlgConnectionProfiles(mudlet::self());
        mpConnection->fillout_form();
        const auto items = mpConnection->findData(*mpConnection->listWidget_profiles, mProfileName, dlgConnectionProfiles::csmNameRole);
        QVERIFY2(!items.isEmpty(), "the profile this walks is not in the connection dialog's games list");
        mpConnection->listWidget_profiles->setCurrentItem(items.first());
        QCoreApplication::processEvents();
        mpConnection->resize(880, 560);
        mpConnection->show();
        QVERIFY(QTest::qWaitForWindowExposed(mpConnection));
    }

    void openNotepad()
    {
        mudlet::self()->slot_notes();
        mpNotepad = HostDialogs::of(mpHost).mpNotePad;
        QVERIFY2(mpNotepad, "the notepad did not open at all");
        // Wide enough for the send strip: its trailing warning is cut to the room the bar has
        mpNotepad->resize(1100, 500);
        mpNotepad->show();
        QVERIFY(QTest::qWaitForWindowExposed(mpNotepad));

        // Long enough, with no prefix, for the strip's warning: the window's one
        // word in a state colour rather than the chrome tone
        if (auto* pNote = qobject_cast<QPlainTextEdit*>(mpNotepad->tabWidget->currentWidget())) {
            QStringList lines;
            for (int line = 1; line <= 12; ++line) {
                lines << qsl("line %1").arg(QString::number(line));
            }
            pNote->setPlainText(lines.join(QChar::LineFeed));
        }
        QCoreApplication::processEvents();
        QVERIFY2(mpNotepad->action_sendAll->isVisible(), "the notepad is not showing the controls a note is sent from");
        QWidget* pNoPrefixNote = mpNotepad->findChild<QWidget*>(qsl("notepadNoPrefixNote"));
        QVERIFY2(pNoPrefixNote && pNoPrefixNote->isVisible(), "the notepad is not showing the warning a long note with no prefix carries");
        QWidget* pFindBar = mpNotepad->findChild<QWidget*>(qsl("notepadFindBar"));
        QVERIFY2(pFindBar, "the notepad has no 'notepadFindBar'");
        pFindBar->show();
        QCoreApplication::processEvents();
    }

    void openPackageManager()
    {
        // Written onto the host rather than installed: what is read is how the window draws it
        const QString package = qsl("readability-audit-package");
        mpHost->mInstalledPackages << package;
        mpHost->mPackageInfo.insert(package,
                                    QMap<QString, QString>{{qsl("mpackage"), package},
                                                           {qsl("title"), qsl("What this package is for, in one line")},
                                                           {qsl("author"), qsl("An Audited Author")},
                                                           {qsl("version"), qsl("2.0.1")},
                                                           {qsl("description"), qsl("The notes this package ships with.\n")}});

        mpPackages = new dlgPackageManager(nullptr, mpHost);
        QVERIFY2(mpPackages, "the package manager did not open at all");
        mpPackages->resize(900, 600);
        mpPackages->show();
        QVERIFY(QTest::qWaitForWindowExposed(mpPackages));
        mpPackages->packageList->setCurrentRow(0);
        QCoreApplication::processEvents();
        QVERIFY2(mpPackages->packageList->count() > 0, "the package manager lists no installed packages, so its details column has nothing on it to read");
        QVERIFY2(!mpPackages->label_packageName->text().isEmpty(), "no package is chosen, so the details column is empty");
    }

    // So the tree of things to export has rows to read
    void openPackageExporter()
    {
        auto* pFolder = new TTrigger(nullptr, mpHost);
        pFolder->setName(qsl("audited exporter folder"));
        pFolder->setIsFolder(true);
        mpHost->getTriggerUnit()->registerTrigger(pFolder);
        auto* pChild = new TTrigger(pFolder, mpHost);
        pChild->setName(qsl("audited exporter trigger"));
        pChild->setRegexCodeList(QStringList{qsl("audited")}, QList<int>{REGEX_SUBSTRING});
        mpHost->getTriggerUnit()->registerTrigger(pChild);

        // Parented to nothing rather than the main window: this fixture has none on screen
        mpExporter = new dlgPackageExporter(nullptr, mpHost);
        QVERIFY2(mpExporter, "the package exporter did not open at all");
        mpExporter->show();
        QVERIFY(QTest::qWaitForWindowExposed(mpExporter));
        QCoreApplication::processEvents();
        QVERIFY2(mpExporter->findChild<QWidget*>(qsl("exporterSelectionColumn")), "the package exporter built no selection column, so there is nothing to walk");
        QVERIFY2(mpExporter->findChild<QWidget*>(qsl("exporterDetailsColumn")), "the package exporter built no details column, so there is nothing to walk");
        QVERIFY2(mpExporter->findChild<QWidget*>(qsl("exporterFoot")), "the package exporter built no foot, so there is nothing to walk");
    }

    // Through the settings dialog's control, the path a player takes; each window
    // restyles off the mudlet::signal_appearanceChanged it sets off. Painting
    // settles a widget's palette, and offscreen nothing paints unless grabbed.
    void setAppearance(const enums::Appearance appearance)
    {
        // The dialog restyles only when the control moves, so a control already on
        // the wanted appearance (the machine's own, say) is moved away and back
        if (mpPreferences->comboBox_appearance->currentIndex() == appearance) {
            mpPreferences->comboBox_appearance->setCurrentIndex(appearance == enums::Appearance::dark ? enums::Appearance::light : enums::Appearance::dark);
            QCoreApplication::processEvents();
        }
        mpPreferences->comboBox_appearance->setCurrentIndex(appearance);
        QCoreApplication::processEvents();
        QTest::qWait(50ms);
        QCoreApplication::processEvents();
        mpEditor->grab();
        mpPreferences->grab();
        mpAbout->grab();
        mpConnection->grab();
        mpNotepad->grab();
        if (mpPackages) {
            mpPackages->grab();
        }
        if (mpExporter) {
            mpExporter->grab();
        }
    }

    // Clipped by every ancestor: a control scrolled out of its column is still
    // visible to Qt and maps to a coordinate the window paints something else at
    static QRect paintedRectIn(const QWidget* pWidget, const QRect& local, const QWidget* pWindow)
    {
        QRect rect = local.intersected(pWidget->rect());
        if (rect.isEmpty()) {
            return {};
        }
        for (const QWidget* pAt = pWidget; pAt != pWindow;) {
            const QWidget* pParent = pAt->parentWidget();
            if (!pParent) {
                return {};
            }
            rect.translate(pAt->pos());
            rect &= pParent->rect();
            if (rect.isEmpty()) {
                return {};
            }
            pAt = pParent;
        }
        return rect;
    }

    // The commonest pixel: words are always the minority, and every surface here is a flat fill
    static QColor paintedBehind(const QImage& shot, const QRect& rect)
    {
        QHash<QRgb, int> histogram;
        for (int y = rect.top(); y <= rect.bottom(); ++y) {
            for (int x = rect.left(); x <= rect.right(); ++x) {
                ++histogram[shot.pixel(x, y)];
            }
        }
        QRgb commonest = 0;
        int seen = 0;
        for (auto entry = histogram.cbegin(); entry != histogram.cend(); ++entry) {
            if (entry.value() > seen) {
                seen = entry.value();
                commonest = entry.key();
            }
        }
        return QColor(commonest);
    }

    static QString wordsOf(const QWidget* pWidget)
    {
        if (const auto* pLabel = qobject_cast<const QLabel*>(pWidget)) {
            return pLabel->text();
        }
        if (const auto* pGroup = qobject_cast<const QGroupBox*>(pWidget)) {
            return pGroup->title();
        }
        if (const auto* pButton = qobject_cast<const QAbstractButton*>(pWidget)) {
            return pButton->text();
        }
        if (const auto* pCombo = qobject_cast<const QComboBox*>(pWidget)) {
            return pCombo->currentText();
        }
        if (const auto* pField = qobject_cast<const QLineEdit*>(pWidget)) {
            return pField->text();
        }
        return QString();
    }

    struct Reading
    {
        QString what;
        QColor ink;
        qreal floor = scmTextFloor;
        // Set where the ink is not drawn on the widget itself: a view paints its
        // rows on its viewport
        const QWidget* pSurface = nullptr;
        QRect where;
    };

    static QList<Reading> readingsOf(const QWidget* pWidget)
    {
        const QPalette::ColorGroup group = pWidget->isEnabled() ? QPalette::Active : QPalette::Disabled;
        const qreal floor = pWidget->isEnabled() ? scmTextFloor : scmQuietFloor;

        if (const auto* pView = qobject_cast<const QAbstractItemView*>(pWidget)) {
            const QWidget* pViewport = pView->viewport();
            QList<Reading> readings{{qsl("its rows"), pView->palette().color(group, QPalette::Text), floor, pViewport, pViewport->rect()}};
            if (pView->selectionModel() && pView->selectionModel()->hasSelection()) {
                // A selection is drawn on its own fill, the one surface in a view
                // that is not the viewport's
                const QRect row = pView->visualRect(pView->selectionModel()->selectedIndexes().constFirst());
                if (!row.isEmpty()) {
                    readings.append({qsl("its chosen row"), pView->palette().color(group, QPalette::HighlightedText), floor, pViewport, row});
                }
            }
            return readings;
        }

        if (const auto* pField = qobject_cast<const QLineEdit*>(pWidget); pField && pField->text().isEmpty()) {
            // A placeholder is not text yet, so it is held to the quiet floor
            return pField->placeholderText().isEmpty() ? QList<Reading>{} : QList<Reading>{{qsl("its placeholder"), pField->palette().color(group, QPalette::PlaceholderText), scmQuietFloor}};
        }

        if (wordsOf(pWidget).isEmpty()) {
            return {};
        }
        // A sheet's "color:" reaches the palette through QStyleSheetStyle::polish(),
        // as test_aStyleSheetsColourReachesTheWidgetsPalette proves
        return {{qsl("its text"), pWidget->palette().color(group, pWidget->foregroundRole()), floor}};
    }

    static bool auditable(const QWidget* pWidget)
    {
        if (qobject_cast<const QAbstractItemView*>(pWidget)) {
            return true;
        }
        return qobject_cast<const QLabel*>(pWidget) || qobject_cast<const QAbstractButton*>(pWidget) || qobject_cast<const QComboBox*>(pWidget) || qobject_cast<const QLineEdit*>(pWidget)
               || qobject_cast<const QGroupBox*>(pWidget);
    }

    // A push button keeps the platform's bevel, whose disabled grey is within
    // 2.1:1 of the quiet ink on macOS; WCAG asks nothing of an inactive component
    static bool aDisabledPushButton(const QWidget* pWidget) { return !pWidget->isEnabled() && (qobject_cast<const QPushButton*>(pWidget) || qobject_cast<const QToolButton*>(pWidget)); }

    // Edbee draws itself from a syntax theme with a background of its own, so
    // neither the ink nor the surface here is the design's
    static bool insideTheCodePane(const QWidget* pWidget)
    {
        for (const QWidget* pAncestor = pWidget; pAncestor; pAncestor = pAncestor->parentWidget()) {
            if (QString::fromLatin1(pAncestor->metaObject()->className()).contains(qsl("edbee"), Qt::CaseInsensitive)) {
                return true;
            }
        }
        return false;
    }

    int auditWindow(QWidget* pWindow, const QString& windowName, const QString& appearance, QStringList& failures) const { return auditWithin(pWindow, pWindow, windowName, appearance, failures); }

    // The whole window is grabbed, since what is behind a form is painted by what
    // is under it, but only pRoot's widgets are read and counted
    int auditWithin(QWidget* pWindow, QWidget* pRoot, const QString& windowName, const QString& appearance, QStringList& failures) const
    {
        const QImage shot = pWindow->grab().toImage();
        int audited = 0;
        for (QWidget* pWidget : pRoot->findChildren<QWidget*>()) {
            if (!pWidget->isVisible() || pWidget->rect().isEmpty() || !auditable(pWidget) || aColourWell(pWidget) || insideTheCodePane(pWidget) || aDisabledPushButton(pWidget)) {
                continue;
            }
            if (paintedRectIn(pWidget, pWidget->rect(), pWindow).isEmpty()) {
                continue;
            }

            for (const Reading& reading : readingsOf(pWidget)) {
                if (!reading.ink.isValid()) {
                    continue;
                }
                const QWidget* pOn = reading.pSurface ? reading.pSurface : pWidget;
                const QRect painted = paintedRectIn(pOn, reading.where.isNull() ? pOn->rect() : reading.where, pWindow).intersected(shot.rect());
                if (painted.width() < 2 || painted.height() < 2) {
                    continue;
                }
                const QColor surface = paintedBehind(shot, painted);
                ++audited;
                const QColor ink = TestPixels::composited(reading.ink, surface);
                const qreal ratio = uiDesign::contrastRatio(ink, surface);
                if (ratio >= reading.floor) {
                    continue;
                }
                failures << qsl("%1 (%2) in %3, %4: %5 %6 on %7 = %8:1, needs %9%10")
                                    .arg(pWidget->objectName().isEmpty() ? qsl("<unnamed>") : pWidget->objectName(),
                                         QString::fromLatin1(pWidget->metaObject()->className()),
                                         windowName,
                                         appearance,
                                         reading.what,
                                         ink.name(),
                                         surface.name(),
                                         QString::number(ratio, 'f', 2),
                                         QString::number(reading.floor, 'f', 1),
                                         // A label built at runtime carries no object name, so what it
                                         // says is the only thing that identifies it
                                         wordsOf(pWidget).isEmpty() ? QString() : qsl(" - \"%1\"").arg(wordsOf(pWidget).simplified().left(60)));
            }
        }
        return audited;
    }

private slots:
    void initTestCase()
    {
        if (TestProfile::Session::configCannotBeRedirected()) {
            QSKIP(TestProfile::Session::configCannotBeRedirectedReason());
        }

        QVERIFY2(mSession.startMudlet(), qPrintable(mSession.why()));
        writeFreshEditorThemesFile();
        QVERIFY2(mSession.openProfile(mProfileName), qPrintable(mSession.why()));
        mpHost = mSession.host();

        mudlet::self()->slot_showTriggerDialog();
        QTest::qWait(100ms);
        mpEditor = HostDialogs::of(mpHost).mpEditorDialog;
        QVERIFY2(mpEditor != nullptr, "Editor dialog should be created");
        mpEditor->resize(1100, 800);

        // Only a script's events are drawn as chips, Mudlet's own inked apart from
        // the script's. Made before the trigger, so the editor settles on that view.
        mpEditor->slot_showScripts();
        mpEditor->addScript(false);
        QTest::qWait(100ms);
        QVERIFY2(mpEditor->mpCurrentScriptItem != nullptr, "addScript() left no current script item");
        TScript* pScript = mpHost->getScriptUnit()->getScript(mpEditor->mpCurrentScriptItem->data(0, Qt::UserRole).toInt());
        QVERIFY2(pScript != nullptr, "the new script is not in the script unit");
        pScript->setEventHandlerList({qsl("sysConnectionEvent"), qsl("MyEvent")});
        mpEditor->slot_scriptsSelected(mpEditor->mpCurrentScriptItem);
        QTest::qWait(100ms);

        // A key with no keystroke, so its form shows the placeholder and the hint
        mpEditor->slot_showKeys();
        mpEditor->addKey(false);
        QTest::qWait(100ms);
        mpKeyItem = mpEditor->mpCurrentKeyItem;
        QVERIFY2(mpKeyItem != nullptr, "addKey() left no current key item");

        mpEditor->slot_showTimers();
        mpEditor->addTimer(false);
        QTest::qWait(100ms);
        mpTimerItem = mpEditor->mpCurrentTimerItem;
        QVERIFY2(mpTimerItem != nullptr, "addTimer() left no current timer item");

        mpEditor->slot_showActions();
        mpEditor->addAction(false);
        QTest::qWait(100ms);
        mpActionItem = mpEditor->mpCurrentActionItem;
        QVERIFY2(mpActionItem != nullptr, "addAction() left no current button");

        // A variable of the profile's own: a root row shows a notice instead of the form
        QVERIFY2(mpHost->mLuaInterpreter.compileAndExecuteScript(qsl("auditedVariable = \"north\"")), "the variable the variables form is walked on could not be made");
        mpEditor->slot_showVariables();
        QTest::qWait(100ms);
        QVERIFY2(auditedVariableRow() != nullptr, "the Variables view did not show the variable this walks");

        mpEditor->slot_showTriggers();
        mpEditor->addTrigger(false);
        QTest::qWait(100ms);
        buildThreePatternRows();
        // The options strip opens closed; a sound file and a highlight colour give
        // its two switches something beside them to read
        mpEditor->setTriggerOptionsShown(true);
        mpEditor->showTriggerSoundFile(qsl("/tmp/ReadabilityAuditTest.wav"));
        mpEditor->mpTriggersMainArea->checkBox_soundTrigger->setChecked(true);
        mpEditor->mpTriggersMainArea->checkBox_triggerColorizer->setChecked(true);
        QTest::qWait(100ms);

        openPreferences();
        openAbout();
        openConnection();
        openNotepad();
        openPackageManager();
        openPackageExporter();

        // A failed compile puts up the code pane's note: the editor's one word in
        // a state colour rather than the shell's ink
        mpEditor->mpSourceEditorEdbee->textDocument()->setText(qsl("local a = 1\nlocal b = 1 +* 2\n"));
        mpEditor->slot_saveEdits();
        QTest::qWait(50ms);
        QWidget* pNote = mpEditor->findChild<QWidget*>(qsl("editorCompileNote"));
        QVERIFY2(pNote != nullptr && pNote->isVisible(), "the heading over the code pane carries no compile note, so the audit below does not cover one");
    }

    void cleanupTestCase()
    {
        mpNotepad = nullptr;
        delete mpExporter;
        mpExporter = nullptr;
        delete mpPackages;
        mpPackages = nullptr;
        delete mpConnection;
        mpConnection = nullptr;
        delete mpAbout;
        mpAbout = nullptr;
        delete mpPreferences;
        mpPreferences = nullptr;
        mpEditor = nullptr;
        mpHost = nullptr;
        if (mudlet::self()) {
            mudlet::self()->setAppearance(enums::Appearance::systemSetting);
        }
        mSession.stop();
    }

    // The premise of the audit, which reads palettes rather than sheets; the shell sheet colours this label by name
    void test_aStyleSheetsColourReachesTheWidgetsPalette()
    {
        QLabel* pLabel = mpEditor->mpLabel_statusCounts;
        QVERIFY2(pLabel != nullptr, "the editor's status bar carries no counts label, so there is nothing here the shell sheet colours by name");

        QMap<QString, QColor> answered;
        for (const auto& appearance : QList<QPair<QString, enums::Appearance>>{{qsl("dark"), enums::Appearance::dark}, {qsl("light"), enums::Appearance::light}}) {
            setAppearance(appearance.second);
            const QColor asked = uiDesign::themeTokens().mutedText;
            const QColor read = pLabel->palette().color(pLabel->foregroundRole());
            qInfo().noquote() << qsl("  %1: the sheet asks for %2, the palette answers %3").arg(appearance.first, asked.name(), read.name());
            QVERIFY2(read.rgb() == asked.rgb(),
                     qPrintable(qsl("on the %1 appearance the editor's sheet writes the status bar in %2, but the label's palette answers %3 - a sheet's colour is not reaching the palette this "
                                    "whole test reads")
                                        .arg(appearance.first, asked.name(), read.name())));
            answered.insert(appearance.first, read);
        }

        QVERIFY2(answered.value(qsl("dark")).rgb() != answered.value(qsl("light")).rgb(),
                 qPrintable(qsl("both appearances read %1 back off the label, so the palette is answering something the appearance does not move and the reading above would hold whatever the "
                                "sheet said")
                                    .arg(answered.value(qsl("dark")).name())));
    }

    // One list at the end: stopping at the first failure would take a run per failure to clear
    void test_everyWordInBothWindowsIsReadableInBothAppearances()
    {
        QStringList failures;
        QStringList counts;
        for (const auto& appearance : QList<QPair<QString, enums::Appearance>>{{qsl("dark"), enums::Appearance::dark}, {qsl("light"), enums::Appearance::light}}) {
            setAppearance(appearance.second);
            const int inTheEditor = auditWindow(mpEditor, qsl("the editor"), appearance.first, failures);
            const int inTheSettings = auditWindow(mpPreferences, qsl("the settings dialog"), appearance.first, failures);
            const int inTheAbout = auditWindow(mpAbout, qsl("the About dialog"), appearance.first, failures);
            counts << qsl("%1: %2 in the editor, %3 in the settings dialog, %4 in the About dialog")
                              .arg(appearance.first, QString::number(inTheEditor), QString::number(inTheSettings), QString::number(inTheAbout));
            QVERIFY2(inTheEditor >= scmLeastAuditedInTheEditor,
                     qPrintable(qsl("only %1 things were read in the editor on the %2 appearance, against the %3 this walk reaches - it stopped finding widgets rather than finding them all "
                                    "readable")
                                        .arg(QString::number(inTheEditor), appearance.first, QString::number(scmLeastAuditedInTheEditor))));
            QVERIFY2(inTheSettings >= scmLeastAuditedInTheSettings,
                     qPrintable(qsl("only %1 things were read in the settings dialog on the %2 appearance, against the %3 this walk reaches")
                                        .arg(QString::number(inTheSettings), appearance.first, QString::number(scmLeastAuditedInTheSettings))));
            QVERIFY2(inTheAbout >= scmLeastAuditedInTheAbout,
                     qPrintable(qsl("only %1 things were read in the About dialog on the %2 appearance, against the %3 this walk reaches")
                                        .arg(QString::number(inTheAbout), appearance.first, QString::number(scmLeastAuditedInTheAbout))));
        }

        qInfo().noquote() << qsl("  audited %1").arg(counts.join(qsl("; ")));
        for (const QString& failure : failures) {
            qWarning().noquote() << failure;
        }
        QVERIFY2(failures.isEmpty(), qPrintable(qsl("%1 thing(s) cannot be read against what is painted behind them - listed above").arg(QString::number(failures.size()))));
    }

    // A case per form: only one is in the column at a time, and the one it replaced
    // needs a repaint before its fields can be read off the screen again
    void test_theScriptsFormIsReadableInBothAppearances()
    {
        mpEditor->slot_showScripts();
        QCoreApplication::processEvents();
        QTest::qWait(100ms);
        QVERIFY2(mpEditor->mpChipRow_scriptEvents != nullptr && mpEditor->mpChipRow_scriptEvents->count() == 2, "the script this walks is not showing the two events it was given");

        QStringList failures;
        QStringList counts;
        for (const auto& appearance : QList<QPair<QString, enums::Appearance>>{{qsl("dark"), enums::Appearance::dark}, {qsl("light"), enums::Appearance::light}}) {
            setAppearance(appearance.second);
            const int read = auditWithin(mpEditor, mpEditor->mpScriptsMainArea, qsl("the editor's scripts form"), appearance.first, failures);
            counts << qsl("%1: %2").arg(appearance.first, QString::number(read));
            QVERIFY2(read >= scmLeastAuditedOnTheScriptsForm,
                     qPrintable(qsl("only %1 things were read on the scripts form on the %2 appearance, against the %3 this walk reaches")
                                        .arg(QString::number(read), appearance.first, QString::number(scmLeastAuditedOnTheScriptsForm))));
        }

        qInfo().noquote() << qsl("  audited %1").arg(counts.join(qsl("; ")));
        for (const QString& failure : failures) {
            qWarning().noquote() << failure;
        }
        QVERIFY2(failures.isEmpty(), qPrintable(qsl("%1 thing(s) cannot be read against what is painted behind them - listed above").arg(QString::number(failures.size()))));
    }

    void test_theKeysFormIsReadableInBothAppearances()
    {
        mpEditor->slot_showKeys();
        mpEditor->slot_keySelected(mpKeyItem);
        QCoreApplication::processEvents();
        QTest::qWait(100ms);
        QVERIFY2(mpEditor->mpLabel_keyHint != nullptr && mpEditor->mpLabel_keyHint->isVisible(), "the key this walks is not showing the hint beside its keystroke");

        QStringList failures;
        QStringList counts;
        for (const auto& appearance : QList<QPair<QString, enums::Appearance>>{{qsl("dark"), enums::Appearance::dark}, {qsl("light"), enums::Appearance::light}}) {
            setAppearance(appearance.second);
            const int read = auditWithin(mpEditor, mpEditor->mpKeysMainArea, qsl("the editor's keys form"), appearance.first, failures);
            counts << qsl("%1: %2").arg(appearance.first, QString::number(read));
            QVERIFY2(read >= scmLeastAuditedOnTheKeysForm,
                     qPrintable(qsl("only %1 things were read on the keys form on the %2 appearance, against the %3 this walk reaches")
                                        .arg(QString::number(read), appearance.first, QString::number(scmLeastAuditedOnTheKeysForm))));
        }

        qInfo().noquote() << qsl("  audited %1").arg(counts.join(qsl("; ")));
        for (const QString& failure : failures) {
            qWarning().noquote() << failure;
        }
        QVERIFY2(failures.isEmpty(), qPrintable(qsl("%1 thing(s) cannot be read against what is painted behind them - listed above").arg(QString::number(failures.size()))));
    }

    void test_theTimersFormIsReadableInBothAppearances()
    {
        mpEditor->slot_showTimers();
        mpEditor->slot_timerSelected(mpTimerItem);
        QCoreApplication::processEvents();
        QTest::qWait(100ms);
        QVERIFY2(mpEditor->mpWidget_timerInterval != nullptr && mpEditor->mpWidget_timerInterval->isVisible(), "the timer this walks is not showing its interval");

        QStringList failures;
        QStringList counts;
        for (const auto& appearance : QList<QPair<QString, enums::Appearance>>{{qsl("dark"), enums::Appearance::dark}, {qsl("light"), enums::Appearance::light}}) {
            setAppearance(appearance.second);
            const int read = auditWithin(mpEditor, mpEditor->mpTimersMainArea, qsl("the editor's timers form"), appearance.first, failures);
            counts << qsl("%1: %2").arg(appearance.first, QString::number(read));
            QVERIFY2(read >= scmLeastAuditedOnTheTimersForm,
                     qPrintable(qsl("only %1 things were read on the timers form on the %2 appearance, against the %3 this walk reaches")
                                        .arg(QString::number(read), appearance.first, QString::number(scmLeastAuditedOnTheTimersForm))));
        }

        qInfo().noquote() << qsl("  audited %1").arg(counts.join(qsl("; ")));
        for (const QString& failure : failures) {
            qWarning().noquote() << failure;
        }
        QVERIFY2(failures.isEmpty(), qPrintable(qsl("%1 thing(s) cannot be read against what is painted behind them - listed above").arg(QString::number(failures.size()))));
    }

    void test_theVariablesFormIsReadableInBothAppearances()
    {
        mpEditor->slot_showVariables();
        QCoreApplication::processEvents();
        QTreeWidgetItem* pRow = auditedVariableRow();
        QVERIFY2(pRow != nullptr, "the Variables view did not show the variable this walks");
        mpEditor->treeWidget_variables->setCurrentItem(pRow);
        mpEditor->slot_variableSelected(pRow);
        QCoreApplication::processEvents();
        QTest::qWait(100ms);
        QVERIFY2(mpEditor->mpVarsMainArea->comboBox_variable_key_type->isVisible(), "the variable this walks is not showing its type pickers");

        QStringList failures;
        QStringList counts;
        for (const auto& appearance : QList<QPair<QString, enums::Appearance>>{{qsl("dark"), enums::Appearance::dark}, {qsl("light"), enums::Appearance::light}}) {
            setAppearance(appearance.second);
            const int read = auditWithin(mpEditor, mpEditor->mpVarsMainArea, qsl("the editor's variables form"), appearance.first, failures);
            counts << qsl("%1: %2").arg(appearance.first, QString::number(read));
            QVERIFY2(read >= scmLeastAuditedOnTheVariablesForm,
                     qPrintable(qsl("only %1 things were read on the variables form on the %2 appearance, against the %3 this walk reaches")
                                        .arg(QString::number(read), appearance.first, QString::number(scmLeastAuditedOnTheVariablesForm))));
        }

        qInfo().noquote() << qsl("  audited %1").arg(counts.join(qsl("; ")));
        for (const QString& failure : failures) {
            qWarning().noquote() << failure;
        }
        QVERIFY2(failures.isEmpty(), qPrintable(qsl("%1 thing(s) cannot be read against what is painted behind them - listed above").arg(QString::number(failures.size()))));
    }

    // Only the tabs have adopted the design, and only one is on show at a time,
    // so each is walked with its own tab current
    void test_theConnectionDialogsTabsAreReadableInBothAppearances()
    {
        QVERIFY2(mpConnection->tabWidget_connectionInfo->isVisible(), "the connection dialog is showing the first-launch invitation rather than the tabs this walks");

        QStringList failures;
        QStringList counts;
        for (const auto& appearance : QList<QPair<QString, enums::Appearance>>{{qsl("dark"), enums::Appearance::dark}, {qsl("light"), enums::Appearance::light}}) {
            setAppearance(appearance.second);
            for (const auto& tab : QList<QPair<QWidget*, int>>{{mpConnection->tab_connection_info, scmLeastAuditedOnTheConnectTab}, {mpConnection->tab_options, scmLeastAuditedOnTheOptionsTab}}) {
                mpConnection->tabWidget_connectionInfo->setCurrentIndex(mpConnection->tabWidget_connectionInfo->indexOf(tab.first));
                QCoreApplication::processEvents();
                QTest::qWait(100ms);
                const int read = auditWithin(mpConnection, tab.first, qsl("the connection dialog's %1 tab").arg(tab.first->objectName()), appearance.first, failures);
                counts << qsl("%1 %2: %3").arg(appearance.first, tab.first->objectName(), QString::number(read));
                QVERIFY2(read >= tab.second,
                         qPrintable(qsl("only %1 things were read on the connection dialog's %2 tab on the %3 appearance, against the %4 this walk reaches")
                                            .arg(QString::number(read), tab.first->objectName(), appearance.first, QString::number(tab.second))));
            }

            // The button sheet is set on these two containers, which the tab pages do not reach
            for (const auto& row : QList<QPair<QWidget*, int>>{{mpConnection->profileAdminArea, scmLeastAuditedOnTheAdminRow}, {mpConnection->widget_bottom, scmLeastAuditedOnTheButtonRow}}) {
                const int read = auditWithin(mpConnection, row.first, qsl("the connection dialog's %1").arg(row.first->objectName()), appearance.first, failures);
                counts << qsl("%1 %2: %3").arg(appearance.first, row.first->objectName(), QString::number(read));
                QVERIFY2(read >= row.second,
                         qPrintable(qsl("only %1 things were read on the connection dialog's %2 on the %3 appearance, against the %4 this walk reaches")
                                            .arg(QString::number(read), row.first->objectName(), appearance.first, QString::number(row.second))));
            }
        }

        qInfo().noquote() << qsl("  audited %1").arg(counts.join(qsl("; ")));
        for (const QString& failure : failures) {
            qWarning().noquote() << failure;
        }
        QVERIFY2(failures.isEmpty(), qPrintable(qsl("%1 thing(s) cannot be read against what is painted behind them - listed above").arg(QString::number(failures.size()))));
    }

    // Walked whole: the design draws all of the notepad
    void test_theNotepadIsReadableInBothAppearances()
    {
        QStringList failures;
        QStringList counts;
        for (const auto& appearance : QList<QPair<QString, enums::Appearance>>{{qsl("dark"), enums::Appearance::dark}, {qsl("light"), enums::Appearance::light}}) {
            setAppearance(appearance.second);
            const int read = auditWindow(mpNotepad, qsl("the notepad"), appearance.first, failures);
            counts << qsl("%1: %2").arg(appearance.first, QString::number(read));
            QVERIFY2(read >= scmLeastAuditedInTheNotepad,
                     qPrintable(qsl("only %1 things were read in the notepad on the %2 appearance, against the %3 this walk reaches")
                                        .arg(QString::number(read), appearance.first, QString::number(scmLeastAuditedInTheNotepad))));
        }

        qInfo().noquote() << qsl("  audited %1").arg(counts.join(qsl("; ")));
        for (const QString& failure : failures) {
            qWarning().noquote() << failure;
        }
        QVERIFY2(failures.isEmpty(), qPrintable(qsl("%1 thing(s) cannot be read against what is painted behind them - listed above").arg(QString::number(failures.size()))));
    }

    // Walked a column at a time: each column carries its own sheet
    void test_thePackageManagerIsReadableInBothAppearances()
    {
        QStringList failures;
        QStringList counts;
        for (const auto& appearance : QList<QPair<QString, enums::Appearance>>{{qsl("dark"), enums::Appearance::dark}, {qsl("light"), enums::Appearance::light}}) {
            setAppearance(appearance.second);
            const int onTheList = auditWithin(mpPackages, mpPackages->leftPanel, qsl("the package manager's list column"), appearance.first, failures);
            const int onTheDetails = auditWithin(mpPackages, mpPackages->rightPanel, qsl("the package manager's details column"), appearance.first, failures);
            counts << qsl("%1: %2 on the list column, %3 on the details column").arg(appearance.first, QString::number(onTheList), QString::number(onTheDetails));
            QVERIFY2(onTheList >= scmLeastAuditedOnThePackageList,
                     qPrintable(qsl("only %1 things were read on the package manager's list column on the %2 appearance, against the %3 this walk reaches")
                                        .arg(QString::number(onTheList), appearance.first, QString::number(scmLeastAuditedOnThePackageList))));
            QVERIFY2(onTheDetails >= scmLeastAuditedOnThePackageDetails,
                     qPrintable(qsl("only %1 things were read on the package manager's details column on the %2 appearance, against the %3 this walk reaches")
                                        .arg(QString::number(onTheDetails), appearance.first, QString::number(scmLeastAuditedOnThePackageDetails))));
        }

        qInfo().noquote() << qsl("  audited %1").arg(counts.join(qsl("; ")));
        for (const QString& failure : failures) {
            qWarning().noquote() << failure;
        }
        QVERIFY2(failures.isEmpty(), qPrintable(qsl("%1 thing(s) cannot be read against what is painted behind them - listed above").arg(QString::number(failures.size()))));
    }

    // Walked a container at a time: each of the three carries its own sheet
    void test_thePackageExporterIsReadableInBothAppearances()
    {
        QWidget* pSelection = mpExporter->findChild<QWidget*>(qsl("exporterSelectionColumn"));
        QWidget* pDetails = mpExporter->findChild<QWidget*>(qsl("exporterDetailsColumn"));
        QWidget* pFoot = mpExporter->findChild<QWidget*>(qsl("exporterFoot"));
        QVERIFY2(pSelection && pDetails && pFoot, "the package exporter's shell is not there to walk");

        QStringList failures;
        QStringList counts;
        for (const auto& appearance : QList<QPair<QString, enums::Appearance>>{{qsl("dark"), enums::Appearance::dark}, {qsl("light"), enums::Appearance::light}}) {
            setAppearance(appearance.second);
            const int onTheTree = auditWithin(mpExporter, pSelection, qsl("the package exporter's selection column"), appearance.first, failures);
            const int onTheForm = auditWithin(mpExporter, pDetails, qsl("the package exporter's details column"), appearance.first, failures);
            const int onTheFoot = auditWithin(mpExporter, pFoot, qsl("the package exporter's foot"), appearance.first, failures);
            counts << qsl("%1: %2 on the selection column, %3 on the details column, %4 on the foot")
                              .arg(appearance.first, QString::number(onTheTree), QString::number(onTheForm), QString::number(onTheFoot));
            QVERIFY2(onTheTree >= scmLeastAuditedOnTheExporterSelection,
                     qPrintable(qsl("only %1 things were read on the package exporter's selection column on the %2 appearance, against the %3 this walk reaches")
                                        .arg(QString::number(onTheTree), appearance.first, QString::number(scmLeastAuditedOnTheExporterSelection))));
            QVERIFY2(onTheForm >= scmLeastAuditedOnTheExporterDetails,
                     qPrintable(qsl("only %1 things were read on the package exporter's details column on the %2 appearance, against the %3 this walk reaches")
                                        .arg(QString::number(onTheForm), appearance.first, QString::number(scmLeastAuditedOnTheExporterDetails))));
            QVERIFY2(onTheFoot >= scmLeastAuditedOnTheExporterFoot,
                     qPrintable(qsl("only %1 things were read on the package exporter's foot on the %2 appearance, against the %3 this walk reaches")
                                        .arg(QString::number(onTheFoot), appearance.first, QString::number(scmLeastAuditedOnTheExporterFoot))));
        }

        qInfo().noquote() << qsl("  audited %1").arg(counts.join(qsl("; ")));
        for (const QString& failure : failures) {
            qWarning().noquote() << failure;
        }
        QVERIFY2(failures.isEmpty(), qPrintable(qsl("%1 thing(s) cannot be read against what is painted behind them - listed above").arg(QString::number(failures.size()))));
    }

    void test_theButtonsFormIsReadableInBothAppearances()
    {
        mpEditor->slot_showActions();
        mpEditor->slot_actionSelected(mpActionItem);
        QCoreApplication::processEvents();
        QTest::qWait(100ms);
        QVERIFY2(mpEditor->mpWidget_actionRotationRow != nullptr && mpEditor->mpWidget_actionRotationRow->isVisible(), "the button this walks is not showing the row its rotation is set on");

        QStringList failures;
        QStringList counts;
        for (const auto& appearance : QList<QPair<QString, enums::Appearance>>{{qsl("dark"), enums::Appearance::dark}, {qsl("light"), enums::Appearance::light}}) {
            setAppearance(appearance.second);
            const int read = auditWithin(mpEditor, mpEditor->mpActionsMainArea, qsl("the editor's buttons form"), appearance.first, failures);
            counts << qsl("%1: %2").arg(appearance.first, QString::number(read));
            QVERIFY2(read >= scmLeastAuditedOnTheButtonsForm,
                     qPrintable(qsl("only %1 things were read on the buttons form on the %2 appearance, against the %3 this walk reaches")
                                        .arg(QString::number(read), appearance.first, QString::number(scmLeastAuditedOnTheButtonsForm))));
        }

        qInfo().noquote() << qsl("  audited %1").arg(counts.join(qsl("; ")));
        for (const QString& failure : failures) {
            qWarning().noquote() << failure;
        }
        QVERIFY2(failures.isEmpty(), qPrintable(qsl("%1 thing(s) cannot be read against what is painted behind them - listed above").arg(QString::number(failures.size()))));
    }
};

#include "ReadabilityAuditTest.moc"
MUDLET_GROUPED_TEST_MAIN(ReadabilityAuditTest)
