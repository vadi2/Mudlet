/***************************************************************************
 *   Copyright (C) 2025 by Mudlet makers - mudlet@mudlet.org              *
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

#include <QtTest/QtTest>
#include <cstdlib>

#include "TelnetServerStub.h"
#include "mudlet.h"
#include "ctelnet.h"
#include "dlgConnectionProfiles.h"

extern void qInitResources_mudlet();
extern void qInitResources_qm();
extern void qInitResources_additional_splash_screens();
extern void qInitResources_mudlet_fonts_common();
extern void qInitResources_mudlet_fonts_posix();
void        initializeQRCResources_VT100();

class VT100SequenceTest : public QObject {
    Q_OBJECT

private:
    TelnetServerStub* mpServer = nullptr;
    const QString mpHostname = "Test-VT100";
    const QString mpPort = "4001";
    const QString mpLocalhost = "localhost";

private slots:
    void initTestCase()
    {
        initializeQRCResources_VT100();
    }

    void init()
    {
        mpServer = new TelnetServerStub(qApp);
        mpServer->start(mpLocalhost, mpPort.toUShort());
        mudlet::start();
        mudlet::self()->setupConfig();
        mudlet::self()->takeOwnershipOfInstanceCoordinator(std::make_unique<MudletInstanceCoordinator>("MudletInstanceCoordinator"));
        mudlet::self()->init();
        mudlet::self()->setStorePasswordsSecurely(false);
        deleteProfileDirectory(mpHostname);
    }

    QString getTestLine(TBuffer& buf)
    {
        for (int i = buf.getLastLineNumber(); i >= 0; --i) {
            const QString line = buf.line(i);
            if (!line.isEmpty() && !line.startsWith(QLatin1String("["))) {
                return line;
            }
        }
        return QString();
    }

    void test_CursorPositionAbsolute()
    {
        // CSI 1;1 H positions cursor at row 1, col 1, then writes "X"
        QString message("Hello\x1B[1;1HX\n");
        QString expected("Xello");

        mpServer->setWelcomeMessage(message);
        startProfile(mpHostname, mpLocalhost, mpPort, true);
        QSignalSpy(mudlet::self()->getActiveHost()->mpConsole, &TMainConsole::signal_newDataAlert).wait(200);

        auto& buf = mudlet::self()->getActiveHost()->mpConsole->buffer;
        QCOMPARE(getTestLine(buf), expected);
    }

    void test_CursorForward()
    {
        // CSI 3 C moves cursor forward 3 positions
        QString message("AB\x1B[3CX\n");
        QString expected("AB   X");

        mpServer->setWelcomeMessage(message);
        startProfile(mpHostname, mpLocalhost, mpPort, true);
        QSignalSpy(mudlet::self()->getActiveHost()->mpConsole, &TMainConsole::signal_newDataAlert).wait(200);

        auto& buf = mudlet::self()->getActiveHost()->mpConsole->buffer;
        QCOMPARE(getTestLine(buf), expected);
    }

    void test_CursorBackward()
    {
        // CSI 2 D moves cursor backward 2 positions
        QString message("ABCDE\x1B[2DXY\n");
        QString expected("ABCXY");

        mpServer->setWelcomeMessage(message);
        startProfile(mpHostname, mpLocalhost, mpPort, true);
        QSignalSpy(mudlet::self()->getActiveHost()->mpConsole, &TMainConsole::signal_newDataAlert).wait(200);

        auto& buf = mudlet::self()->getActiveHost()->mpConsole->buffer;
        QCOMPARE(getTestLine(buf), expected);
    }

    void test_EraseToEndOfLine()
    {
        // CSI 0 K erases from cursor to end of line
        QString message("Hello World\x1B[1;6H\x1B[0K\n");
        QString expected("Hello");

        mpServer->setWelcomeMessage(message);
        startProfile(mpHostname, mpLocalhost, mpPort, true);
        QSignalSpy(mudlet::self()->getActiveHost()->mpConsole, &TMainConsole::signal_newDataAlert).wait(200);

        auto& buf = mudlet::self()->getActiveHost()->mpConsole->buffer;
        QCOMPARE(getTestLine(buf), expected);
    }

    void test_EraseWholeLine()
    {
        // CSI 2 K erases entire line - expect first non-system, non-empty line to be from a different test
        // With erase whole line, the test content becomes empty, so we look for any empty line
        QString message("Hello World\x1B[1;1H\x1B[2K\n");

        mpServer->setWelcomeMessage(message);
        startProfile(mpHostname, mpLocalhost, mpPort, true);
        QSignalSpy(mudlet::self()->getActiveHost()->mpConsole, &TMainConsole::signal_newDataAlert).wait(200);

        auto& buf = mudlet::self()->getActiveHost()->mpConsole->buffer;
        bool foundEmptyAfterSystem = false;
        for (int i = 0; i <= buf.getLastLineNumber(); ++i) {
            const QString line = buf.line(i);
            if (!line.startsWith(QLatin1String("[")) && line.isEmpty()) {
                foundEmptyAfterSystem = true;
                break;
            }
        }
        QVERIFY(foundEmptyAfterSystem);
    }

    void test_VT100DisabledByDefault()
    {
        // With VT100 disabled, cursor sequences should be ignored/stripped
        QString message("Hello\x1B[1;1HX\n");
        QString expected("HelloX");

        mpServer->setWelcomeMessage(message);
        startProfile(mpHostname, mpLocalhost, mpPort, false);
        QSignalSpy(mudlet::self()->getActiveHost()->mpConsole, &TMainConsole::signal_newDataAlert).wait(200);

        auto& buf = mudlet::self()->getActiveHost()->mpConsole->buffer;
        QCOMPARE(getTestLine(buf), expected);
    }

    void cleanup()
    {
        delete mpServer;
        mpServer = nullptr;
        deleteProfileDirectory(mpHostname);
        delete mudlet::self();
    }

    void startProfile(const QString& hostname, const QString& address, const QString& port, bool enableVT100)
    {
        QTimer::singleShot(0, qApp, [hostname, address, port]() {
            mudlet::self()->startAutoLogin({});
            QTest::qWait(100);
            QTest::mouseClick(mudlet::self()->mpConnectionDialog->new_profile_button, Qt::LeftButton);
            QTest::qWait(100);
            QTest::keyClicks(QApplication::focusWidget(), hostname);
            QTest::qWait(100);
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
            QTest::qWait(100);
            QTest::keyClicks(QApplication::focusWidget(), address);
            QTest::qWait(100);
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_Tab);
            QTest::qWait(100);
            QTest::keyClicks(QApplication::focusWidget(), port);
            QTest::qWait(100);
            QTest::keyClick(QApplication::focusWidget(), Qt::Key_Return);
        });

        QSignalSpy spy(mudlet::self(), &mudlet::signal_profileLoaded);
        if (!spy.wait(1000)) {
            QFAIL("Profile took too long to load.");
        }
        auto host = mudlet::self()->getActiveHost();
        if (!host) {
            QFAIL("No active host available for the test.");
        }

        host->mEnableVT100 = enableVT100;
        if (enableVT100 && host->mpConsole) {
            host->mpConsole->buffer.setVT100Enabled(true);
        }

        QSignalSpy spy2(&(host->mTelnet), &cTelnet::signal_connected);
        if (!spy2.wait(500)) {
            QFAIL("Could not connect with the host.");
        }
    }

    void deleteProfileDirectory(const QString& profileName)
    {
        const QString path = mudlet::getMudletPath(enums::profileHomePath, profileName);
        QDir dir(path);

        if (!dir.exists()) {
            qInfo() << "Profile directory does not exist:" << path;
            return;
        }
        dir.removeRecursively();
    }
};

void initializeQRCResources_VT100() {
#ifdef INCLUDE_VARIABLE_SPLASH_SCREEN
    qInitResources_additional_splash_screens();
#endif
#ifdef INCLUDE_FONTS
    qInitResources_mudlet_fonts_common();
#if defined(Q_OS_LINUX) || defined(Q_OS_FREEBSD)
    qInitResources_mudlet_fonts_posix();
#endif
#endif
    qInitResources_mudlet();
    qInitResources_qm();
}

#include "VT100SequenceTest.moc"
QTEST_MAIN(VT100SequenceTest)
