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

#include "dict/dictionarycontroller.h"

#include <QtConcurrentRun>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSettings>
#include <QTimer>
#include "util/directoryutils.h"


#ifdef MEMENTO_SYSTEM_QCORO
#include <QCoroFuture>
#else
#include <qcoro/core/qcorofuture.h>
#endif // MEMENTO_SYSTEM_QCORO

/* Begin Constructor/Deconstructor */

DictionaryController::DictionaryController(
    Settings *settings, QObject *parent) :
    Dictionary(parent),
    m_settings(settings)
{
    if (m_settings != nullptr)
    {
        m_order.clear();
        const QList<int64_t> &order = m_settings->dictionaryOrder();
        for (qsizetype i = 0; i < order.size(); ++i)
        {
            m_order.emplace(order[i], i);
        }
    }

    updateDictionaries();
    if (!QStandardPaths::isTestModeEnabled()) {
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
        QSettings preferences(DirectoryUtils::getCacheConfig(), QSettings::NativeFormat);
#else
        QSettings preferences;
#endif
        if (!preferences.value("dictionary/defaults-offered", false).toBool()) {
            preferences.setValue("dictionary/defaults-offered", true);
            if (m_dictionaries->items().isEmpty())
                QTimer::singleShot(1000, this, &DictionaryController::installDefaults);
        }
    }
}

DictionaryController::~DictionaryController()
{
    m_imports.clear();
    if (m_download) { m_download->disconnect(this); m_download->abort(); }
    m_importFuture.waitForFinished();
    delete m_archive;
}

/* End Constructor/Deconstructor */
/* Begin Actions */

void DictionaryController::setActivity(const QString &text, double progress)
{
    m_activity = text;
    m_progress = progress;
    emit activityChanged();
}

void DictionaryController::addDictionary(QString path)
{
    const QUrl url(path);
    path = url.isLocalFile() ? url.toLocalFile() : path;
    m_imports.enqueue({QFileInfo(path).fileName(), path, {}});
    if (!m_busy) startNextImport();
}

void DictionaryController::installDefaults()
{
    if (m_busy || modifyingDatabase()) return;
    const QList<ImportJob> defaults{
        {QStringLiteral("Jitendex"), {}, QUrl("https://github.com/stephenmk/stephenmk.github.io/releases/latest/download/jitendex-yomitan.zip")},
        {QStringLiteral("JPDB frequency"), {}, QUrl("https://raw.githubusercontent.com/Kuuuube/yomitan-dictionaries/main/dictionaries/JPDB_v2.2_Frequency_Kana_2024-10-13.zip")}
    };
    for (const auto &job : defaults) {
        const QString match = job.name.startsWith("JPDB") ? QStringLiteral("JPDB") : job.name;
        bool installed = false;
        for (const auto *dictionary : m_dictionaries->items())
            installed |= dictionary->name().contains(match, Qt::CaseInsensitive);
        if (!installed) m_imports.enqueue(job);
    }
    if (m_imports.isEmpty()) { setActivity(tr("Default dictionaries are installed."), 1); return; }
    startNextImport();
}

void DictionaryController::cancelPendingImports()
{
    m_imports.clear();
    if (m_download) m_download->abort();
    else if (m_busy) setActivity(tr("Finishing the current dictionary import…"));
}

void DictionaryController::startNextImport()
{
    if (m_imports.isEmpty()) {
        m_busy = false;
        setActivity(tr("Dictionaries ready."), 1);
        return;
    }
    m_busy = true;
    m_currentImport = m_imports.dequeue();
    if (m_currentImport.url.isEmpty()) { importCurrent(); return; }
    setActivity(tr("Downloading %1…").arg(m_currentImport.name));
    m_archive = new QTemporaryFile(this);
    if (!m_archive->open()) { finishImport(-1); return; }
    QNetworkRequest request(m_currentImport.url);
    request.setTransferTimeout(60000);
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setRawHeader("User-Agent", "Memento-Custom/2.3");
    auto *reply = m_network.get(request);
    m_download = reply;
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        const QByteArray data = reply->readAll();
        if (m_archive->size() + data.size() > 256 * 1024 * 1024 || m_archive->write(data) != data.size()) {
            reply->setProperty("archiveWriteFailed", true);
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        setActivity(tr("Downloading %1 — %2 MB%3").arg(m_currentImport.name)
            .arg(received / 1048576.0, 0, 'f', 1)
            .arg(total > 0 ? tr(" of %1 MB").arg(total / 1048576.0, 0, 'f', 1) : QString()),
            total > 0 ? static_cast<double>(received) / total : -1);
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        m_download = nullptr;
        reply->deleteLater();
        const bool cancelled = reply->error() == QNetworkReply::OperationCanceledError;
        if (reply->error() != QNetworkReply::NoError || reply->property("archiveWriteFailed").toBool()) {
            m_imports.clear();
            m_busy = false;
            setActivity(reply->property("archiveWriteFailed").toBool() ? tr("Dictionary download could not be saved or exceeded the size limit. Retry in Options → Dictionaries.") : cancelled ? tr("Dictionary download cancelled. Retry in Options → Dictionaries.") :
                tr("Dictionary download failed: %1. Retry in Options → Dictionaries.").arg(reply->errorString()));
            delete m_archive; m_archive = nullptr;
            return;
        }
        if (!m_archive->flush()) { finishImport(-1); return; }
        m_currentImport.path = m_archive->fileName();
        m_archive->close();
        importCurrent();
    });
}

void DictionaryController::importCurrent()
{
    setActivity(tr("Importing %1… This may take a few minutes.").arg(m_currentImport.name));
    m_importFuture = QtConcurrent::run(&DatabaseManager::addDictionary, m_db, m_currentImport.path);
    QCoro::connect(QFuture<int>(m_importFuture), this, [this](int result) { finishImport(result); });
}

void DictionaryController::finishImport(int result)
{
    delete m_archive; m_archive = nullptr;
    if (result) {
        m_imports.clear();
        m_busy = false;
        const QString error = result == -1 ? tr("Cannot create or write the dictionary download file.") : m_db->errorCodeToString(result);
        setActivity(tr("Dictionary setup failed: %1").arg(error));
        setLastError(error);
        return;
    }
    updateDictionaries();
    writeOrder();
    startNextImport();
}

void DictionaryController::removeDictionary(int64_t id)
{
    if (m_busy || modifyingDatabase()) return;
    QFuture<int> result = QtConcurrent::run(
        &DatabaseManager::deleteDictionary,
        m_db,
        id
    );
    QCoro::connect(
        std::move(result),
        this,
        [this] (int ret) -> void
        {
            if (ret)
            {
                setLastError(m_db->errorCodeToString(ret));
                return;
            }
            updateDictionaries();
            writeOrder();
        }
    );
}

void DictionaryController::moveDictionary(int from, int to)
{
    if (!dictionaries()->move(from, to))
    {
        return;
    }
    updateOrder();
    writeOrder();
}

/* End Actions */
/* Begin Slots */

void DictionaryController::updateDictionaries()
{
    QList<DictionaryInfo *> dicts = m_db->getDictionaries(this);
    for (DictionaryInfo *info : dicts)
    {
        connect(
            info, &DictionaryInfo::enabledChanged,
            this,
            [this, info] () { handleEnabledChanged(info); },
            Qt::QueuedConnection
        );
    }
    std::sort(
        std::begin(dicts), std::end(dicts),
        [this] (const DictionaryInfo *lhs, const DictionaryInfo *rhs) -> bool
        {
            qsizetype lhsPriority = m_order.contains(lhs->id()) ?
                m_order[lhs->id()] : std::numeric_limits<qsizetype>::max();
            qsizetype rhsPriority = m_order.contains(rhs->id()) ?
                m_order[rhs->id()] : std::numeric_limits<qsizetype>::max();
            return lhsPriority < rhsPriority;
        }
    );
    dictionaries()->setItems(std::move(dicts));
    updateOrder();
}

void DictionaryController::updateOrder()
{
    const QList<DictionaryInfo *> &dicts = dictionaries()->items();

    m_order.clear();
    for (qsizetype i = 0; i < dicts.size(); ++i)
    {
        m_order.emplace(dicts[i]->id(), i);
    }
}

void DictionaryController::writeOrder()
{
    if (m_settings == nullptr)
    {
        return;
    }

    QList<int64_t> items;
    items.reserve(m_dictionaries->items().size());
    for (const DictionaryInfo *item : m_dictionaries->items())
    {
        items.emplaceBack(item->id());
    }
    m_settings->setDictionaryOrder(items);
}

void DictionaryController::handleEnabledChanged(const DictionaryInfo *info)
{
    QFuture<int> result = QtConcurrent::run(
        info->enabled() ?
            &DatabaseManager::enableDictionary :
            &DatabaseManager::disableDictionary,
        m_db,
        info->id()
    );
    QCoro::connect(
        std::move(result),
        this,
        [this] (int ret) -> void
        {
            if (ret)
            {
                setLastError(m_db->errorCodeToString(ret));
                updateDictionaries();
                return;
            }
        }
    );
}

/* End Slots */
/* Begin Properties */

DictionaryInfoModel *DictionaryController::dictionaries() const noexcept
{
    return m_dictionaries;
}

const QString &DictionaryController::lastError() const noexcept
{
    return m_lastError;
}

void DictionaryController::setLastError(const QString &error)
{
    m_lastError = error;
    if (!m_lastError.isEmpty())
    {
        emit lastErrorChanged(m_lastError);
    }
}

/* End Properties */
