////////////////////////////////////////////////////////////////////////////////
//
// Copyright (c) 2026 Ripose
//
// This file is part of Memento.
//
// Memento is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, version 2 of the License.
//
// Memento is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with Memento.  If not, see <https://www.gnu.org/licenses/>.
//
////////////////////////////////////////////////////////////////////////////////

#pragma once
#include <QObject>
#include <QUrl>
#include <QVariantMap>
#ifdef MEMENTO_SYSTEM_QCORO
#include <QCoroTask>
#include <QCoroQmlTask>
#else
#include <qcoro/qcorotask.h>
#include <qcoro/qml/qcoroqmltask.h>
#endif
class Context;
class MigakuClientTest;

/** Exports episode screenshots and sentence audio for manual use in Migaku. */
class MigakuClient : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(bool enabled READ enabled WRITE setEnabled NOTIFY enabledChanged)
    Q_PROPERTY(QUrl exportFolder READ exportFolder WRITE setExportFolder NOTIFY exportFolderChanged)
    Q_PROPERTY(QString exportFolderPath READ exportFolderPath NOTIFY exportFolderChanged)
    Q_PROPERTY(QString lastExport READ lastExport NOTIFY lastExportChanged)
public:
    explicit MigakuClient(Context *context, QObject *parent = nullptr);
    bool busy() const noexcept;
    bool enabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    QUrl exportFolder() const { return m_exportFolder; }
    QString exportFolderPath() const { return m_exportFolder.toLocalFile(); }
    void setExportFolder(const QUrl &folder);
    QString lastExport() const { return m_lastExport; }
    Q_INVOKABLE bool openExportFolder();
    Q_INVOKABLE QCoro::QmlTask exportCurrentSubtitle();
signals:
    void enabledChanged();
    void exportFolderChanged();
    void lastExportChanged();
    void busyChanged(bool value);
    void filesExported(const QString &baseName);
    void exportFailed(const QString &error);
private:
    friend class MigakuClientTest;
    struct CardData;
    struct MediaData;
    QCoro::Task<QVariantMap> exportCurrentSubtitleAsync();
    void setBusy(bool value);
    static QString episodeStem(QString title);
    static QVariantMap saveMediaPair(const QString &folder, const QString &episode,
                                    double position, const QString &image, const QString &audio);
    static bool prepareClipRange(double requestedStart, double requestedEnd,
                                 double mediaDuration, double *start, double *end) noexcept;
    static bool fileWithinLimit(const QString &path, qint64 maximumSize);
    static QVariantMap errorResult(const QString &error);
    Context *m_context;
    QUrl m_exportFolder;
    QString m_lastExport;
    bool m_enabled{false};
    quint64 m_generation{0};
    bool m_busy{false};
};
