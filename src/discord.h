#ifndef DISCORD_H
#define DISCORD_H

#include "Host.h"

#include "pre_guard.h"
#include <functional>
#include <utility>
#include <QDebug>
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

class Discord : public QObject
{
    Q_OBJECT

public:
    explicit Discord(QObject *parent = nullptr);    
    ~Discord() override;

    void UpdatePresence();
    std::tuple<bool, QString> setGame(Host* pHost, const QString& name);
    bool setArea(Host* pHost, const QString& area);
    bool setCharacterIcon(Host* pHost, const QString& icon);
    bool setCharacter(Host* pHost, const QString& text);
    bool gameIntegrationSupported(const QString& address);
    bool libraryLoaded();

private:
    // These are function pointers to functions located in the Discord RPC library:
    std::function<void(const char*, DiscordEventHandlers*, int, const char*)> Discord_Initialize;
    std::function<void(const DiscordRichPresence* presence)> Discord_UpdatePresence;
    std::function<void(void)> Discord_RunCallbacks;
    std::function<void(void)> Discord_Shutdown;

    QMap<Host*, QString>mGamesNames;  // Used to populate Discord's "details" as "Playing %s" (first line of text) and "Large icon key" from lower case version
    QMap<Host*, QString>mAreas; // Used to populate Discord's "State" first part of second line of text (followed by X of Y numbers)
    QMap<Host*, QString>mCharacterIcons; // Used to populate Discord's " "Small icon key"
    QMap<Host*, QString>mCharacters; // Used to populate "Small image text"
    QMap<Host*, int64_t>mStartTimes;

    QScopedPointer<QLibrary> mpLibrary;
    bool mLoaded;

    QHash<QString, QVector<QString>> mKnownGames;
    QVector<QString> mKnownAddresses;

    static void handleDiscordReady(const DiscordUser* request);
    static void handleDiscordDisconnected(int errorCode, const char* message);
    static void handleDiscordError(int errorCode, const char* message);
    static void handleDiscordJoinGame(const char* joinSecret);
    static void handleDiscordSpectateGame(const char* spectateSecret);
    static void handleDiscordJoinRequest(const DiscordUser* request);

    void timerEvent(QTimerEvent *event) override;

signals:

public slots:
    void slot_handleGameConnection(Host*);
    void slot_handleGameDisconnection(Host*);

};

#endif // DISCORD_H
