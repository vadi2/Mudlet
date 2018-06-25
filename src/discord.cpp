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

#include "discord.h"
#include "mudlet.h"

#include "pre_guard.h"
#include <QtDebug>
#include <QSet>
#include <QHash>
#include <ctime>
#include <string.h> // For strncpy
#include <stdio.h> // For snprinf
#include "post_guard.h"

QReadWriteLock Discord::smReadWriteLock;

QString Discord::smUserName;
QString Discord::smUserId;
QString Discord::smDiscriminator;
QString Discord::smAvatar;

Discord::Discord(QObject* parent)
: QObject(parent)
, mGamesNames{}
, mAreas{}
, mCharacterIcons{}
, mCharacters{}
, mLoaded{}
// lowercase list of known games
// {game name, {game addresses}}
// The values are not currently used!
, mKnownGames{{"midmud", {"midmud.com"}},
              {"wotmud", {"game.wotmud.org"}},
              {"luminari", {}},
              {"achaea", {"achaea.com", "iron-ach.ironrealms.com"}},
              {"aetolia", {"aetolia.com", "iron-aet.ironrealms.com"}},
              {"imperian", {"imperian.com", " iron-imp.ironrealms.com"}},
              {"lusternia", {"lusternia.com", "iron-lus.ironrealms.com"}},
              {"starmourn", {"starmourn.com"}}}
// For details see https://discordapp.com/developers/docs/rich-presence/how-to#initialization
// Initialise with a nullptr one with Mudlet's own ID
// N. B. for testing the following MUDs have registered:
// "midmud"  is "460618737712889858", has "server-icon", "exventure" and "mudlet" icons
// "carinus" is "438335628942376960", has "server-icon" and "mudlet" icons
// "wotmud"  is "464945517156106240", has "mudlet" icon
,mHostPresenceIds{{nullptr,"450571881909583884"}}
{
    qDebug() << "Will search for Discord RPC library file in:";
    for (QString libraryPath : qApp->libraryPaths()) {
        qDebug() << "    " << libraryPath;
    }
    mpLibrary.reset(new QLibrary(QStringLiteral("discord-rpc")));

    using Discord_InitializePrototype = void (*)(const char*, DiscordEventHandlers*, int);
    using Discord_UpdatePresencePrototype = void (*)(const DiscordRichPresence*);
    using Discord_RunCallbacksPrototype = void (*)();
    using Discord_ShutdownPrototype = void (*)();

    Discord_Initialize = reinterpret_cast<Discord_InitializePrototype>(mpLibrary->resolve("Discord_Initialize"));
    Discord_UpdatePresence = reinterpret_cast<Discord_UpdatePresencePrototype>(mpLibrary->resolve("Discord_UpdatePresence"));
    Discord_RunCallbacks = reinterpret_cast<Discord_RunCallbacksPrototype>(mpLibrary->resolve("Discord_RunCallbacks"));
    Discord_Shutdown = reinterpret_cast<Discord_ShutdownPrototype>(mpLibrary->resolve("Discord_Shutdown"));

    if (!Discord_Initialize || !Discord_UpdatePresence || !Discord_RunCallbacks || !Discord_Shutdown) {
        qDebug() << "Discord integration failure, failed to load functions from library dynamically.";
        return;
    }

    mLoaded = true;
    qDebug() << "Discord integration loaded. Using functions from:" << mpLibrary.data()->fileName();

    mpHandlers = new DiscordEventHandlers;
    memset(mpHandlers, 0, sizeof(DiscordEventHandlers));
    mpHandlers->ready = handleDiscordReady;
    mpHandlers->errored = handleDiscordError;
    mpHandlers->disconnected = handleDiscordDisconnected;
    mpHandlers->joinGame = handleDiscordJoinGame;
    mpHandlers->spectateGame = handleDiscordSpectateGame;
    mpHandlers->joinRequest = handleDiscordJoinRequest;

    // Initialise the default Mudlet presence - we are not registering an
    // application protocol to start a game on the player's computer from Discord:
    qDebug().nospace().noquote() << "Discord::Discord(...) INFO - calling Discord_Initialize(\"" << mHostPresenceIds.value(nullptr) << "\", mpHandlers, 0)";
    Discord_Initialize(mHostPresenceIds.value(nullptr).toUtf8().constData(), mpHandlers, 0);
    // A new localDiscordPresence instance (with an empty key) will be generated
    // on the first call to UpdatePresence()


    // mudlet instance is not available in this constructor as it's still being initialised, so postpone the connection
    QTimer::singleShot(0, [this]() {
        Q_ASSERT(mudlet::self());
        connect(mudlet::self(), &mudlet::signal_tabChanged, this, &Discord::UpdatePresence);

        // process Discord callbacks every 50ms once we are all set up:
        startTimer(50);
    });

}

Discord::~Discord()
{
    if (mLoaded) {
        Discord_Shutdown();
        // We might expect to have to do an mpLibrary->unload() but we do not
        // need to as it happens automagically on the application shutdown...

        // Clear out the localDiscordPresence collection:
        QMutableMapIterator<QString, localDiscordPresence*> itPresencePtrs(mPresencePtrs);
        while (itPresencePtrs.hasNext()) {
            itPresencePtrs.next();
            delete itPresencePtrs.value();
            itPresencePtrs.remove();
        }
    }
}

std::tuple<bool, QString> Discord::setGame(Host* pHost, const QString& name)
{
    if (!mLoaded) {
        return std::tuple<bool, QString>(false, QStringLiteral("Discord integration is not available"));
    }

    if (isUsingDefaultDiscordPresence(pHost)) {
        mGamesNames[pHost] = name;
        UpdatePresence();
        if (mKnownGames.contains(name.toLower())) {
            return std::tuple<bool, QString>(true, QString());
        } else {
            // set the game anyway to what the user would like, but warn
            return std::tuple<bool, QString>(false, QStringLiteral("changed text, but %1 is not a known game - no icon will be displayed unless it has been added since this version of Mudlet was created").arg(name));
        }
    } else {
        return std::tuple<bool, QString>(false, QStringLiteral("setDiscordGame function only available when using default (Mudlet's) own Discord Presence Id"));
    }
}

std::tuple<bool, QString> Discord::setArea(Host* pHost, const QString& area)
{
    if (!mLoaded) {
        return std::tuple<bool, QString>(false, QStringLiteral("Discord integration is not available"));
    }

    if (isUsingDefaultDiscordPresence(pHost)) {
        mRawMode[pHost] = false;
        mAreas[pHost] = area;
        UpdatePresence();
        return std::tuple<bool, QString>(true, QString());
    } else {
        return std::tuple<bool, QString>(false, QStringLiteral("setDiscordArea function only available when using default (Mudlet's) own Discord Presence Id"));
    }
}

std::tuple<bool, QString> Discord::setCharacterIcon(Host* pHost, const QString& icon)
{
    if (!mLoaded) {
        return std::tuple<bool, QString>(false, QStringLiteral("Discord integration is not available"));
    }

    if (isUsingDefaultDiscordPresence(pHost)) {
        mRawMode[pHost] = false;
        mCharacterIcons[pHost] = icon;
        UpdatePresence();
        return std::tuple<bool, QString>(true, QString());
    } else {
        return std::tuple<bool, QString>(false, QStringLiteral("setDiscordCharacterIcon function only available when using default (Mudlet's) own Discord Presence Id"));
    }
}

std::tuple<bool, QString> Discord::setCharacter(Host* pHost, const QString& text)
{
    if (!mLoaded) {
        return std::tuple<bool, QString>(false, QStringLiteral("Discord integration is not available"));
    }

    if (isUsingDefaultDiscordPresence(pHost)) {
        mRawMode[pHost] = false;
        mCharacters[pHost] = text;
        UpdatePresence();
        return std::tuple<bool, QString>(true, QString());
    } else {
        return std::tuple<bool, QString>(false, QStringLiteral("setDiscordCharacter function only available when using default (Mudlet's) own Discord Presence Id"));
    }
}

bool Discord::setDetailText(Host* pHost, const QString& text)
{
    if (mLoaded) {
        mRawMode[pHost] = true;
        mDetailTexts[pHost] = text;
        UpdatePresence();
        return true;
    }
    return false;
}

std::tuple<bool, QString> Discord::getDetailText(Host* pHost) const
{
    if (mLoaded) {
        return std::tuple<bool, QString>(true, mDetailTexts.value(pHost));
    } else {
        return std::tuple<bool, QString>(false, QStringLiteral("Discord integration is not available"));
    }
}

bool Discord::setStateText(Host* pHost, const QString& text)
{
    if (mLoaded) {
        mRawMode[pHost] = true;
        mStateTexts[pHost] = text;
        UpdatePresence();
        return true;
    }
    return false;
}

std::tuple<bool, QString> Discord::getStateText(Host* pHost) const
{
    if (mLoaded) {
        return std::tuple<bool, QString>(true, mStateTexts.value(pHost));
    } else {
        return std::tuple<bool, QString>(false, QStringLiteral("Discord integration is not available"));
    }
}

bool Discord::setLargeImage(Host* pHost, const QString& text)
{
    if (mLoaded) {
        mRawMode[pHost] = true;
        mLargeImages[pHost] = text;
        UpdatePresence();
        return true;
    }
    return false;
}

std::tuple<bool, QString> Discord::getLargeImage(Host* pHost) const
{
    if (mLoaded) {
        return std::tuple<bool, QString>(true, mLargeImages.value(pHost));
    } else {
        return std::tuple<bool, QString>(false, QStringLiteral("Discord integration is not available"));
    }
}

bool Discord::setLargeImageText(Host* pHost, const QString& text)
{
    if (mLoaded) {
        mRawMode[pHost] = true;
        mLargeImageTexts[pHost] = text;
        UpdatePresence();
        return true;
    }
    return false;
}

std::tuple<bool, QString> Discord::getLargeImageText(Host* pHost) const
{
    if (mLoaded) {
        return std::tuple<bool, QString>(true, mLargeImageTexts.value(pHost));
    } else {
        return std::tuple<bool, QString>(false, QStringLiteral("Discord integration is not available"));
    }
}

bool Discord::setSmallImage(Host* pHost, const QString& text)
{
    if (mLoaded) {
        mRawMode[pHost] = true;
        mSmallImages[pHost] = text;
        UpdatePresence();
        return true;
    }
    return false;
}

std::tuple<bool, QString> Discord::getSmallImage(Host* pHost) const
{
    if (mLoaded) {
        return std::tuple<bool, QString>(true, mSmallImages.value(pHost));
    } else {
        return std::tuple<bool, QString>(false, QStringLiteral("Discord integration is not available"));
    }
}

bool Discord::setSmallImageText(Host* pHost, const QString& text)
{
    if (mLoaded) {
        mRawMode[pHost] = true;
        mSmallImageTexts[pHost] = text;
        UpdatePresence();
        return true;
    }
    return false;
}

std::tuple<bool, QString> Discord::getSmallImageText(Host* pHost) const
{
    if (mLoaded) {
        return std::tuple<bool, QString>(true, mSmallImageTexts.value(pHost));
    } else {
        return std::tuple<bool, QString>(false, QStringLiteral("Discord integration is not available"));
    }
}

void Discord::timerEvent(QTimerEvent* event)
{
    Q_UNUSED(event);

    if (mLoaded) {
        Discord_RunCallbacks();
    }
}

void Discord::handleDiscordReady(const DiscordUser* request)
{
    Discord::smReadWriteLock.lockForWrite(); // Will block until gets lock
    Discord::smUserName = QString::fromUtf8(request->username);
    Discord::smUserId = QString::fromUtf8(request->userId);
    Discord::smDiscriminator = QString::fromUtf8(request->discriminator);
    Discord::smAvatar = QString::fromUtf8(request->avatar);
    Discord::smReadWriteLock.unlock();

    qDebug() << "Discord Ready received - user:" << Discord::smUserName << "descriminator:" << Discord::smDiscriminator;
    qDebug() << "                       userId:" << Discord::smUserId << "avatar:" << Discord::smAvatar;

    // Must use indirection via mudlet pointer because UpdatePresence is not a static method:
    mudlet::self()->mDiscord.UpdatePresence();
}

const QStringList& Discord::getDiscordUserDetails() const
{
    QStringList results;
    if (Discord::smReadWriteLock.tryLockForRead()) {
        results << Discord::smUserName << Discord::smUserId << Discord::smDiscriminator << Discord::smAvatar;
        // Make a deep copy whilst we hold a lock on the details to avoid the
        // writer {handleDiscordReady(...)} having to invoking the C-o-W itself.
        results.detach();
        Discord::smReadWriteLock.unlock();
    }

    return results;
}

void Discord::handleDiscordDisconnected(int errorCode, const char* message)
{
    qWarning() << "Discord disconnected - code:" << errorCode << "message:" << message;
}

void Discord::handleDiscordError(int errorCode, const char* message)
{
    qWarning() << "Discord error - code:" << errorCode << "message:" << message;
}

void Discord::handleDiscordJoinGame(const char* joinSecret)
{
    qDebug() << "Discord JoinGame received with secret:" << joinSecret;
}

void Discord::handleDiscordSpectateGame(const char* spectateSecret)
{
    qDebug() << "Discord SpectateGame received with secret:" << spectateSecret;
}

void Discord::handleDiscordJoinRequest(const DiscordUser* request)
{
    qDebug() << "Discord JoinRequest received from user:" << request->username << "userId:" << request->userId;
    qDebug() << "                         descriminator:" << request->discriminator << "avatar:" << request->avatar;
}

void Discord::UpdatePresence()
{
    if (!mLoaded) {
        return;
    }

    auto pHost = mudlet::self()->getActiveHost();
    if (!pHost) {
        return;
    }

    // Need to establish which presence to use - will be null if it has not been overridden:
    QString presenceId = mHostPresenceIds.value(pHost);

    if (mPresencePtrs.isEmpty()) {
        // First time only - with no localDiscordPresence in collection,
        // must just create the default one:
        localDiscordPresence* pTempPresence = new localDiscordPresence;
        mPresencePtrs.insert(QString(), pTempPresence);
    }

    // If the localDiscordPresence presenceId is NOT present in the existing
    // QMap then this will return a nullptr:
    localDiscordPresence* pDiscordPresence = nullptr;
    if (presenceId.isEmpty()) {
        pDiscordPresence = mPresencePtrs.value(nullptr);
        // Reset the empty presenceId to the one that belongs to Mudlet:
        presenceId = mHostPresenceIds.value(nullptr);
    } else {
        pDiscordPresence = mPresencePtrs.value(presenceId);

        if (!pDiscordPresence) {
            // So insert a non-default one

            pDiscordPresence = new localDiscordPresence;
            mPresencePtrs.insert(presenceId, pDiscordPresence);
        }
    }

    if (mCurrentPresenceId != presenceId) {
        // It has changed - must shutdown and reopen the library instance with
        // the alternate presence Id:
        qDebug() << "Discord::UpdatePresence() INFO - calling Discord_Shutdown()";
        Discord_Shutdown();

        qDebug().nospace().noquote() << "Discord::UpdatePresence() INFO - calling Discord_Initialize(\"" << presenceId << "\", mpHandlers, 0)";
        // Using toUtf8() but only for paranoic reasons - toLatin1() would
        // probably work:
        Discord_Initialize(presenceId.toUtf8().constData(), mpHandlers, 0);
        mCurrentPresenceId = presenceId;
    }

    if (mRawMode.value(pHost, false)) {
        pDiscordPresence->setDetailText(mDetailTexts.value(pHost));
        pDiscordPresence->setStateText(mStateTexts.value(pHost));
        pDiscordPresence->setLargeImageKey(mLargeImages.value(pHost));
        pDiscordPresence->setLargeImageText(mLargeImageTexts.value(pHost));
        pDiscordPresence->setSmallImageKey(mSmallImages.value(pHost));
        pDiscordPresence->setSmallImageText(mSmallImageTexts.value(pHost));
    } else {
        int port = pHost->getPort();
        QString url = pHost->getUrl();
        QString gameName = mGamesNames.value(pHost).toUtf8();
        if (!gameName.isEmpty()) {
            // CHECKME: Consider changing to show {"%1 (Connected)"|"%1 (Connecting)"|"%1 (Disconnected)"} to reflect connection status...
            mDetailTexts[pHost] = tr("Playing %1",
                                     "This is an awkward case - it is visible to others who may be using various languages "
                                     "but it is not appropriate to NOT translate it according to the current user's "
                                     "requirements - we have to assume that they will be playing using a language "
                                     "that their collegues will also be using and would expect to see..."
                                     "Also, the introduction (the fixed text part, NOT the parameter), really wants to be "
                                     "the shortest possible word so that it takes up the least amount of space on the "
                                     "player's Discord Icon/Rich Presence report.").arg(gameName);
            pDiscordPresence->setDetailText(mDetailTexts.value(pHost));

            mLargeImages[pHost] = gameName.toLower();
            pDiscordPresence->setLargeImageKey(mLargeImages.value(pHost));

            if (!pHost->mDiscordHideAddress) {
                mLargeImageTexts[pHost] = QStringLiteral("%1:%2").arg(url, QString::number(port));
                pDiscordPresence->setLargeImageText(mLargeImageTexts.value(pHost));
            }
        }

        if (!pHost->mDiscordHideCurrentArea && !mAreas.value(pHost).isEmpty()) {
            mStateTexts[pHost] = mAreas.value(pHost);
            pDiscordPresence->setStateText(mAreas.value(pHost));
        }

        // list of known games - if the user sets an unknown game, have a heuristic
        // still display their character icon
        bool knownGame = mGamesNames.contains(pHost) && mKnownGames.contains(mGamesNames.value(pHost).toLower());

        if (!pHost->mDiscordHideCharacterIcon && !mCharacterIcons.value(pHost).isEmpty()) {
            //  the game is unknown, set the small image as the big one so at least something shows
            if (knownGame) {
                mSmallImages[pHost] = mCharacterIcons.value(pHost).toLower().trimmed();
                pDiscordPresence->setSmallImageKey(mSmallImages.value(pHost));
            } else {
                mLargeImages[pHost] = mCharacterIcons.value(pHost).toLower().trimmed();
                pDiscordPresence->setLargeImageKey(mLargeImages.value(pHost));
            }
        }

        if (!pHost->mDiscordHideCharacterText && !mCharacters.value(pHost).isEmpty()) {
            if (knownGame) {
                mSmallImageTexts[pHost] = mCharacters.value(pHost);
                pDiscordPresence->setSmallImageText(mCharacters.value(pHost));
            } else {
                if (!pHost->mDiscordHideAddress) {
                    mLargeImageTexts[pHost] = QStringLiteral("%1:%2 | %3").arg(url, QString::number(port), mCharacters.value(pHost));
                } else {
                    mLargeImageTexts[pHost] = mCharacters.value(pHost);
                }
                pDiscordPresence->setLargeImageText(mLargeImageTexts.value(pHost));
            }
        }
    }

    // At present confine ourselves to showing an elapsed time since profile
    // load - will change to be connected time once initial bugs ironed out
    // and then allow both to be manipulated from lua...
    pDiscordPresence->setStartTimeStamp(mStartTimes.value(pHost));
    pDiscordPresence->setEndTimeStamp(0);

    // Convert our stored presence into the format that the RPC library wants:
    qDebug() << "Discord::UpdatePresence() INFO - calling Discord_UpdatePresence(...) using:" << *pDiscordPresence;

    DiscordRichPresence convertedPresence(pDiscordPresence->convert());
    Discord_UpdatePresence(&convertedPresence);
}

bool Discord::gameIntegrationSupported(const QString& address)
{
    // Handle using localhost as an off-line testing case
    if (address == QLatin1String("localhost") || address == QLatin1String("127.0.0.1") || address == QLatin1String("::1")) {

        return true;
    }

    // Handle the cases where the server url contains the "well-known" Server
    // name - that being the key of the QHash mKnownGames:
    if (mKnownGames.contains(address)) {
        return true;
    }

    // Handle the remaining cases where the known URL is something else - like
    // say a fixed IP address stored as a member of the value for the QHash
    // mKnownGames:
    QHashIterator<QString, QVector<QString>> itServer(mKnownGames);
    while (itServer.hasNext()) {
        itServer.next();
        QVectorIterator<QString> itUrl(itServer.value());
        while (itUrl.hasNext()) {
            if (itUrl.next().contains(address)) {
                return true;
            }
        }
    }

    // Oh dear, the given address does not match anything we know about
    return false;
}

bool Discord::libraryLoaded()
{
    return mLoaded;
}

void Discord::slot_handleGameConnection(Host* pHost)
{
    mStartTimes[pHost] = static_cast<int64_t>(std::time(nullptr));
}

void Discord::slot_handleGameDisconnection(Host* pHost)
{
    mStartTimes[pHost] = 0;
}

// AFAICT A Discord Application Id is an 18 digit number - this seems to
// corresponds to an unsigned long long int (a.k.a. a quint64, or qulonglong)
// but we won't assume anything here and stick to treating it as a QString.
bool Discord::setPresence(Host* pHost, const QString& text)
{
    // Note what the current presenceId is for the given Host - will be an empty
    // string if not overridden from the default Mudlet one:
    if (text.isEmpty()) {
        // An empty or null string is the signal to switch back to default
        // "Mudlet" presence - and always succeeds
        mHostPresenceIds.remove(pHost);

        return true;
    }

    QString oldPresenceId = mHostPresenceIds.value(pHost);
    if (oldPresenceId == text) {
        // No change so do nothing
        return true;
    }


    bool ok = false;
    if (text.toLongLong(&ok) && ok) {
        // Got something that makes a non-zero number - so assume it is ok
        mHostPresenceIds[pHost] = text;
        return true;

    } else {

        return false;
    }
}

// Check to see if the presence is still needed:
//if ((!text.isEmpty()) && !mHostPresenceIds.values().contains(text)) {
//    // No other Host instance is using the given Presence Id and it is
//    // not the default one - so remove the
//}

DiscordRichPresence localDiscordPresence::convert() const
{
    return DiscordRichPresence { mState,
                                 mDetails,
                                 mStartTimestamp,
                                 mEndTimestamp,
                                 mLargeImageKey,
                                 mLargeImageText,
                                 mSmallImageKey,
                                 mSmallImageText,
                                 mPartyId,
                                 mPartySize,
                                 mPartyMax,
                                 mMatchSecret,
                                 mJoinSecret,
                                 mSpectateSecret,
                                 mInstance };
}

void localDiscordPresence::setDetailText(const QString& text)
{
    // Set the amount to be copied to be one less than the size of the buffer
    // so that the last byte is untouched and always contains the initial
    // null that was placed there when the thing pointed to by
    // pDiscordPresence was created:

    strncpy(mDetails, text.toUtf8().constData(), 127);
}

void localDiscordPresence::setStateText(const QString& text)
{
    strncpy(mState, text.toUtf8().constData(), 127);
}

void localDiscordPresence::setLargeImageText(const QString& text)
{
    strncpy(mLargeImageText, text.toUtf8().constData(), 127);
}

void localDiscordPresence::setLargeImageKey(const QString& text)
{
    strncpy(mLargeImageKey, text.toUtf8().constData(), 31);
}

void localDiscordPresence::setSmallImageText(const QString& text)
{
    strncpy(mSmallImageText, text.toUtf8().constData(), 127);
}

void localDiscordPresence::setSmallImageKey(const QString& text)
{
    strncpy(mSmallImageKey, text.toUtf8().constData(), 31);
}


void localDiscordPresence::setJoinSecret(const QString& text)
{
    strncpy(mJoinSecret, text.toUtf8().constData(), 127);
}

void localDiscordPresence::setMatchSecret(const QString& text)
{
    strncpy(mMatchSecret, text.toUtf8().constData(), 127);
}

void localDiscordPresence::setSpectateSecret(const QString& text)
{
    strncpy(mSpectateSecret, text.toUtf8().constData(), 127);
}

bool Discord::isUsingDefaultDiscordPresence(Host* pHost) const
{
    return (! mHostPresenceIds.contains(pHost));
}
