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
////////////////////////////////////////////////////////////////////////////////

#include "quick/episodefolder.h"

#include <algorithm>

#include <QCollator>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>

#include "util/directoryutils.h"
#include "quick/torrentengine.h"

namespace
{

constexpr const char *LIBRARY_FILE = "episode-library.json";
constexpr const char *LIBRARY_BACKUP_FILE = "episode-library.backup.json";
constexpr const char *LEGACY_PROGRESS_FILE = "episode-progress.json";
constexpr const char *LEGACY_CURRENT_FOLDER = "current-folder";
constexpr const char *LEGACY_WATCHED_BY_FOLDER = "watched-by-folder";
constexpr const char *LEGACY_SETTINGS_GROUP = "episode-folders";
constexpr const char *LEGACY_WATCHED_FILES = "watched-files";
constexpr qsizetype MAX_TORRENT_SIZE = 64 * 1024 * 1024;
constexpr int MAX_BENCODE_DEPTH = 64;
constexpr int MAX_BENCODE_VALUES = 500000;
constexpr qint64 MAX_LIBRARY_SIZE = 16 * 1024 * 1024;
constexpr int MAX_LIBRARY_ENTRIES = 1000;
constexpr int MAX_EPISODES_PER_ENTRY = 10000;
constexpr qint64 MAX_PATH_BYTES_PER_ENTRY = 8 * 1024 * 1024;

const QSet<QString> VIDEO_EXTENSIONS{
    "3gp", "avi", "flv", "m2ts", "m4p", "m4v", "mkv", "mov", "mp2",
    "mp4", "mpe", "mpeg", "mpg", "mpv", "mts", "ogg", "ogm", "ogv",
    "qt", "ts", "vob", "webm", "wmv"
};

QString configFilePath(const char *fileName)
{
    return QDir(DirectoryUtils::getConfigDir()).filePath(fileName);
}

bool readJsonObject(const QString &path, QJsonObject *object)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 ||
        file.size() > MAX_LIBRARY_SIZE)
    {
        return false;
    }

    const QByteArray data = file.read(MAX_LIBRARY_SIZE + 1);
    if (data.size() != file.size())
    {
        return false;
    }
    QJsonParseError error;
    const QJsonDocument document = QJsonDocument::fromJson(data, &error);
    if (error.error != QJsonParseError::NoError || !document.isObject())
    {
        return false;
    }

    *object = document.object();
    return true;
}

bool writeFileAtomically(const QString &path, const QByteArray &data)
{
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) &&
        file.write(data) == data.size() &&
        file.commit();
}

QJsonObject readLibraryObject()
{
    QJsonObject library;
    const auto valid = [](const QJsonObject &object) {
        const QJsonValue entriesValue = object.value(QStringLiteral("entries"));
        if (!entriesValue.isArray() ||
            entriesValue.toArray().size() > MAX_LIBRARY_ENTRIES)
        {
            return false;
        }
        for (const QJsonValue &value : entriesValue.toArray())
        {
            if (!value.isObject())
            {
                continue;
            }
            const QJsonObject entry = value.toObject();
            for (const char *key : {
                    "episodes", "episodeSizes", "torrentFileIndices", "watched"})
            {
                const QJsonValue list = entry.value(QLatin1String(key));
                if (list.isArray() &&
                    list.toArray().size() > MAX_EPISODES_PER_ENTRY)
                {
                    return false;
                }
            }
        }
        return true;
    };
    if (readJsonObject(configFilePath(LIBRARY_FILE), &library) && valid(library))
    {
        return library;
    }
    if (readJsonObject(configFilePath(LIBRARY_BACKUP_FILE), &library) &&
        valid(library))
    {
        return library;
    }
    return {};
}

QSettings makeSettings()
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    return QSettings(DirectoryUtils::getCacheConfig(), QSettings::NativeFormat);
#else
    return QSettings();
#endif
}

QString jsonString(const QJsonObject &object, const char *key)
{
    return object.value(QLatin1String(key)).toString();
}

QStringList jsonStringList(const QJsonValue &value)
{
    QStringList result;
    if (!value.isArray())
    {
        return result;
    }
    qint64 bytes = 0;
    for (const QJsonValue &item : value.toArray())
    {
        const QString string = item.toString();
        const qint64 stringBytes = string.toUtf8().size();
        if (result.size() >= MAX_EPISODES_PER_ENTRY ||
            stringBytes > MAX_PATH_BYTES_PER_ENTRY - bytes)
        {
            break;
        }
        if (item.isString())
        {
            result.emplaceBack(string);
            bytes += stringBytes;
        }
    }
    return result;
}

QList<qint64> jsonIntegerList(const QJsonValue &value)
{
    QList<qint64> result;
    for (const QJsonValue &item : value.toArray())
    {
        if (result.size() >= MAX_EPISODES_PER_ENTRY)
        {
            break;
        }
        if (item.isDouble())
        {
            result.emplaceBack(item.toInteger());
        }
    }
    return result;
}

QList<int> jsonIntList(const QJsonValue &value)
{
    QList<int> result;
    for (const QJsonValue &item : value.toArray())
    {
        if (result.size() >= MAX_EPISODES_PER_ENTRY)
        {
            break;
        }
        if (item.isDouble())
        {
            result.emplaceBack(item.toInt(-1));
        }
    }
    return result;
}

void naturalSort(QStringList &paths)
{
    QCollator collator{QLocale{QLocale::English}};
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(
        paths.begin(),
        paths.end(),
        [&collator](const QString &left, const QString &right) {
            return collator.compare(left, right) < 0;
        }
    );
}

struct BencodeValue
{
    enum class Type
    {
        Invalid,
        Integer,
        Bytes,
        List,
        Dictionary,
    };

    Type type{Type::Invalid};
    qint64 integer{0};
    QByteArray bytes;
    QList<BencodeValue> list;
    QMap<QByteArray, BencodeValue> dictionary;
    qsizetype start{0};
    qsizetype end{0};

    [[nodiscard]] const BencodeValue *member(const QByteArray &key) const
    {
        const auto iterator = dictionary.constFind(key);
        return iterator == dictionary.cend() ? nullptr : &iterator.value();
    }
};

class BencodeParser
{
public:
    explicit BencodeParser(const QByteArray &data) : m_data(data) {}

    [[nodiscard]] bool parse(BencodeValue *value)
    {
        return value != nullptr && parseValue(value, 0) &&
            m_position == m_data.size();
    }

private:
    [[nodiscard]] bool parseValue(BencodeValue *value, int depth)
    {
        if (depth > MAX_BENCODE_DEPTH || m_position >= m_data.size() ||
            ++m_valueCount > MAX_BENCODE_VALUES)
        {
            return false;
        }

        value->start = m_position;
        const char token = m_data.at(m_position);
        if (token == 'i')
        {
            ++m_position;
            const qsizetype end = m_data.indexOf('e', m_position);
            if (end < 0 || end == m_position)
            {
                return false;
            }
            bool ok = false;
            value->integer = m_data.mid(m_position, end - m_position)
                .toLongLong(&ok);
            if (!ok)
            {
                return false;
            }
            value->type = BencodeValue::Type::Integer;
            m_position = end + 1;
        }
        else if (token == 'l')
        {
            ++m_position;
            value->type = BencodeValue::Type::List;
            while (m_position < m_data.size() && m_data.at(m_position) != 'e')
            {
                BencodeValue child;
                if (!parseValue(&child, depth + 1))
                {
                    return false;
                }
                value->list.emplaceBack(std::move(child));
            }
            if (m_position >= m_data.size())
            {
                return false;
            }
            ++m_position;
        }
        else if (token == 'd')
        {
            ++m_position;
            value->type = BencodeValue::Type::Dictionary;
            while (m_position < m_data.size() && m_data.at(m_position) != 'e')
            {
                QByteArray key;
                if (!parseBytes(&key))
                {
                    return false;
                }
                BencodeValue child;
                if (!parseValue(&child, depth + 1))
                {
                    return false;
                }
                value->dictionary.insert(key, std::move(child));
            }
            if (m_position >= m_data.size())
            {
                return false;
            }
            ++m_position;
        }
        else if (token >= '0' && token <= '9')
        {
            value->type = BencodeValue::Type::Bytes;
            if (!parseBytes(&value->bytes))
            {
                return false;
            }
        }
        else
        {
            return false;
        }

        value->end = m_position;
        return true;
    }

    [[nodiscard]] bool parseBytes(QByteArray *bytes)
    {
        const qsizetype colon = m_data.indexOf(':', m_position);
        if (colon < 0 || colon == m_position)
        {
            return false;
        }

        bool ok = false;
        const qlonglong length = m_data.mid(m_position, colon - m_position)
            .toLongLong(&ok);
        if (!ok || length < 0 || length > m_data.size() - colon - 1)
        {
            return false;
        }

        m_position = colon + 1;
        *bytes = m_data.mid(m_position, length);
        m_position += length;
        return true;
    }

    const QByteArray &m_data;
    qsizetype m_position{0};
    int m_valueCount{0};
};

QString decodeText(const BencodeValue *value)
{
    return value != nullptr && value->type == BencodeValue::Type::Bytes ?
        QString::fromUtf8(value->bytes) : QString();
}

QString torrentName(const BencodeValue &info)
{
    QString name = decodeText(info.member("name.utf-8"));
    if (name.isEmpty())
    {
        name = decodeText(info.member("name"));
    }
    return name;
}

QString safeRelativePath(const QStringList &segments)
{
    QStringList cleanSegments;
    cleanSegments.reserve(segments.size());
    for (const QString &segment : segments)
    {
        if (segment.isEmpty() || segment == "." || segment == ".." ||
            segment.contains('/') || segment.contains('\\'))
        {
            return {};
        }
        cleanSegments.emplaceBack(segment);
    }

    const QString path = QDir::cleanPath(cleanSegments.join('/'));
    if (path.isEmpty() || path == "." || path == ".." ||
        path.startsWith("../") || QDir::isAbsolutePath(path))
    {
        return {};
    }
    return path;
}

QString pathFromList(const BencodeValue *pathValue)
{
    if (pathValue == nullptr || pathValue->type != BencodeValue::Type::List)
    {
        return {};
    }

    QStringList segments;
    segments.reserve(pathValue->list.size());
    for (const BencodeValue &segment : pathValue->list)
    {
        if (segment.type != BencodeValue::Type::Bytes)
        {
            return {};
        }
        segments.emplaceBack(QString::fromUtf8(segment.bytes));
    }
    return safeRelativePath(segments);
}

void collectV2Files(
    const BencodeValue &node,
    QStringList path,
    QStringList *files)
{
    if (node.type != BencodeValue::Type::Dictionary || files == nullptr ||
        files->size() >= MAX_BENCODE_VALUES)
    {
        return;
    }

    for (auto iterator = node.dictionary.cbegin();
         iterator != node.dictionary.cend(); ++iterator)
    {
        if (iterator.key().isEmpty())
        {
            const QString relativePath = safeRelativePath(path);
            if (!relativePath.isEmpty() &&
                VIDEO_EXTENSIONS.contains(QFileInfo(relativePath).suffix().toLower()))
            {
                files->emplaceBack(relativePath);
            }
            continue;
        }

        QStringList childPath = path;
        childPath.emplaceBack(QString::fromUtf8(iterator.key()));
        collectV2Files(iterator.value(), std::move(childPath), files);
    }
}

[[maybe_unused]] bool readTorrent(
    const QString &path,
    QString *title,
    QStringList *files,
    QString *id)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 ||
        file.size() > MAX_TORRENT_SIZE)
    {
        return false;
    }

    const QByteArray data = file.readAll();
    BencodeValue root;
    BencodeParser parser(data);
    if (!parser.parse(&root) || root.type != BencodeValue::Type::Dictionary)
    {
        return false;
    }

    const BencodeValue *info = root.member("info");
    if (info == nullptr || info->type != BencodeValue::Type::Dictionary)
    {
        return false;
    }

    *title = torrentName(*info);
    if (title->isEmpty())
    {
        return false;
    }

    const BencodeValue *fileList = info->member("files");
    if (fileList != nullptr && fileList->type == BencodeValue::Type::List)
    {
        for (const BencodeValue &fileValue : fileList->list)
        {
            if (fileValue.type != BencodeValue::Type::Dictionary)
            {
                continue;
            }
            const BencodeValue *pathValue = fileValue.member("path.utf-8");
            if (pathValue == nullptr)
            {
                pathValue = fileValue.member("path");
            }
            const QString relativePath = pathFromList(pathValue);
            if (!relativePath.isEmpty() &&
                VIDEO_EXTENSIONS.contains(QFileInfo(relativePath).suffix().toLower()))
            {
                files->emplaceBack(relativePath);
            }
        }
    }
    else if (const BencodeValue *fileTree = info->member("file tree");
             fileTree != nullptr)
    {
        collectV2Files(*fileTree, {}, files);
    }
    else if (VIDEO_EXTENSIONS.contains(QFileInfo(*title).suffix().toLower()))
    {
        files->emplaceBack(*title);
    }

    files->removeDuplicates();
    naturalSort(*files);
    if (files->isEmpty())
    {
        return false;
    }

    const QByteArray infoBytes = data.mid(info->start, info->end - info->start);
    *id = "torrent-" + QString::fromLatin1(
        QCryptographicHash::hash(infoBytes, QCryptographicHash::Sha1).toHex()
    );
    return true;
}

} // namespace

EpisodeFolder::EpisodeFolder(QObject *parent) :
    QObject(parent), m_torrentEngine(new TorrentEngine(this))
{
    connect(
        m_torrentEngine, &TorrentEngine::metadataReady, this,
        &EpisodeFolder::applyTorrentMetadata
    );
    connect(
        m_torrentEngine, &TorrentEngine::torrentChanged, this,
        [this](const QString &id) {
            const int index = findEntryById(id);
            if (index < 0)
            {
                return;
            }
            emit libraryChanged();
            if (index == m_currentIndex)
            {
                emit selectionChanged();
            }
        }
    );
    connect(
        m_torrentEngine, &TorrentEngine::torrentError, this,
        [this](const QString &, const QString &message) {
            setLastError(message);
            emit errorOccurred(message);
        }
    );
    loadLibrary();
}

QVariantList EpisodeFolder::library() const
{
    QVariantList result;
    result.reserve(m_entries.size());
    for (int index = 0; index < m_entries.size(); ++index)
    {
        result.emplaceBack(entryMap(m_entries.at(index), index));
    }
    return result;
}

QVariantList EpisodeFolder::episodes() const
{
    QVariantList result;
    if (m_currentIndex < 0 || m_currentIndex >= m_entries.size())
    {
        return result;
    }

    const Entry &entry = m_entries.at(m_currentIndex);
    result.reserve(entry.relativePaths.size());
    for (int index = 0; index < entry.relativePaths.size(); ++index)
    {
        const QString relativePath = entry.relativePaths.at(index);
        const bool torrent = entry.type == "torrent";
        const int torrentFileIndex = index < entry.torrentFileIndices.size() ?
            entry.torrentFileIndices.at(index) : -1;
        const QString path = torrent ?
            m_torrentEngine->urlFor(
                entry.id, torrentFileIndex, displayName(relativePath)
            ) :
            (index < entry.resolvedPaths.size() ?
                entry.resolvedPaths.at(index) : QString());
        QVariantMap episode{
            {"index", index},
            {"number", index + 1},
            {"title", displayName(relativePath)},
            {"relativePath", relativePath},
            {"path", path},
            {"available", !path.isEmpty()},
            {"size", index < entry.episodeSizes.size() ?
                entry.episodeSizes.at(index) : 0},
            {"watched", entry.watched.contains(relativePath)},
            {"lastPlayed", entry.lastPlayed == relativePath},
        };
        result.emplaceBack(episode);
    }
    return result;
}

QVariantMap EpisodeFolder::currentEntry() const
{
    return m_currentIndex >= 0 && m_currentIndex < m_entries.size() ?
        entryMap(m_entries.at(m_currentIndex), m_currentIndex) : QVariantMap();
}

int EpisodeFolder::currentIndex() const noexcept
{
    return m_currentIndex;
}

void EpisodeFolder::setCurrentIndex(int index)
{
    static_cast<void>(selectCurrentIndex(index));
}

bool EpisodeFolder::selectCurrentIndex(int index)
{
    if (index < -1 || index >= m_entries.size())
    {
        return false;
    }
    if (m_currentIndex == index)
    {
        return true;
    }

    const int previousIndex = m_currentIndex;
    m_currentIndex = index;
    if (!writeLibrary())
    {
        m_currentIndex = previousIndex;
        reportPersistenceError();
        emit selectionChanged();
        emit episodesChanged();
        emit watchedChanged();
        return false;
    }
    setLastError({});
    emit selectionChanged();
    emit episodesChanged();
    emit watchedChanged();
    return true;
}

const QString &EpisodeFolder::lastError() const noexcept
{
    return m_lastError;
}

const QString &EpisodeFolder::folder() const noexcept
{
    return m_currentIndex >= 0 && m_currentIndex < m_entries.size() ?
        m_entries.at(m_currentIndex).folder : m_emptyString;
}

QString EpisodeFolder::folderName() const
{
    return QFileInfo(folder()).fileName();
}

QStringList EpisodeFolder::files() const
{
    return m_currentIndex >= 0 && m_currentIndex < m_entries.size() ?
        playableFiles(m_entries.at(m_currentIndex)) : QStringList();
}

int EpisodeFolder::totalCount() const noexcept
{
    return m_currentIndex >= 0 && m_currentIndex < m_entries.size() ?
        m_entries.at(m_currentIndex).relativePaths.size() : 0;
}

int EpisodeFolder::watchedCount() const noexcept
{
    return m_currentIndex >= 0 && m_currentIndex < m_entries.size() ?
        watchedCount(m_entries.at(m_currentIndex)) : 0;
}

int EpisodeFolder::addFolder(const QUrl &folderUrl)
{
    const QString path = normalizedPath(
        folderUrl.isLocalFile() ? folderUrl.toLocalFile() : folderUrl.toString()
    );
    if (!QFileInfo(path).isDir())
    {
        setLastError(tr("The selected episode folder is not accessible."));
        return -1;
    }

    const QString id = makeFolderId(path);
    const int existingIndex = findEntryById(id);
    if (existingIndex >= 0)
    {
        if (!selectCurrentIndex(existingIndex))
        {
            return -1;
        }
        rescan(existingIndex);
        if (!m_lastError.isEmpty())
        {
            return -1;
        }
        return existingIndex;
    }
    if (m_entries.size() >= MAX_LIBRARY_ENTRIES)
    {
        setLastError(tr("The media library has reached its item limit."));
        return -1;
    }

    Entry entry;
    entry.id = id;
    entry.title = QFileInfo(path).fileName();
    entry.type = "folder";
    entry.source = path;
    entry.folder = path;
    if (!scanFolder(entry) || entry.relativePaths.isEmpty())
    {
        setLastError(tr("The selected folder does not contain supported video files."));
        return -1;
    }
    loadLegacyWatched(entry);

    const int previousIndex = m_currentIndex;
    m_entries.emplaceBack(std::move(entry));
    m_currentIndex = m_entries.size() - 1;
    if (!writeLibrary())
    {
        m_entries.removeLast();
        m_currentIndex = previousIndex;
        reportPersistenceError();
        return -1;
    }
    setLastError({});
    notifyEntryChanged(true);
    return m_currentIndex;
}

int EpisodeFolder::addTorrent(const QUrl &torrentUrl)
{
    const QString path = normalizedPath(
        torrentUrl.isLocalFile() ? torrentUrl.toLocalFile() : torrentUrl.toString()
    );
    const TorrentEngine::AddResult result = m_torrentEngine->addTorrentFile(path);
    if (!result.valid())
    {
        setLastError(result.error);
        return -1;
    }

    const int existingIndex = findEntryById(result.id);
    if (existingIndex >= 0)
    {
        if (!updateTorrentMetadata(
            result.id, result.title, result.paths, result.sizes,
            result.fileIndices, result.source, true
        ))
        {
            return -1;
        }
        const int updatedIndex = findEntryById(result.id);
        if (updatedIndex < 0)
        {
            return -1;
        }
        setLastError({});
        return updatedIndex;
    }
    if (m_entries.size() >= MAX_LIBRARY_ENTRIES)
    {
        m_torrentEngine->removeTorrent(result.id);
        setLastError(tr("The media library has reached its item limit."));
        return -1;
    }

    Entry entry;
    entry.id = result.id;
    entry.title = result.title;
    entry.type = "torrent";
    entry.source = result.source;
    const QVector<Entry> previousEntries = m_entries;
    const int previousIndex = m_currentIndex;
    m_entries.emplaceBack(std::move(entry));
    if (!updateTorrentMetadata(
        result.id, result.title, result.paths, result.sizes,
        result.fileIndices, result.source, true
    ))
    {
        m_entries = previousEntries;
        m_currentIndex = previousIndex;
        m_torrentEngine->removeTorrent(result.id);
        return -1;
    }
    const int addedIndex = findEntryById(result.id);
    if (addedIndex < 0)
    {
        return -1;
    }
    setLastError({});
    return addedIndex;
}

int EpisodeFolder::addMagnet(const QString &magnet)
{
    const TorrentEngine::AddResult result = m_torrentEngine->addMagnet(
        magnet.trimmed()
    );
    if (!result.valid())
    {
        setLastError(result.error);
        return -1;
    }
    const int existingIndex = findEntryById(result.id);
    if (existingIndex >= 0)
    {
        return selectCurrentIndex(existingIndex) ? existingIndex : -1;
    }
    if (m_entries.size() >= MAX_LIBRARY_ENTRIES)
    {
        m_torrentEngine->removeTorrent(result.id);
        setLastError(tr("The media library has reached its item limit."));
        return -1;
    }

    Entry entry;
    entry.id = result.id;
    entry.title = result.title;
    entry.type = "torrent";
    entry.source = result.source;
    const int previousIndex = m_currentIndex;
    m_entries.emplaceBack(std::move(entry));
    m_currentIndex = m_entries.size() - 1;
    if (!writeLibrary())
    {
        m_entries.removeLast();
        m_currentIndex = previousIndex;
        m_torrentEngine->removeTorrent(result.id);
        reportPersistenceError();
        return -1;
    }
    setLastError({});
    notifyEntryChanged(true);
    return m_currentIndex;
}

bool EpisodeFolder::setContentFolder(int entryIndex, const QUrl &folderUrl)
{
    if (entryIndex < 0 || entryIndex >= m_entries.size())
    {
        setLastError(tr("No library item is selected."));
        return false;
    }

    const QString path = normalizedPath(
        folderUrl.isLocalFile() ? folderUrl.toLocalFile() : folderUrl.toString()
    );
    if (!QFileInfo(path).isDir())
    {
        setLastError(tr("The selected episode folder is not accessible."));
        return false;
    }

    Entry &entry = m_entries[entryIndex];
    if (entry.type == "torrent")
    {
        setLastError(tr(
            "Torrent episodes are streamed and cached automatically. "
            "Add an Episode Folder item for local files."
        ));
        return false;
    }
    Entry updated = entry;
    updated.source = path;
    updated.folder = path;
    updated.id = makeFolderId(path);
    updated.title = QFileInfo(path).fileName();
    if (!scanFolder(updated) || updated.relativePaths.isEmpty())
    {
        setLastError(tr(
            "The selected folder does not contain supported video files."
        ));
        return false;
    }
    const int duplicateIndex = findEntryById(updated.id);
    if (duplicateIndex >= 0 && duplicateIndex != entryIndex)
    {
        if (selectCurrentIndex(duplicateIndex))
        {
            setLastError(tr(
                "That episode folder is already in the media library."
            ));
        }
        return false;
    }
    const Entry previousEntry = entry;
    const int previousIndex = m_currentIndex;
    entry = std::move(updated);

    m_currentIndex = entryIndex;
    if (!writeLibrary())
    {
        entry = previousEntry;
        m_currentIndex = previousIndex;
        reportPersistenceError();
        return false;
    }
    setLastError({});
    notifyEntryChanged(true);
    return true;
}

void EpisodeFolder::removeEntry(int entryIndex, bool deleteCache)
{
    if (entryIndex < 0 || entryIndex >= m_entries.size())
    {
        return;
    }

    const QVector<Entry> previousEntries = m_entries;
    const int previousIndex = m_currentIndex;
    const Entry removed = m_entries.at(entryIndex);
    m_entries.removeAt(entryIndex);
    if (m_entries.isEmpty())
    {
        m_currentIndex = -1;
    }
    else if (m_currentIndex > entryIndex)
    {
        --m_currentIndex;
    }
    else if (m_currentIndex == entryIndex)
    {
        m_currentIndex = std::min(
            entryIndex, static_cast<int>(m_entries.size()) - 1
        );
    }

    if (!writeLibrary())
    {
        m_entries = previousEntries;
        m_currentIndex = previousIndex;
        reportPersistenceError();
        return;
    }
    if (removed.type == "torrent")
    {
        m_torrentEngine->removeTorrent(removed.id, deleteCache);
    }
    setLastError({});
    notifyEntryChanged(true);
}

QStringList EpisodeFolder::rescan(int entryIndex)
{
    int index = entryIndex < 0 ? m_currentIndex : entryIndex;
    if (index < 0 || index >= m_entries.size())
    {
        return {};
    }

    const int originalIndex = index;
    bool retainOperationError = false;
    const QVector<Entry> previousEntries = m_entries;
    const int previousIndex = m_currentIndex;
    Entry &entry = m_entries[index];
    if (entry.type == "folder")
    {
        static_cast<void>(scanFolder(entry));
    }
    else
    {
        const TorrentEngine::AddResult result = m_torrentEngine->restore(
            entry.id, entry.source
        );
        if (!result.valid())
        {
            setLastError(result.error);
            retainOperationError = true;
        }
        else
        {
            const int duplicateIndex = findEntryById(result.id);
            if (duplicateIndex >= 0 && duplicateIndex != index)
            {
                Entry &duplicate = m_entries[duplicateIndex];
                duplicate.watched.unite(entry.watched);
                if (duplicate.lastPlayed.isEmpty())
                {
                    duplicate.lastPlayed = entry.lastPlayed;
                }
                if (duplicate.audioTrack < 0) duplicate.audioTrack = entry.audioTrack;
                if (duplicate.subtitleLink.isEmpty()) duplicate.subtitleLink = entry.subtitleLink;
                duplicate.source = result.source;

                const bool removedCurrent = m_currentIndex == index;
                m_entries.removeAt(index);
                int adjustedDuplicate = duplicateIndex;
                if (adjustedDuplicate > index)
                {
                    --adjustedDuplicate;
                }
                if (m_currentIndex > index)
                {
                    --m_currentIndex;
                }
                if (removedCurrent)
                {
                    m_currentIndex = adjustedDuplicate;
                }
                index = adjustedDuplicate;
            }
            else
            {
                entry.id = result.id;
                entry.source = result.source;
            }
            setLastError({});
        }
    }

    if (!writeLibrary())
    {
        m_entries = previousEntries;
        m_currentIndex = previousIndex;
        const QStringList previousFiles =
            originalIndex >= 0 && originalIndex < m_entries.size() ?
                playableFiles(m_entries.at(originalIndex)) : QStringList();
        reportPersistenceError();
        return previousFiles;
    }
    if (!retainOperationError)
    {
        setLastError({});
    }
    notifyEntryChanged(index == m_currentIndex);
    return playableFiles(m_entries.at(index));
}

QStringList EpisodeFolder::open(const QUrl &folderUrl)
{
    const int index = addFolder(folderUrl);
    return index >= 0 ? playableFiles(m_entries.at(index)) : QStringList();
}

bool EpisodeFolder::containsFile(const QString &file) const
{
    return findEntryForFile(file, nullptr) >= 0;
}

bool EpisodeFolder::isTransientStream(const QString &file) const
{
    QString torrentId;
    int torrentFileIndex = -1;
    return m_torrentEngine->identifyStreamUrl(
        file, &torrentId, &torrentFileIndex
    );
}

QString EpisodeFolder::fileName(const QString &file) const
{
    return QFileInfo(normalizedPath(file)).fileName();
}

bool EpisodeFolder::isWatched(const QString &file) const
{
    int episodeIndex = -1;
    const int entryIndex = findEntryForFile(file, &episodeIndex);
    return entryIndex >= 0 && episodeIndex >= 0 &&
        m_entries.at(entryIndex).watched.contains(
            m_entries.at(entryIndex).relativePaths.at(episodeIndex)
        );
}

void EpisodeFolder::setWatched(const QString &file, bool watched)
{
    int episodeIndex = -1;
    const int entryIndex = findEntryForFile(file, &episodeIndex);
    if (entryIndex < 0 || episodeIndex < 0)
    {
        return;
    }

    Entry &entry = m_entries[entryIndex];
    const QString relativePath = entry.relativePaths.at(episodeIndex);
    if (entry.watched.contains(relativePath) == watched)
    {
        return;
    }
    if (watched)
    {
        entry.watched.insert(relativePath);
    }
    else
    {
        entry.watched.remove(relativePath);
    }
    if (!writeLibrary())
    {
        if (watched)
        {
            entry.watched.remove(relativePath);
        }
        else
        {
            entry.watched.insert(relativePath);
        }
        reportPersistenceError();
        emit libraryChanged();
        if (entryIndex == m_currentIndex)
        {
            emit episodesChanged();
            emit watchedChanged();
        }
        return;
    }
    setLastError({});
    emit libraryChanged();
    if (entryIndex == m_currentIndex)
    {
        emit episodesChanged();
        emit watchedChanged();
    }
}

void EpisodeFolder::setEpisodeWatched(int episodeIndex, bool watched)
{
    if (m_currentIndex < 0 || m_currentIndex >= m_entries.size())
    {
        return;
    }
    const Entry &entry = m_entries.at(m_currentIndex);
    if (episodeIndex < 0 || episodeIndex >= entry.relativePaths.size())
    {
        return;
    }

    const QString path = episodeIndex < entry.resolvedPaths.size() ?
        entry.resolvedPaths.at(episodeIndex) : QString();
    if (!path.isEmpty())
    {
        setWatched(path, watched);
        return;
    }

    Entry &mutableEntry = m_entries[m_currentIndex];
    const QString relativePath = mutableEntry.relativePaths.at(episodeIndex);
    if (mutableEntry.watched.contains(relativePath) == watched)
    {
        return;
    }
    if (watched)
    {
        mutableEntry.watched.insert(relativePath);
    }
    else
    {
        mutableEntry.watched.remove(relativePath);
    }
    if (!writeLibrary())
    {
        if (watched)
        {
            mutableEntry.watched.remove(relativePath);
        }
        else
        {
            mutableEntry.watched.insert(relativePath);
        }
        reportPersistenceError();
        notifyEntryChanged();
        return;
    }
    setLastError({});
    notifyEntryChanged();
}

void EpisodeFolder::recordPlayed(const QString &file)
{
    int episodeIndex = -1;
    const int entryIndex = findEntryForFile(file, &episodeIndex);
    if (entryIndex < 0 || episodeIndex < 0)
    {
        return;
    }

    Entry &entry = m_entries[entryIndex];
    const QString relativePath = entry.relativePaths.at(episodeIndex);
    const QString previousLastPlayed = entry.lastPlayed;
    const int previousIndex = m_currentIndex;
    const bool changed = entry.lastPlayed != relativePath ||
        m_currentIndex != entryIndex;
    entry.lastPlayed = relativePath;
    m_currentIndex = entryIndex;
    if (changed)
    {
        if (!writeLibrary())
        {
            entry.lastPlayed = previousLastPlayed;
            m_currentIndex = previousIndex;
            reportPersistenceError();
            return;
        }
        setLastError({});
        notifyEntryChanged(true);
    }
}

QString EpisodeFolder::episodePath(int episodeIndex)
{
    if (m_currentIndex < 0 || m_currentIndex >= m_entries.size())
    {
        return {};
    }
    const Entry &entry = m_entries.at(m_currentIndex);
    if (episodeIndex < 0 || episodeIndex >= entry.relativePaths.size())
    {
        return {};
    }
    if (entry.type == "torrent")
    {
        if (episodeIndex >= entry.torrentFileIndices.size() ||
            episodeIndex >= entry.episodeSizes.size() ||
            entry.torrentFileIndices.at(episodeIndex) < 0 ||
            entry.episodeSizes.at(episodeIndex) <= 0)
        {
            setLastError(tr("Torrent metadata is not ready yet."));
            return {};
        }
        const QString url = m_torrentEngine->streamUrl(
            entry.id,
            entry.torrentFileIndices.at(episodeIndex),
            displayName(entry.relativePaths.at(episodeIndex))
        );
        if (url.isEmpty())
        {
            setLastError(tr("This episode is not ready to stream yet."));
        }
        else
        {
            setLastError({});
        }
        return url;
    }
    return episodeIndex < entry.resolvedPaths.size() ?
        entry.resolvedPaths.at(episodeIndex) : QString();
}

QString EpisodeFolder::normalizedPath(const QString &value)
{
    const QUrl url(value);
    const QString localPath = url.isLocalFile() ? url.toLocalFile() : value;
    const QFileInfo info(localPath);
    const QString canonical = info.canonicalFilePath();
    return canonical.isEmpty() ? info.absoluteFilePath() : canonical;
}

QString EpisodeFolder::stableFolderIdentity(const QString &folder)
{
    static const QRegularExpression portalPath(
        "^/run/(?:flatpak|user/\\d+)/doc/[^/]+/(.+)$"
    );
    const QRegularExpressionMatch match = portalPath.match(folder);
    return match.hasMatch() ? "flatpak-document/" + match.captured(1) : folder;
}

bool EpisodeFolder::isVideoPath(const QString &path)
{
    return VIDEO_EXTENSIONS.contains(QFileInfo(path).suffix().toLower());
}

QString EpisodeFolder::displayName(const QString &relativePath)
{
    return QFileInfo(relativePath).fileName();
}

QString EpisodeFolder::makeFolderId(const QString &folder)
{
    return "folder-" + QString::fromLatin1(
        QCryptographicHash::hash(
            stableFolderIdentity(folder).toUtf8(), QCryptographicHash::Sha256
        ).toHex()
    );
}

int EpisodeFolder::findEntryForFile(
    const QString &file,
    int *episodeIndex) const
{
    QString torrentId;
    int torrentFileIndex = -1;
    if (m_torrentEngine->identifyStreamUrl(
            file, &torrentId, &torrentFileIndex))
    {
        const int entryIndex = findEntryById(torrentId);
        if (entryIndex >= 0)
        {
            const int found = m_entries.at(entryIndex)
                .torrentFileIndices.indexOf(torrentFileIndex);
            if (found >= 0)
            {
                if (episodeIndex != nullptr)
                {
                    *episodeIndex = found;
                }
                return entryIndex;
            }
        }
    }

    const QString path = normalizedPath(file);
    for (int entryIndex = 0; entryIndex < m_entries.size(); ++entryIndex)
    {
        const int found = m_entries.at(entryIndex).resolvedPaths.indexOf(path);
        if (found >= 0)
        {
            if (episodeIndex != nullptr)
            {
                *episodeIndex = found;
            }
            return entryIndex;
        }
    }
    return -1;
}

int EpisodeFolder::findEntryById(const QString &id) const
{
    for (int index = 0; index < m_entries.size(); ++index)
    {
        if (m_entries.at(index).id == id)
        {
            return index;
        }
    }
    return -1;
}

int EpisodeFolder::watchedCount(const Entry &entry) const noexcept
{
    int result = 0;
    for (const QString &path : entry.relativePaths)
    {
        result += entry.watched.contains(path) ? 1 : 0;
    }
    return result;
}

int EpisodeFolder::availableCount(const Entry &entry) const noexcept
{
    if (entry.type == "torrent")
    {
        if (!m_torrentEngine->status(entry.id).value("ready").toBool())
        {
            return 0;
        }
        int result = 0;
        for (int index = 0; index < entry.relativePaths.size(); ++index)
        {
            result += index < entry.episodeSizes.size() &&
                    entry.episodeSizes.at(index) > 0 &&
                    index < entry.torrentFileIndices.size() &&
                    entry.torrentFileIndices.at(index) >= 0 ? 1 : 0;
        }
        return result;
    }
    return std::count_if(
        entry.resolvedPaths.cbegin(),
        entry.resolvedPaths.cend(),
        [](const QString &path) { return !path.isEmpty(); }
    );
}

QVariantMap EpisodeFolder::entryMap(const Entry &entry, int index) const
{
    const QVariantMap torrentStatus = entry.type == "torrent" ?
        m_torrentEngine->status(entry.id) : QVariantMap();
    const bool torrentReady = torrentStatus.value("ready").toBool();
    int nextEpisode = -1;
    int lastPlayedIndex = -1;
    for (int episodeIndex = 0;
         episodeIndex < entry.relativePaths.size(); ++episodeIndex)
    {
        const QString &relativePath = entry.relativePaths.at(episodeIndex);
        if (relativePath == entry.lastPlayed)
        {
            lastPlayedIndex = episodeIndex;
        }
        const bool available = entry.type == "torrent" ?
            (torrentReady &&
             episodeIndex < entry.episodeSizes.size() &&
             entry.episodeSizes.at(episodeIndex) > 0 &&
             episodeIndex < entry.torrentFileIndices.size() &&
             entry.torrentFileIndices.at(episodeIndex) >= 0) :
            (episodeIndex < entry.resolvedPaths.size() &&
             !entry.resolvedPaths.at(episodeIndex).isEmpty());
        if (nextEpisode < 0 && !entry.watched.contains(relativePath) && available)
        {
            nextEpisode = episodeIndex;
        }
    }

    QVariantMap result{
        {"index", index},
        {"id", entry.id},
        {"title", entry.title},
        {"type", entry.type},
        {"source", entry.source},
        {"folder", entry.folder},
        {"totalCount", entry.relativePaths.size()},
        {"watchedCount", watchedCount(entry)},
        {"availableCount", availableCount(entry)},
        {"nextEpisodeIndex", nextEpisode},
        {"lastPlayedIndex", lastPlayedIndex},
        {"subtitleLink", entry.subtitleLink},
        {"audioTrack", entry.audioTrack},
    };
    if (entry.type == "torrent")
    {
        result.insert("ready", torrentStatus.value("ready"));
        result.insert("loadingMetadata", torrentStatus.value("loadingMetadata"));
        result.insert("torrentState", torrentStatus.value("state"));
        result.insert("downloadRate", torrentStatus.value("downloadRate"));
        result.insert("peers", torrentStatus.value("peers"));
        result.insert("downloadProgress", torrentStatus.value("progress"));
    }
    return result;
}

QStringList EpisodeFolder::playableFiles(const Entry &entry) const
{
    QStringList result;
    if (entry.type == "torrent")
    {
        for (int index = 0; index < entry.relativePaths.size() &&
             index < entry.torrentFileIndices.size(); ++index)
        {
            result.emplaceBack(m_torrentEngine->urlFor(
                entry.id,
                entry.torrentFileIndices.at(index),
                displayName(entry.relativePaths.at(index))
            ));
        }
        return result;
    }
    for (const QString &path : entry.resolvedPaths)
    {
        if (!path.isEmpty())
        {
            result.emplaceBack(path);
        }
    }
    return result;
}

bool EpisodeFolder::scanFolder(Entry &entry)
{
    if (!QFileInfo(entry.folder).isDir())
    {
        entry.resolvedPaths.fill({}, entry.relativePaths.size());
        return false;
    }

    struct EpisodePath
    {
        QString relative;
        QString resolved;
    };
    QVector<EpisodePath> episodes;
    qint64 pathBytes = 0;
    QDir root(entry.folder);
    QDirIterator iterator(
        entry.folder,
        QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories
    );
    while (iterator.hasNext())
    {
        iterator.next();
        const QFileInfo info = iterator.fileInfo();
        if (!isVideoPath(info.fileName()))
        {
            continue;
        }
        const QString resolved = info.canonicalFilePath();
        if (!resolved.isEmpty())
        {
            const QString relative = QDir::fromNativeSeparators(
                root.relativeFilePath(resolved)
            );
            const qint64 addedBytes = relative.toUtf8().size() +
                resolved.toUtf8().size();
            if (episodes.size() >= MAX_EPISODES_PER_ENTRY ||
                addedBytes > MAX_PATH_BYTES_PER_ENTRY - pathBytes)
            {
                return false;
            }
            pathBytes += addedBytes;
            episodes.push_back({
                relative,
                resolved,
            });
        }
    }

    QCollator collator{QLocale{QLocale::English}};
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(
        episodes.begin(),
        episodes.end(),
        [&collator](const EpisodePath &left, const EpisodePath &right) {
            return collator.compare(left.relative, right.relative) < 0;
        }
    );

    entry.relativePaths.clear();
    entry.resolvedPaths.clear();
    entry.relativePaths.reserve(episodes.size());
    entry.resolvedPaths.reserve(episodes.size());
    for (const EpisodePath &episode : episodes)
    {
        entry.relativePaths.emplaceBack(episode.relative);
        entry.resolvedPaths.emplaceBack(episode.resolved);
    }
    return true;
}

void EpisodeFolder::resolveTorrentFiles(Entry &entry)
{
    entry.resolvedPaths.fill({}, entry.relativePaths.size());
    if (!QFileInfo(entry.folder).isDir())
    {
        return;
    }

    QHash<QString, QStringList> byFileName;
    QHash<QString, QString> byRelativePath;
    int discoveredFiles = 0;
    qint64 pathBytes = 0;
    QDir root(entry.folder);
    QDirIterator iterator(
        entry.folder,
        QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
        QDirIterator::Subdirectories
    );
    while (iterator.hasNext())
    {
        iterator.next();
        const QFileInfo info = iterator.fileInfo();
        if (!isVideoPath(info.fileName()))
        {
            continue;
        }
        const QString canonical = info.canonicalFilePath();
        if (canonical.isEmpty())
        {
            continue;
        }
        const qint64 addedBytes = canonical.toUtf8().size();
        if (++discoveredFiles > MAX_EPISODES_PER_ENTRY ||
            addedBytes > MAX_PATH_BYTES_PER_ENTRY - pathBytes)
        {
            return;
        }
        pathBytes += addedBytes;
        const QString relative = QDir::fromNativeSeparators(
            root.relativeFilePath(canonical)
        );
        byRelativePath.insert(relative.toCaseFolded(), canonical);
        byFileName[info.fileName().toCaseFolded()].emplaceBack(canonical);
    }

    const QString titlePrefix = QDir::fromNativeSeparators(entry.title) + '/';
    for (int index = 0; index < entry.relativePaths.size(); ++index)
    {
        const QString relative = QDir::fromNativeSeparators(
            entry.relativePaths.at(index)
        );
        QString resolved = byRelativePath.value(relative.toCaseFolded());
        if (resolved.isEmpty())
        {
            resolved = byRelativePath.value(
                (titlePrefix + relative).toCaseFolded()
            );
        }
        if (resolved.isEmpty())
        {
            const QStringList matches = byFileName.value(
                QFileInfo(relative).fileName().toCaseFolded()
            );
            if (matches.size() == 1)
            {
                resolved = matches.constFirst();
            }
        }
        entry.resolvedPaths[index] = resolved;
    }
}

void EpisodeFolder::applyTorrentMetadata(
    const QString &id,
    const QString &title,
    const QStringList &paths,
    const QList<qint64> &sizes,
    const QList<int> &fileIndices,
    const QString &storedSource)
{
    static_cast<void>(updateTorrentMetadata(
        id, title, paths, sizes, fileIndices, storedSource, false
    ));
}

bool EpisodeFolder::updateTorrentMetadata(
    const QString &id,
    const QString &title,
    const QStringList &paths,
    const QList<qint64> &sizes,
    const QList<int> &fileIndices,
    const QString &storedSource,
    bool selectEntry)
{
    const int entryIndex = findEntryById(id);
    if (entryIndex < 0)
    {
        return false;
    }

    struct TorrentEpisode
    {
        QString path;
        qint64 size{0};
        int fileIndex{-1};
    };
    QVector<TorrentEpisode> episodes;
    QSet<int> seenFileIndices;
    for (int index = 0; index < paths.size(); ++index)
    {
        if (episodes.size() >= MAX_EPISODES_PER_ENTRY)
        {
            break;
        }
        if (!isVideoPath(paths.at(index)) || index >= sizes.size() ||
            index >= fileIndices.size() || sizes.at(index) <= 0 ||
            fileIndices.at(index) < 0 ||
            seenFileIndices.contains(fileIndices.at(index)))
        {
            continue;
        }
        seenFileIndices.insert(fileIndices.at(index));
        episodes.emplaceBack(TorrentEpisode{
            QDir::fromNativeSeparators(paths.at(index)),
            sizes.at(index),
            fileIndices.at(index),
        });
    }
    QCollator collator{QLocale{QLocale::English}};
    collator.setNumericMode(true);
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(
        episodes.begin(), episodes.end(),
        [&collator](const TorrentEpisode &left, const TorrentEpisode &right) {
            return collator.compare(left.path, right.path) < 0;
        }
    );

    const QVector<Entry> previousEntries = m_entries;
    const int previousIndex = m_currentIndex;
    Entry &entry = m_entries[entryIndex];
    if (episodes.isEmpty())
    {
        const bool reportAsynchronousFailure = entry.source.startsWith(
            QStringLiteral("magnet:"), Qt::CaseInsensitive
        );
        m_entries.removeAt(entryIndex);
        if (m_entries.isEmpty())
        {
            m_currentIndex = -1;
        }
        else if (m_currentIndex > entryIndex)
        {
            --m_currentIndex;
        }
        else if (m_currentIndex == entryIndex)
        {
            m_currentIndex = std::min(
                entryIndex, static_cast<int>(m_entries.size()) - 1
            );
        }
        const QString message = tr(
            "This torrent does not contain supported video episodes."
        );
        if (!writeLibrary())
        {
            m_entries = previousEntries;
            m_currentIndex = previousIndex;
            reportPersistenceError();
            return false;
        }
        // Destructive cache/session cleanup is intentionally delayed until the
        // record removal has been committed successfully.
        m_torrentEngine->removeTorrent(id);
        setLastError(message);
        notifyEntryChanged(true);
        if (reportAsynchronousFailure)
        {
            emit errorOccurred(message);
        }
        return false;
    }
    entry.title = title.isEmpty() ? entry.title : title;
    entry.source = storedSource.isEmpty() ? entry.source : storedSource;
    entry.relativePaths.clear();
    entry.resolvedPaths.clear();
    entry.episodeSizes.clear();
    entry.torrentFileIndices.clear();
    for (const TorrentEpisode &episode : episodes)
    {
        entry.relativePaths.emplaceBack(episode.path);
        entry.resolvedPaths.emplaceBack(QString());
        entry.episodeSizes.emplaceBack(episode.size);
        entry.torrentFileIndices.emplaceBack(episode.fileIndex);
    }
    if (selectEntry)
    {
        m_currentIndex = entryIndex;
    }
    if (!writeLibrary())
    {
        m_entries = previousEntries;
        m_currentIndex = previousIndex;
        reportPersistenceError();
        return false;
    }
    setLastError({});
    notifyEntryChanged(
        entryIndex == m_currentIndex || previousIndex != m_currentIndex
    );
    return true;
}

void EpisodeFolder::loadLibrary()
{
    const QJsonObject root = readLibraryObject();
    m_lastSubtitleSearch = root.value("lastSubtitleSearch").toObject().toVariantMap();
    QString currentId = root.value("currentId").toString();
    const QJsonArray storedEntries = root.value("entries").toArray();
    for (const QJsonValue &value : storedEntries)
    {
        if (!value.isObject())
        {
            continue;
        }
        const QJsonObject object = value.toObject();
        Entry entry;
        entry.id = jsonString(object, "id");
        entry.title = jsonString(object, "title");
        entry.type = jsonString(object, "type");
        entry.source = jsonString(object, "source");
        entry.folder = jsonString(object, "folder");
        entry.relativePaths = jsonStringList(object.value("episodes"));
        entry.episodeSizes = jsonIntegerList(object.value("episodeSizes"));
        entry.torrentFileIndices = jsonIntList(
            object.value("torrentFileIndices")
        );
        const QStringList watched = jsonStringList(object.value("watched"));
        entry.watched = QSet<QString>(watched.cbegin(), watched.cend());
        entry.lastPlayed = jsonString(object, "lastPlayed");
        entry.subtitleLink = object.value("subtitleLink").toObject().toVariantMap();
        entry.audioTrack = object.value("audioTrack").toInt(-1);

        if (entry.id.isEmpty() || entry.title.isEmpty() ||
            (entry.type != "folder" && entry.type != "torrent"))
        {
            continue;
        }
        if (findEntryById(entry.id) >= 0 ||
            m_entries.size() >= MAX_LIBRARY_ENTRIES)
        {
            continue;
        }
        if (entry.type == "folder")
        {
            if (!scanFolder(entry))
            {
                entry.resolvedPaths.fill({}, entry.relativePaths.size());
            }
        }
        else
        {
            entry.resolvedPaths.fill({}, entry.relativePaths.size());
        }
        m_entries.emplaceBack(std::move(entry));
    }

    if (m_entries.isEmpty())
    {
        migrateLegacyFolder();
        return;
    }

    bool migratedTorrent = false;
    for (int index = 0; index < m_entries.size();)
    {
        if (m_entries.at(index).type != "torrent")
        {
            ++index;
            continue;
        }

        const QString oldId = m_entries.at(index).id;
        const TorrentEngine::AddResult result = m_torrentEngine->restore(
            oldId, m_entries.at(index).source
        );
        if (!result.valid())
        {
            ++index;
            continue;
        }

        if (currentId == oldId)
        {
            currentId = result.id;
        }
        const int existingIndex = findEntryById(result.id);
        if (existingIndex >= 0 && existingIndex != index)
        {
            Entry &existing = m_entries[existingIndex];
            existing.watched.unite(m_entries.at(index).watched);
            if (existing.lastPlayed.isEmpty())
            {
                existing.lastPlayed = m_entries.at(index).lastPlayed;
            }
            if (existing.audioTrack < 0) existing.audioTrack = m_entries.at(index).audioTrack;
            if (existing.subtitleLink.isEmpty()) existing.subtitleLink = m_entries.at(index).subtitleLink;
            existing.source = result.source;
            m_entries.removeAt(index);
            migratedTorrent = true;
            continue;
        }

        Entry &entry = m_entries[index];
        if (entry.id != result.id || entry.source != result.source)
        {
            entry.id = result.id;
            entry.source = result.source;
            migratedTorrent = true;
        }
        ++index;
    }

    m_currentIndex = findEntryById(currentId);
    if (m_currentIndex < 0)
    {
        m_currentIndex = 0;
    }
    if (migratedTorrent)
    {
        if (!writeLibrary())
        {
            reportPersistenceError();
        }
    }
}

void EpisodeFolder::migrateLegacyFolder()
{
    QJsonObject legacy;
    readJsonObject(configFilePath(LEGACY_PROGRESS_FILE), &legacy);
    QString savedFolder = legacy.value(LEGACY_CURRENT_FOLDER).toString();
    if (savedFolder.isEmpty())
    {
        QSettings settings = makeSettings();
        settings.beginGroup(LEGACY_SETTINGS_GROUP);
        savedFolder = settings.value(LEGACY_CURRENT_FOLDER).toString();
        settings.endGroup();
    }
    if (!QFileInfo(savedFolder).isDir())
    {
        return;
    }

    Entry entry;
    entry.id = makeFolderId(savedFolder);
    entry.title = QFileInfo(savedFolder).fileName();
    entry.type = "folder";
    entry.source = savedFolder;
    entry.folder = savedFolder;
    if (!scanFolder(entry))
    {
        return;
    }
    loadLegacyWatched(entry);
    if (!entry.relativePaths.isEmpty())
    {
        m_entries.emplaceBack(std::move(entry));
        m_currentIndex = 0;
        if (!writeLibrary())
        {
            m_entries.clear();
            m_currentIndex = -1;
            reportPersistenceError();
        }
    }
}

void EpisodeFolder::loadLegacyWatched(Entry &entry) const
{
    QJsonObject legacy;
    readJsonObject(configFilePath(LEGACY_PROGRESS_FILE), &legacy);
    const QString legacyKey = entry.id.startsWith("folder-") ?
        entry.id.sliced(7) : entry.id;
    const QStringList watched = jsonStringList(
        legacy.value(LEGACY_WATCHED_BY_FOLDER)
            .toObject()
            .value(legacyKey)
    );
    for (const QString &storedPath : watched)
    {
        const QString storedName = QFileInfo(storedPath).fileName();
        for (const QString &relativePath : entry.relativePaths)
        {
            if (relativePath == storedPath ||
                QFileInfo(relativePath).fileName() == storedName)
            {
                entry.watched.insert(relativePath);
                break;
            }
        }
    }

    if (!entry.watched.isEmpty())
    {
        return;
    }

    QSettings settings = makeSettings();
    settings.beginGroup(LEGACY_SETTINGS_GROUP);
    for (const QString &folderKey : settings.childGroups())
    {
        settings.beginGroup(folderKey);
        const QStringList storedFiles = settings.value(LEGACY_WATCHED_FILES)
            .toStringList();
        settings.endGroup();
        if (storedFiles.isEmpty() ||
            stableFolderIdentity(QFileInfo(storedFiles.constFirst()).absolutePath()) !=
                stableFolderIdentity(entry.folder))
        {
            continue;
        }
        for (const QString &storedFile : storedFiles)
        {
            const QString storedName = QFileInfo(storedFile).fileName();
            for (const QString &relativePath : entry.relativePaths)
            {
                if (QFileInfo(relativePath).fileName() == storedName)
                {
                    entry.watched.insert(relativePath);
                    break;
                }
            }
        }
    }
    settings.endGroup();
}

bool EpisodeFolder::writeLibrary() const
{
    QJsonArray entries;
    for (const Entry &entry : m_entries)
    {
        QStringList watched(entry.watched.cbegin(), entry.watched.cend());
        naturalSort(watched);
        entries.append(QJsonObject{
            {"id", entry.id},
            {"title", entry.title},
            {"type", entry.type},
            {"source", entry.source},
            {"folder", entry.folder},
            {"episodes", QJsonArray::fromStringList(entry.relativePaths)},
            {"episodeSizes", QJsonArray::fromVariantList([&entry]() {
                QVariantList values;
                values.reserve(entry.episodeSizes.size());
                for (qint64 value : entry.episodeSizes)
                {
                    values.emplaceBack(value);
                }
                return values;
            }())},
            {"torrentFileIndices", QJsonArray::fromVariantList([&entry]() {
                QVariantList values;
                values.reserve(entry.torrentFileIndices.size());
                for (int value : entry.torrentFileIndices)
                {
                    values.emplaceBack(value);
                }
                return values;
            }())},
            {"watched", QJsonArray::fromStringList(watched)},
            {"lastPlayed", entry.lastPlayed},
            {"subtitleLink", QJsonObject::fromVariantMap(entry.subtitleLink)},
            {"audioTrack", entry.audioTrack},
        });
    }

    const QJsonObject root{
        {"version", 1},
        {"lastSubtitleSearch", QJsonObject::fromVariantMap(m_lastSubtitleSearch)},
        {"currentId", m_currentIndex >= 0 && m_currentIndex < m_entries.size() ?
            m_entries.at(m_currentIndex).id : QString()},
        {"entries", entries},
    };
    const QByteArray data = QJsonDocument(root).toJson(QJsonDocument::Indented);
    if (data.size() > MAX_LIBRARY_SIZE)
    {
        return false;
    }
    const QString path = configFilePath(LIBRARY_FILE);
    QFile current(path);
    if (current.open(QIODevice::ReadOnly) && current.size() > 0 &&
        current.size() <= MAX_LIBRARY_SIZE)
    {
        const qint64 previousSize = current.size();
        const QByteArray previous = current.read(MAX_LIBRARY_SIZE + 1);
        // QFile's Windows sharing mode does not allow replacing an open file.
        // Release the read handle before QSaveFile atomically renames over it.
        current.close();
        QJsonParseError error;
        const QJsonDocument document = QJsonDocument::fromJson(previous, &error);
        if (previous.size() == previousSize &&
            error.error == QJsonParseError::NoError && document.isObject())
        {
            if (!writeFileAtomically(
                    configFilePath(LIBRARY_BACKUP_FILE), previous))
            {
                return false;
            }
        }
    }
    current.close();
    return writeFileAtomically(path, data);
}

void EpisodeFolder::reportPersistenceError()
{
    const QString message = tr(
        "Memento could not save the media library. Your last change was not applied."
    );
    setLastError(message);
    emit errorOccurred(message);
}

void EpisodeFolder::setLastError(const QString &error)
{
    if (m_lastError == error)
    {
        return;
    }
    m_lastError = error;
    emit lastErrorChanged();
}

void EpisodeFolder::notifyEntryChanged(bool selectionMayHaveChanged)
{
    emit libraryChanged();
    emit episodesChanged();
    emit watchedChanged();
    if (selectionMayHaveChanged)
    {
        emit selectionChanged();
    }
}


QVariantMap EpisodeFolder::playbackInfo(const QString &file) const
{
    int episode = -1;
    const int entry = findEntryForFile(file, &episode);
    if (entry < 0 || episode < 0) return {};
    const Entry &item = m_entries.at(entry);
    return {{"key", item.id}, {"title", item.title},
        {"filename", item.relativePaths.at(episode)}};
}

QVariantMap EpisodeFolder::subtitleLinkForFile(const QString &file) const
{
    const int index = findEntryForFile(file, nullptr);
    return index < 0 ? QVariantMap{} : m_entries.at(index).subtitleLink;
}

bool EpisodeFolder::setSubtitleLinkForFile(const QString &file, const QVariantMap &link)
{
    const int index = findEntryForFile(file, nullptr);
    if (index < 0) return false;
    Entry &entry = m_entries[index];
    const QVariantMap previous = entry.subtitleLink;
    if (previous == link) return true;
    entry.subtitleLink = link;
    if (!writeLibrary())
    {
        entry.subtitleLink = previous;
        reportPersistenceError();
        return false;
    }
    notifyEntryChanged(true);
    return true;
}

bool EpisodeFolder::clearSubtitleLinkForFile(const QString &file)
{
    return setSubtitleLinkForFile(file, {});
}

bool EpisodeFolder::rememberSubtitleSearch(const QVariantMap &link, int episode)
{
    const auto previous = m_lastSubtitleSearch;
    m_lastSubtitleSearch = link;
    m_lastSubtitleSearch.insert("episode", episode);
    if (!writeLibrary()) { m_lastSubtitleSearch = previous; reportPersistenceError(); return false; }
    emit libraryChanged();
    return true;
}

int EpisodeFolder::defaultAudioTrackForFile(const QString &file) const
{
    const int index = findEntryForFile(file, nullptr);
    return index < 0 ? -1 : m_entries.at(index).audioTrack;
}

bool EpisodeFolder::setDefaultAudioTrackForFile(const QString &file, int track)
{
    const int index = findEntryForFile(file, nullptr);
    if (index < 0 || track < -1) return false;
    auto &entry = m_entries[index];
    const int previous = entry.audioTrack;
    entry.audioTrack = track;
    if (!writeLibrary()) { entry.audioTrack = previous; reportPersistenceError(); return false; }
    notifyEntryChanged(true);
    return true;
}
