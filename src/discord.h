#ifndef DISCORD_H
#define DISCORD_H
/***************************************************************************
 *   Copyright (C) 2018 by Vadim Peretokin - vperetokin@gmail.com          *
 *   Copyright (C) 2018 by Stephen Lyons - slysven@virginmedia.com         *
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

#include "Host.h"

#include "pre_guard.h"
#include <functional>
#include <utility>
#include <QDebug>
#include <QReadWriteLock>
#include <QTimer>
#include <QTimerEvent>
#include <QLibrary>

#if (defined(Q_OS_LINUX) && defined(Q_PROCESSOR_X86_64)) || defined(Q_OS_MACOS) || defined(Q_OS_WIN32)
// Discord does not provide support for 32Bit Linux processor - the blighters, 8-(
#include "../3rdparty/discord/rpc/include/discord_register.h"
#include "../3rdparty/discord/rpc/include/discord_rpc.h"
#else
// Unsupported OS/Processors
// * FreeBSD
// * 32Bit Linux
#endif

#include "post_guard.h"

/*
 * From the discord headers and on-line documentation:
 * typedef struct DiscordRichPresence {
 *    const char* state; // max 128 bytes
 *    const char* details; // max 128 bytes
 *    int64_t startTimestamp;
 *    int64_t endTimestamp;
 *    const char* largeImageKey; // max 32 bytes
 *    const char* largeImageText; // max 128 bytes
 *    const char* smallImageKey; // max 32 bytes
 *    const char* smallImageText; // max 128 bytes
 *    const char* partyId; // max 128 bytes
 *    int partySize;
 *    int partyMax;
 *    const char* matchSecret; // max 128 bytes
 *    const char* joinSecret; // max 128 bytes
 *    const char* spectateSecret; // max 128 bytes
 *    int8_t instance;
 * } DiscordRichPresence;
 *
 *
 * typedef struct DiscordUser {
 *    const char* userId;
 *    const char* username;
 *    const char* discriminator;
 *    const char* avatar;
 * } DiscordUser;
 */

// This is used to hold data to be stuffed into a DiscordRichPresence before
// it is sent to the RPC library with an Discord_UpdatePresence(...) call.
// It is done this way because the definition we have for the
// DiscordRichPresence is filled with const char pointers that can only be
// set on instantiation.
class localDiscordPresence {

public:
    localDiscordPresence()
    : mState(), mDetails()
    , mStartTimestamp(0), mEndTimestamp(0)
    , mLargeImageKey(), mLargeImageText()
    , mSmallImageKey(), mSmallImageText()
    , mPartyId(), mPartySize(0), mPartyMax(0)
    , mMatchSecret(), mJoinSecret(), mSpectateSecret()
    , mInstance(1)
    {
    }

    void setStateText(const QString&);
    void setDetailText(const QString&);
    void setStartTimeStamp(int64_t startTime) { mStartTimestamp = startTime; }
    void setEndTimeStamp(int64_t endTime) { mEndTimestamp = endTime; }
    void setLargeImageKey(const QString&);
    void setLargeImageText(const QString&);
    void setSmallImageKey(const QString&);
    void setSmallImageText(const QString&);
    void setJoinSecret(const QString&);
    void setMatchSecret(const QString&);
    void setSpectateSecret(const QString&);
    DiscordRichPresence convert() const;
    const QString& getStateText() const { return QString::fromUtf8(mState); }
    QString getDetailText() const { return QString::fromUtf8(mDetails); }
    int64_t getStartTimeStamp() const { return mStartTimestamp; }
    int64_t getEndTimeStamp() const { return mEndTimestamp; }
    QString getLargeImageKey() const { return QString::fromUtf8(mLargeImageKey); }
    QString getLargeImageText() const { return QString::fromUtf8(mLargeImageText); }
    QString getSmallImageKey() const { return QString::fromUtf8(mSmallImageKey); }
    QString getSmallImageText() const { return QString::fromUtf8(mSmallImageText); }
    QString getJoinSecret() const { return QString::fromUtf8(mJoinSecret); }
    QString getMatchSecret() const { return QString::fromUtf8(mMatchSecret); }
    QString getSpectateSecret() const { return QString::fromUtf8(mSpectateSecret); }
    QString getPartyId() const { return QString::fromUtf8(mPartyId); }
    int getPartySize() const { return mPartySize; }
    int getPartyMax() const { return mPartyMax; }
    int8_t getInstance() const { return mInstance; }

private:
    char mState[128]; // max 128 bytes
    char mDetails[128]; // max 128 bytes
    int64_t mStartTimestamp;
    int64_t mEndTimestamp;
    char mLargeImageKey[32]; // max 32 bytes
    char mLargeImageText[128]; // max 128 bytes
    char mSmallImageKey[32]; // max 32 bytes
    char mSmallImageText[128]; // max 128 bytes
    char mPartyId[128]; // max 128 bytes
    int mPartySize;
    int mPartyMax;
    char mMatchSecret[128]; // max 128 bytes
    char mJoinSecret[128]; // max 128 bytes
    char mSpectateSecret[128]; // max 128 bytes
    int8_t mInstance;
};

#ifndef QT_NO_DEBUG_STREAM
// Note "inline" is REQUIRED:
inline QDebug& operator<<(QDebug& debug, const localDiscordPresence& ldp)
{
    QDebugStateSaver saver(debug);
    Q_UNUSED(saver);

    QString result = QStringLiteral("localDiscordPresence(\n"
                                    "    mDetails: \"%1\"  mState: \"%2\" mInstance: %3\n"
                                    "    mLargeImageKey: \"%4\"  mLargeImageText: \"%5\" \n"
                                    "    mSmallImageKey: \"%6\"  mSmallImageText: \"%7\" \n")
                     .arg(ldp.getDetailText(), ldp.getStateText(),
                          QString::number(ldp.getInstance()),
                          ldp.getLargeImageKey(), ldp.getLargeImageText(),
                          ldp.getSmallImageKey(), ldp.getSmallImageText());

    result.append(QStringLiteral("    mPartyId: \"%1\"  mPartySize: %2 mPartyMax %3\n"
                                 "    mMatchSecret: \"%4\"  mJoinSecret: \"%5\" mSpectateSecret \"%6\")\n")
                  .arg(ldp.getMatchSecret(), ldp.getJoinSecret(), ldp.getSpectateSecret(),
                       ldp.getPartyId(), QString::number(ldp.getPartySize()), QString::number(ldp.getPartyMax())));

    debug.nospace().noquote() << result;
    return debug;
}
#endif // QT_NO_DEBUG_STREAM

class Discord : public QObject
{
    Q_OBJECT

public:
    explicit Discord(QObject *parent = nullptr);    
    ~Discord() override;

    void UpdatePresence();
    // The next four methods only work when using the default Mudlet presenceId
    // they also clear the mRawMode flag for the given host so that the update
    // process used the values entered for them:

    // Sets the "detailText" to "Playing Xxxx" and the LargeImageKey to the
    // lower-case form of name for Games that Mudlet knows about:
    std::tuple<bool, QString> setGame(Host* pHost, const QString& name);
    // Sets the "stateText" to the given "area":
    std::tuple<bool, QString> setArea(Host* pHost, const QString& area);
    // If the game is one Mudlet "knows" about, sets the small icon to the given
    // key - otherwise sets the large one (which will not otherwise have been
    // given an icon) to that key - however this must be enabled by a perission
    // on the "Special options" tab of the "Profile Preferences":
    std::tuple<bool, QString> setCharacterIcon(Host* pHost, const QString& icon);
    // If the game is one Mudlet "knows" about, sets the small icon tooltip to
    // the given text - otherwise sets the large icon tooltip to the text if
    // the Mud Server address is set to be hidden:
    std::tuple<bool, QString> setCharacter(Host* pHost, const QString& text);


    bool gameIntegrationSupported(const QString& address);
    bool libraryLoaded();

    // The next six methods work when using any presenceId they also set the
    // mRawMode flag for the given host so that the update process uses them
    // directly - effectively they give lua access to the Discord RPC via
    // a buffer that caches the values so they can be retrieved afterwards:
    bool setLargeImage(Host*, const QString&);
    bool setLargeImageText(Host*, const QString&);
    bool setSmallImage(Host*, const QString&);
    bool setSmallImageText(Host*, const QString&);
    bool setStateText(Host*, const QString&);
    bool setDetailText(Host*, const QString&);
    bool setPresence(Host*, const QString&);

    bool isUsingDefaultDiscordPresence(Host*) const;

    // These retrieve the cached data - even if it was entered via the Mudlet
    // Presence Id specific methods:
    std::tuple<bool, QString> getDetailText(Host*) const;
    std::tuple<bool, QString> getStateText(Host*) const;
    std::tuple<bool, QString> getLargeImage(Host*) const;
    std::tuple<bool, QString> getLargeImageText(Host*) const;
    std::tuple<bool, QString> getSmallImage(Host*) const;
    std::tuple<bool, QString> getSmallImageText(Host*) const;

    // Returns the Discord user received from the Discord_Ready callback
    // this is typically send every minute, perhaps?
    const QStringList& getDiscordUserDetails() const;

private:
    static void handleDiscordReady(const DiscordUser* request);
    static void handleDiscordDisconnected(int errorCode, const char* message);
    static void handleDiscordError(int errorCode, const char* message);
    static void handleDiscordJoinGame(const char* joinSecret);
    static void handleDiscordSpectateGame(const char* spectateSecret);
    static void handleDiscordJoinRequest(const DiscordUser* request);

    void timerEvent(QTimerEvent *event) override;

    DiscordEventHandlers* mpHandlers;

    // These are function pointers to functions located in the Discord RPC library:
    std::function<void(const char*, DiscordEventHandlers*, int)> Discord_Initialize;
    std::function<void(const DiscordRichPresence* presence)> Discord_UpdatePresence;
    std::function<void(void)> Discord_RunCallbacks;
    std::function<void(void)> Discord_Shutdown;

    bool mLoaded;

    // Key is a Presence Id, Value is a pointer to a local copy of the data
    // currently held for that presence:
    QMap<QString, localDiscordPresence*> mPresencePtrs;

    // Used to tie a profile to a particular Discord presence - multiple
    // profiles can have the same presence but defaults to the nullptr one for
    // Mudlet:
    QMap<Host*, QString>mHostPresenceIds;
    // Used to populate Discord's "details" as "Playing %s" (first line of text)
    // and "Large icon key" from lower case version:
    QMap<Host*, QString>mGamesNames;
    // Used to populate Discord's "State" first part of second line of text
    // (followed by X of Y numbers {Party Size/Party Max)
    QMap<Host*, QString>mAreas;
    // Used to populate Discord's " "Small image key"
    QMap<Host*, QString>mCharacterIcons;
    // Used to populate "Small image text"
    QMap<Host*, QString>mCharacters;

    QScopedPointer<QLibrary> mpLibrary;

    QMap<Host*, int64_t>mStartTimes;
    QMap<Host*, QString>mDetailTexts;
    QMap<Host*, QString>mStateTexts;
    QMap<Host*, QString>mLargeImages;
    QMap<Host*, QString>mLargeImageTexts;
    QMap<Host*, QString>mSmallImages;
    QMap<Host*, QString>mSmallImageTexts;

    QMap<Host*, bool>mRawMode;

    QHash<QString, QVector<QString>> mKnownGames;
    QString mCurrentPresenceId;

    // Protect the four values after this one from async processes:
    static QReadWriteLock smReadWriteLock;
    // These may be needed in the future to validate the local user's presence
    // on Discord is the one that they want to be associated with a profile's
    // character name - it may be desired to not reveal the character name on
    // Discord until that has confirmed that a currently active
    // Discord/Discord-PTB/Discord-Canary application is using the expected
    // User identity (reflected in the User Avatar image and name within that
    // application).
    static QString smUserName;
    static QString smUserId;
    static QString smDiscriminator;
    static QString smAvatar;

signals:

public slots:
    void slot_handleGameConnection(Host*);
    void slot_handleGameDisconnection(Host*);

};

#endif // DISCORD_H
