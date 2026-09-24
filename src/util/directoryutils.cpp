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

#include "util/directoryutils.h"

#include <QDir>
#include <QStandardPaths>
#include <QCoreApplication>

namespace
{

/**
 * @brief Get a normalized writeable location. This removes the redundant
 * "memento" from the end of paths.
 *
 * @param location The location to get.
 * @return Path to the writable location.
 */
[[nodiscard]]
QString normalizedWritableLocation(QStandardPaths::StandardLocation location)
{
    constexpr size_t PATH_CHOP_LENGTH = sizeof("memento") - 1;

    QString path = QStandardPaths::writableLocation(location);

    if (path.endsWith("memento/memento"))
    {
        path.chop(PATH_CHOP_LENGTH);
    }

    if (path.endsWith("memento"))
    {
        path += '/';
    }
    else if (!path.endsWith("memento/"))
    {
        path += "/memento/";
        path = QDir::cleanPath(path) + '/';
    }
    return path;
}

} // namespace

QString DirectoryUtils::getProgramDirectory()
{
    return QCoreApplication::applicationDirPath() + '/';
}

QString DirectoryUtils::getConfigDir()
{
    return normalizedWritableLocation(QStandardPaths::AppConfigLocation);
}

QString DirectoryUtils::getDataDir()
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    return normalizedWritableLocation(QStandardPaths::AppDataLocation);
#else
    return getConfigDir();
#endif
}

QString DirectoryUtils::getCacheDir()
{
    return normalizedWritableLocation(QStandardPaths::CacheLocation);
}

QString DirectoryUtils::getDictionaryResourceDir()
{
    constexpr const char *RESOURCE_DIR = "res";
    return getDataDir() + RESOURCE_DIR + '/';
}

QString DirectoryUtils::getMecabDictionary()
{
#if defined(Q_OS_WIN)
    return getProgramDirectory() + "dic/";
#elif defined(MEMENTO_BUNDLE)
    return getProgramDirectory() + "../Resources/mecab/dic/";
#else
    return "";
#endif
}

QString DirectoryUtils::getDictionaryDb()
{
    constexpr const char *DICT_DB_FILE = "dictionaries.sqlite";
    return getDataDir() + DICT_DB_FILE;
}

QString DirectoryUtils::getAnkiConfig()
{
    constexpr const char *ANKI_CONFIG_FILE = "anki_connect.json";
    return getConfigDir() + ANKI_CONFIG_FILE;
}

QString DirectoryUtils::getMpvInputConfig()
{
    constexpr const char *MPV_INPUT_CONF_FILE = "input.conf";
    return getConfigDir() + MPV_INPUT_CONF_FILE;
}

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
QString DirectoryUtils::getCacheConfig()
{
    constexpr const char *CACHE_CONFIG_FILE = "memento.conf";
    return getCacheDir() + CACHE_CONFIG_FILE;
}
#endif
