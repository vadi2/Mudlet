/***************************************************************************
 *   Copyright (C) 2026 by Vadim Peretokin - vperetokin@gmail.com          *
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

#include <ShortcutsManager.h>
#include <QtTest/QtTest>

class ShortcutsManagerTest : public QObject
{
    Q_OBJECT

private slots:
    void testSetShortcutWithNullSequence();
    void testSetShortcutWithUnregisteredKey();
    void testSetShortcutWithValidInputs();
};

// BUG: setShortcut dereferences the sequence pointer without a null check,
// so passing nullptr causes a crash. The safe behavior is to return early
// or otherwise handle nullptr gracefully.
void ShortcutsManagerTest::testSetShortcutWithNullSequence()
{
    ShortcutsManager manager;

    // Register a shortcut so the key exists in the map
    QKeySequence seq(Qt::CTRL | Qt::Key_A);
    manager.registerShortcut(qsl("test_action"), qsl("Test Action"), &seq);

    // Calling setShortcut with nullptr should not crash
    manager.setShortcut(qsl("test_action"), nullptr);

    // The original shortcut should remain unchanged after a no-op on nullptr
    QVERIFY(manager.getSequence(qsl("test_action")) != nullptr);
    QCOMPARE(*manager.getSequence(qsl("test_action")), QKeySequence(Qt::CTRL | Qt::Key_A));
}

// BUG: shortcuts.value(key) returns nullptr for an unregistered key, and
// then ->swap() dereferences it, causing a crash. The safe behavior is to
// return early or otherwise handle the missing key gracefully.
void ShortcutsManagerTest::testSetShortcutWithUnregisteredKey()
{
    ShortcutsManager manager;

    QKeySequence seq(Qt::CTRL | Qt::Key_B);

    // Calling setShortcut for a key that was never registered should not crash
    manager.setShortcut(qsl("nonexistent_key"), &seq);

    // The unregistered key should still not exist in the manager
    QVERIFY(manager.getSequence(qsl("nonexistent_key")) == nullptr);
}

// Happy path: register a shortcut, then update it via setShortcut, and
// verify the value was changed correctly.
void ShortcutsManagerTest::testSetShortcutWithValidInputs()
{
    ShortcutsManager manager;

    QKeySequence original(Qt::CTRL | Qt::Key_C);
    manager.registerShortcut(qsl("copy_action"), qsl("Copy"), &original);

    // Verify the initial registration
    QVERIFY(manager.getSequence(qsl("copy_action")) != nullptr);
    QCOMPARE(*manager.getSequence(qsl("copy_action")), QKeySequence(Qt::CTRL | Qt::Key_C));

    // Update the shortcut to a new key sequence
    QKeySequence updated(Qt::CTRL | Qt::SHIFT | Qt::Key_C);
    manager.setShortcut(qsl("copy_action"), &updated);

    // The shortcut should now reflect the updated value
    QVERIFY(manager.getSequence(qsl("copy_action")) != nullptr);
    QCOMPARE(*manager.getSequence(qsl("copy_action")), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_C));

    // The default should remain at the original value
    QVERIFY(manager.getDefault(qsl("copy_action")) != nullptr);
    QCOMPARE(*manager.getDefault(qsl("copy_action")), QKeySequence(Qt::CTRL | Qt::Key_C));
}

#include "ShortcutsManagerTest.moc"
QTEST_MAIN(ShortcutsManagerTest)
