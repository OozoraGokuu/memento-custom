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

#include <QString>

/**
 * @brief Utilities for getting important program directories and files.
 */
namespace DirectoryUtils
{

/**
 * @brief Get the directory the executable is in.
 *
 * @return Path to the program directory.
 */
[[nodiscard]]
QString getProgramDirectory();

/**
 * @brief Get the config directory.
 *
 * @return Path to the config directory.
 */
[[nodiscard]]
QString getConfigDir();

/**
 * @brief The top level directory for application data.
 *
 * @return Path to the data directory.
 */
[[nodiscard]]
QString getDataDir();

/**
 * @brief Get the application cache directory.
 *
 * @return Path to the application cache directory.
 */
[[nodiscard]]
QString getCacheDir();

/**
 * @brief Gets the dictionary resource directory path.
 *
 * @return The dictionary resource directory path.
 */
[[nodiscard]]
QString getDictionaryResourceDir();

/**
 * @brief Get the MeCab dictionary directory if it exists.
 *
 * @return Path to the MeCab dictionary directory if it exists, empty string
 * otherwise.
 */
[[nodiscard]]
QString getMecabDictionary();

/**
 * @brief Get the dictionary database.
 *
 * @return Path to the dictionary directory.
 */
[[nodiscard]]
QString getDictionaryDb();

/**
 * @brief Get the path to the Anki config file.
 *
 * @return Path to the Anki config file.
 */
[[nodiscard]]
QString getAnkiConfig();

/**
 * @brief Get the path to the mpv input.conf.
 *
 * @return Path to mpv's input.conf.
 */
[[nodiscard]]
QString getMpvInputConfig();

#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
/**
 * @brief Get the configuration file for cached options.
 *
 * @return Path to the config file for cached options.
 */
[[nodiscard]]
QString getCacheConfig();
#endif

};
