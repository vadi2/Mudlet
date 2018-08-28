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
#include "post_guard.h"

QReadWriteLock Discord::smReadWriteLock;

QString Discord::smUserName;
QString Discord::smUserId;
QString Discord::smDiscriminator;
QString Discord::smAvatar;
const QString Discord::mMudletApplicationId = QLatin1String("450571881909583884");

Discord::Discord(QObject* parent)
: QObject(parent)
, mLoaded{}
// For details see https://discordapp.com/developers/docs/rich-presence/how-to#initialization
// Initialise with a nullptr one with Mudlet's own ID
// N. B. for testing the following MUDs have registered:
// "midmud"  is "460618737712889858", has "server-icon", "exventure" and "mudlet" icons
// "carinus" is "438335628942376960", has "server-icon" and "mudlet" icons
// "wotmud"  is "464945517156106240", has "mudlet", "ajar_(red|green|yellow|blue|white|grey|brown)"
, mHostApplicationIDs{{nullptr, mMudletApplicationId}}
// lowercase list of known games
// {game name, {game addresses}}
// The values are not currently used!
, mKnownGames{{"midmud", {"midmud.com"}},
              {"wotmud", {"game.wotmud.org"}},
              {"luminari", {"luminarimud.com"}},
              {"achaea", {"achaea.com", "iron-ach.ironrealms.com"}},
              {"aetolia", {"aetolia.com", "iron-aet.ironrealms.com"}},
              {"imperian", {"imperian.com", " iron-imp.ironrealms.com"}},
              {"lusternia", {"lusternia.com", "iron-lus.ironrealms.com"}},
              {"starmourn", {"starmourn.com"}}}
{
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
        qDebug() << "Could not find Discord library - searched in:";
        for (auto& libraryPath : qApp->libraryPaths()) {
            qDebug() << "    " << libraryPath;
        }
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
    qDebug().nospace().noquote() << "Discord::Discord(...) INFO - calling Discord_Initialize(\"" << mHostApplicationIDs.value(nullptr) << "\", mpHandlers, 0)";
    Discord_Initialize(mHostApplicationIDs.value(nullptr).toUtf8().constData(), mpHandlers, 0);
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

// For all the setters below the caller is supposed to check that they have the
// permission to do the operation
void Discord::setDetailText(Host* pHost, const QString& text)
{
    mDetailTexts[pHost] = text;
    if (mLoaded) {
        UpdatePresence();
    }
}

void Discord::setStateText(Host* pHost, const QString& text)
{
    mStateTexts[pHost] = text;
    if (mLoaded) {
        UpdatePresence();
    }
}

void Discord::setLargeImage(Host* pHost, const QString& text)
{
    mLargeImages[pHost] = text;
    if (mLoaded) {
        UpdatePresence();
    }
}

void Discord::setLargeImageText(Host* pHost, const QString& text)
{
    mLargeImageTexts[pHost] = text;
    if (mLoaded) {
        UpdatePresence();
    }
}

void Discord::setSmallImage(Host* pHost, const QString& text)
{
    mSmallImages[pHost] = text;
    if (mLoaded) {
        UpdatePresence();
    }
}

void Discord::setSmallImageText(Host* pHost, const QString& text)
{
    mSmallImageTexts[pHost] = text;
    if (mLoaded) {
        UpdatePresence();
    }
}

void Discord::setStartTimeStamp(Host* pHost, int64_t epochTimeStamp)
{
    mStartTimes[pHost] = epochTimeStamp;
    mEndTimes.remove(pHost);
    if (mLoaded) {
        UpdatePresence();
    }
}

void Discord::setEndTimeStamp(Host* pHost, int64_t epochTimeStamp)
{
    mEndTimes[pHost] = epochTimeStamp;
    mStartTimes.remove(pHost);
    if (mLoaded) {
        UpdatePresence();
    }
}

void Discord::setParty(Host* pHost, int partySize)
{
    int validPartySize = qMax(0, partySize);
    if (validPartySize) {
        // Is more than zero:
        if (mPartyMax.value(pHost) < validPartySize) {
            mPartyMax[pHost] = validPartySize;
        }

        mPartySize[pHost] = validPartySize;
    } else if (mPartyMax.contains(pHost)) {
        // There is a max size set - so zero this value
        mPartySize[pHost] = 0;
    } else {
        // There isn't a party size set so remove this (zero) value
        mPartySize.remove(pHost);
    }
    if (mLoaded) {
        UpdatePresence();
    }
}

void Discord::setParty(Host* pHost, int partySize, int partyMax)
{
    int validPartySize = qMax(0, partySize);
    int validPartyMax = qMax(0, partyMax);

    if (validPartyMax) {
        // We have a party max size that is a positive number - so use the
        // largest of it and the size as the maximum:
        mPartyMax[pHost] = qMax(validPartySize, validPartyMax);
        mPartySize[pHost] = validPartySize;
    } else {
        // We have explicitly set the party maximum size to 0 (or less) - so
        // clear things:
        mPartySize.remove(pHost);
        mPartyMax.remove(pHost);
    }
    if (mLoaded) {
        UpdatePresence();
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

QStringList Discord::getDiscordUserDetails() const
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

    if (!pHost->discordUserIdMatch(Discord::smUserName, Discord::smDiscriminator)) {
        // Oh dear - the current Discord User does not match the required user
        // details (if set) - must abort
        return;
    }

    // Need to establish which presence to use - will be null if it has not been overridden:
    QString applicationID = mHostApplicationIDs.value(pHost);

    if (mPresencePtrs.isEmpty()) {
        // First time only - with no localDiscordPresence in collection,
        // must just create the default one:
        localDiscordPresence* pTempPresence = new localDiscordPresence;
        mPresencePtrs.insert(QString(), pTempPresence);
    }

    // If the localDiscordPresence applicationID is NOT present in the existing
    // QMap then this will return a nullptr:
    localDiscordPresence* pDiscordPresence = nullptr;
    if (applicationID.isEmpty()) {
        pDiscordPresence = mPresencePtrs.value(nullptr);
        // Reset the empty applicationID to the one that belongs to Mudlet:
        applicationID = mHostApplicationIDs.value(nullptr);
    } else {
        pDiscordPresence = mPresencePtrs.value(applicationID);

        if (!pDiscordPresence) {
            // So insert a non-default one

            pDiscordPresence = new localDiscordPresence;
            mPresencePtrs.insert(applicationID, pDiscordPresence);
        }
    }

    if (mCurrentApplicationId != applicationID) {
        // It has changed - must shutdown and reopen the library instance with
        // the alternate application id:
        qDebug() << "Discord::UpdatePresence() INFO - calling Discord_Shutdown()";
        Discord_Shutdown();

        qDebug().nospace().noquote() << "Discord::UpdatePresence() INFO - calling Discord_Initialize(\"" << applicationID << "\", mpHandlers, 0)";
        // Using toUtf8() but only for paranoic reasons - toLatin1() would
        // probably work:
        Discord_Initialize(applicationID.toUtf8().constData(), mpHandlers, 0);
        mCurrentApplicationId = applicationID;
    }

    if (pHost->mDiscordAccessFlags & Host::DiscordSetDetail) {
        pDiscordPresence->setDetailText(mDetailTexts.value(pHost));
    } else {
        pDiscordPresence->setDetailText(QString());
    }

    if (pHost->mDiscordAccessFlags & Host::DiscordSetState) {
        pDiscordPresence->setStateText(mStateTexts.value(pHost));
    } else {
        pDiscordPresence->setStateText(QString());
    }

    if (pHost->mDiscordAccessFlags & Host::DiscordSetLargeIcon) {
        pDiscordPresence->setLargeImageKey(mLargeImages.value(pHost));
    } else {
        pDiscordPresence->setLargeImageKey(QString());
    }

    if (pHost->mDiscordAccessFlags & Host::DiscordSetLargeIconText) {
        pDiscordPresence->setLargeImageText(mLargeImageTexts.value(pHost));
    } else {
        pDiscordPresence->setLargeImageText(QString());
    }

    if (pHost->mDiscordAccessFlags & Host::DiscordSetSmallIcon) {
        pDiscordPresence->setSmallImageKey(mSmallImages.value(pHost));
    }
    if (pHost->mDiscordAccessFlags & Host::DiscordSetSmallIconText) {
        pDiscordPresence->setSmallImageText(mSmallImageTexts.value(pHost));
    }

    if (mPartyMax.value(pHost) && (pHost->mDiscordAccessFlags & Host::DiscordSetPartyInfo)) {
        pDiscordPresence->setPartySize(mPartySize.value(pHost));
        pDiscordPresence->setPartyMax(mPartyMax.value(pHost));
    } else {
        pDiscordPresence->setPartySize(0);
        pDiscordPresence->setPartyMax(0);
    }

    if (pHost->mDiscordAccessFlags & Host::DiscordSetTimeInfo) {
        if (mEndTimes.value(pHost)) {
            pDiscordPresence->setEndTimeStamp(mEndTimes.value(pHost));
            pDiscordPresence->setStartTimeStamp(0);
        } else {
            pDiscordPresence->setEndTimeStamp(0);
            pDiscordPresence->setStartTimeStamp(mStartTimes.value(pHost, 0));
        }
    }

    // Convert our stored presence into the format that the RPC library wants:
    qDebug() << "Discord::UpdatePresence() INFO - calling Discord_UpdatePresence(...) using:" << *pDiscordPresence;

    DiscordRichPresence convertedPresence(pDiscordPresence->convert());
    Discord_UpdatePresence(&convertedPresence);
}

// TODO: This is likely to need development as more use cases come to light
QString Discord::deduceGameName(const QString& address)
{
    // Handle using localhost as an off-line testing case
    if (address == QLatin1String("localhost") || address == QLatin1String("127.0.0.1") || address == QLatin1String("::1")) {

        return QLatin1String("localhost");
    }

    // Handle the cases where the server url contains the "well-known" Server
    // name - that being the key of the QHash mKnownGames:
    if (mKnownGames.contains(address)) {
        return address;
    }

    // Do a bit of URL processing on the (potentially) host url:
    QString otherName;
    switch (address.count(QChar('.'))) {
    default:
        // Too complex - abandon
        qDebug().noquote().noquote() << "Discord::deduceGameName(\"" << address << "\") WARN - Unable to deduce MUD name from given address.";
        break;
    case 2:
    {
            // three terms - assume last is a TLD so remove it but the first may be significant

        QStringList fragments = address.split(QChar('.'));
        fragments.removeLast();
        otherName = fragments.join(QLatin1String("."));
        if (otherName.startsWith(QLatin1String("game."))) {
            // WoTMUD type case - so take remaing term in the middle of original
            otherName = otherName.split(QChar('.')).last();
            break;
        } else if (otherName.startsWith(QLatin1String("www."))) {
            // Error(?) in entering details so that a web-server name was give:
            otherName = otherName.split(QChar('.')).last();
            break;
        }
    }
        qDebug().noquote().noquote() << "Discord::deduceGameName(\"" << address << "\") WARN - Unable to deduce MUD name from given address - even after discarding last element to consider just: \"" << otherName << "\".";
        otherName.clear();
        break;
    case 1:
        // two terms - assume last is a TLD so remove it
        otherName = address.split(QChar('.')).first();
        break;
    case 0:
        // single term no need to split it
        otherName = address;
        break;
    }

    if (address.endsWith(QStringLiteral(".com"))) {
        otherName = address.left(address.length() - 4);
    } else if (address.endsWith(QStringLiteral(".de"))) {
        // Handle avalon.de case
        otherName = address.left(address.length() - 4);
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
                return itServer.key();
            }
        }
    }

    // This may be an empty string but it is the best guess otherwise:
    return otherName;
}

// Returns true in First if this is a MUD we know about (and have an Icon for in
// on the Mudlet Discord erver!) and the deduced name in Second - if the
// first is true.
QPair<bool, QString> Discord::gameIntegrationSupported(const QString& address)
{
    QString deducedName = deduceGameName(address);

    // Handle using localhost as an off-line testing case
    if (deducedName == QLatin1String("localhost")) {

        return qMakePair(true, deducedName);
    } else {
        return qMakePair((!deducedName.isEmpty() && mKnownGames.contains(deducedName)), deducedName);;
    }
}

bool Discord::libraryLoaded()
{
    return mLoaded;
}

// AFAICT A Discord Application Id is an unsigned long long int (a.k.a. a
// quint64, or qulonglong)
bool Discord::setApplicationID(Host* pHost, const QString& text)
{
    QString oldID = mHostApplicationIDs.value(pHost);
    if (oldID == text) {
        // No change so do nothing
        return true;
    }

    // Note what the current app ID is for the given Host - will be an empty
    // string if not overridden from the default Mudlet one:
    if (text.isEmpty()) {
        // An empty or null string is the signal to switch back to default
        // "Mudlet" presence - and always succeeds
        mHostApplicationIDs.remove(pHost);
        pHost->setDiscordApplicationID(QString());
        UpdatePresence();

        return true;
    }

    bool ok = false;
    if (text.toLongLong(&ok) && ok) {
        // Got something that makes a non-zero number - so assume it is ok
        mHostApplicationIDs[pHost] = text;
        pHost->setDiscordApplicationID(text);
        UpdatePresence();

        return true;

    } else {

        return false;
    }
}

// Returns Host set app ID or the default Mudlet one if none set for the
// specific Host:
QString Discord::getApplicationId(Host* pHost) const
{
    return mHostApplicationIDs.value(pHost, mHostApplicationIDs.value(nullptr));
}

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

bool Discord::usingMudletsDiscordID(Host* pHost) const
{
    return (!mHostApplicationIDs.contains(pHost));
}

bool Discord::discordUserIdMatch(Host* pHost) const
{
    return pHost->discordUserIdMatch(Discord::smUserName, Discord::smDiscriminator);
}
