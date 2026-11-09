#pragma once

#include <QObject>
#include <QVariantList>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QPointer>
#include <QDateTime>
#include <functional>
class QNetworkReply;

class Context;
class KitsunekkoClientTest;

// Searches the online Ajatt-Tools/kitsunekko-mirror catalog; downloads selected files only.
class KitsunekkoClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QVariantList searchResults READ searchResults NOTIFY changed)
    Q_PROPERTY(QVariantList fileResults READ fileResults NOTIFY changed)
    Q_PROPERTY(QString selectedEntryName READ selectedEntryName NOTIFY changed)
    Q_PROPERTY(int selectedEpisode READ selectedEpisode NOTIFY changed)
public:
    explicit KitsunekkoClient(Context *context, QObject *parent = nullptr);
    bool busy() const { return m_busy; }
    QString status() const { return m_status; }
    QVariantList searchResults() const { return m_entries; }
    QVariantList fileResults() const { return m_files; }
    QString selectedEntryName() const { return m_entryName; }
    int selectedEpisode() const { return m_episode; }
    Q_INVOKABLE void fetchLinkedForCurrentMedia(bool manual = false);
    Q_INVOKABLE void refreshCatalog(const QString &query);
    Q_INVOKABLE void search(const QString &query);
    Q_INVOKABLE void selectEntry(int index, int episode = -1);
    Q_INVOKABLE void openLinkedEntry(const QVariantMap &link, int episode, bool all = false);
    Q_INVOKABLE void selectEntryAllFiles(int index, int episode);
    Q_INVOKABLE void attachResult(int index);
    Q_INVOKABLE int preferredFileIndex(int episode) const;
    Q_INVOKABLE void clearSearch();
    Q_INVOKABLE void cancel();
signals:
    void changed();
    void failed(const QString &message);
    void subtitleAttached(const QString &fileName, const QString &entryName);
private:
    friend class KitsunekkoClientTest;
    void get(const QUrl &url, std::function<void(const QByteArray &)> done);
    void loadCategory(int index);
    bool loadCachedCatalog();
    void startCatalogLoad();
    void resolveLinkedCatalog(const QVariantMap &link, int episode, bool force = false,
                              bool automatic = true, bool all = false);
    void finishLinkedCatalog(bool fresh);
    void filterTitles();
    static QVariantList parseFiles(const QByteArray &data, const QString &prefix, int episode);
    void selectFiles(int index, int episode, bool all, bool persist = true,
                     bool automatic = false, bool catalogFresh = false);
    static int automaticFileIndex(const QVariantList &files, int episode,
                                 const QVariantMap &rule,
                                 const QString &exactFilename = {});
    void attachResult(int index, bool automatic);
    void attachPath(const QString &path, const QVariantMap &file, bool automatic);
    QVariantMap exactSelection(int episode) const;
    void watchMedia();
    void checkAttached();
    void error(const QString &message);
    Context *m_context;
    QString m_status, m_entryName, m_mediaPath, m_pendingPath, m_query;
    QVariantList m_catalog, m_catalogBuild, m_categories;
    QDateTime m_catalogFetched;
    QVariantMap m_catalogLink;
    int m_catalogEpisode{-1};
    bool m_catalogAutomatic{true}, m_catalogAll{false};
    QNetworkAccessManager m_manager;
    QPointer<QNetworkReply> m_reply;
    QVariantList m_entries, m_files;
    QVariantMap m_entryLink, m_pendingFile, m_pendingSelection;
    int m_episode{-1};
    quint64 m_generation{0};
    bool m_busy{false};
    bool m_pendingAutomatic{false};
    QTimer m_attachTimer;
    QMetaObject::Connection m_trackConnection;
    QMetaObject::Connection m_sidConnection;
    QMetaObject::Connection m_mediaConnection;
};
