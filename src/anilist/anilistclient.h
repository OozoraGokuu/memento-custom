#pragma once
#include <QObject>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QTcpServer>
#include <QTimer>
#include <QSet>
#include <QVariantList>
#include <functional>

class EpisodeFolder;
class MpvPlayer;
class AniListClientTest;

class AniListClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString clientId READ clientId WRITE setClientId NOTIFY changed)
    Q_PROPERTY(QString username READ username NOTIFY changed)
    Q_PROPERTY(bool connected READ connected NOTIFY changed)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY changed)
    Q_PROPERTY(int threshold READ threshold WRITE setThreshold NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QVariantList results READ results NOTIFY changed)
    Q_PROPERTY(QString currentKey READ currentKey NOTIFY changed)
    Q_PROPERTY(QString currentTitle READ currentTitle NOTIFY changed)
    Q_PROPERTY(int currentEpisode READ currentEpisode NOTIFY changed)
    Q_PROPERTY(int revision READ revision NOTIFY changed)
    Q_PROPERTY(int pendingCount READ pendingCount NOTIFY changed)
public:
    explicit AniListClient(EpisodeFolder *library, QObject *parent = nullptr);
    void attachPlayer(MpvPlayer *player);
    QString clientId() const { return m_config.value("client_id").toString(); }
    QString username() const { return m_config.value("username").toString(); }
    bool connected() const { return m_verified; }
    bool enabled() const { return m_config.value("enabled").toBool(false); }
    int threshold() const { return m_config.value("threshold").toInt(80); }
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QVariantList results() const { return m_results; }
    QString currentKey() const { return m_current.value("key").toString(); }
    QString currentTitle() const { return m_current.value("title").toString(); }
    int currentEpisode() const;
    int revision() const { return m_revision; }
    int pendingCount() const;
    void setClientId(const QString &value);
    void setEnabled(bool value);
    void setThreshold(int value);
    Q_INVOKABLE void connectAccount();
    Q_INVOKABLE void disconnectAccount();
    Q_INVOKABLE void testConnection();
    Q_INVOKABLE void search(const QString &query);
    Q_INVOKABLE QVariantMap mapping(const QString &key) const;
    Q_INVOKABLE void linkTitle(const QString &key, int resultIndex, int offset = 0);
    Q_INVOKABLE void unlinkTitle(const QString &key);
    Q_INVOKABLE void setCurrentEpisode(int episode);
    Q_INVOKABLE void syncNow();
    Q_INVOKABLE void retryPending();
    Q_INVOKABLE void clearPending();
signals:
    void changed();
private:
    friend class AniListClientTest;
    using Callback = std::function<void(QJsonObject, QString, bool, int)>;
    using Transport = std::function<void(QString, QJsonObject, QString, Callback)>;
    void request(const QString &query, const QJsonObject &variables, Callback callback,
        const QString &token = QString());
    void authenticate(const QString &token);
    void receiveAuthorization();
    void refreshCurrent(const QString &path);
    void observe(double position, double duration);
    void enqueueCurrent();
    void processQueue();
    bool save();
    void announce(const QString &message);
    static QUrl authorizationUrl(const QString &clientId, const QString &state);
    static int episodeFromName(const QString &name);
    static QJsonObject progressUpdate(const QJsonObject &media, int requested);
    EpisodeFolder *m_library;
    QPointer<MpvPlayer> m_player;
    QNetworkAccessManager m_network;
    QTcpServer m_login;
    QTimer m_loginTimeout;
    QTimer m_retry;
    QJsonObject m_config;
    QVariantMap m_current;
    QVariantList m_results;
    QHash<QString, int> m_episodeOverrides;
    QHash<int, int> m_confirmed;
    QSet<QString> m_rejected;
    QString m_file;
    QString m_status;
    QString m_authState;
    QString m_configPath;
    Transport m_transport;
    int m_generation{0};
    int m_searchGeneration{0};
    int m_revision{0};
    int m_retrySeconds{30};
    bool m_verified{false};
    bool m_busy{false};
    bool m_ready{false};
};
