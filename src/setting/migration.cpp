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

#include "setting/migration.h"

#include <QDir>
#include <QFont>
#include <QSettings>

#include "setting/keys.h"

#if defined(Q_OS_WIN)
#include "util/utils.h"
#endif // defined(Q_OS_WIN)

namespace
{

/**
 * @brief Read a font from a QVariant. This handles both the old and new ways
 * of serializing fonts.
 *
 * @param value The variant containing the serialized font.
 * @param defaultFont The default font to use in case of an error.
 * @return The variant as a font, defaultFont on error.
 */
[[nodiscard]]
QFont fontFromSettingsValue(const QVariant &value, const QFont &defaultFont)
{
    if (!value.isValid())
    {
        return defaultFont;
    }

    if (value.metaType().id() == QMetaType::QFont)
    {
        return value.value<QFont>();
    }

    QFont font = defaultFont;
    if (!font.fromString(value.toString()))
    {
        return defaultFont;
    }
    return font;
}

/**
 * @brief Migrate a font setting from the version 4 format to version 5 format.
 *
 * @param settings The settings object the font to access the font.
 * @param key The key of the font setting.
 * @param defaultFont The font to return as the default.
 */
void migrateFontSetting4To5(
    QSettings &settings,
    const QString &key,
    const QFont &defaultFont)
{
    if (!settings.contains(key))
    {
        return;
    }

    const QVariant value = settings.value(key);
    const QFont font = fontFromSettingsValue(value, defaultFont);
    settings.setValue(key, font.toString());
}

/**
 * @brief Migrate settings from version 1 to 2.
 */
void migrate1to2()
{
    /* These paths are hardcoded because DirectoryUtils may change in
     * the future. */
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    QDir configDir(QString(::getenv("HOME")) + "/.config/memento");
    configDir.rename(
        "./dict/dictionaries.sqlite", "./dictionaries.sqlite"
    );

    QDir dictDir(configDir.absolutePath() + "/dict");
    dictDir.removeRecursively();
#elif defined(Q_OS_WIN)
    QString programDirPath = DirectoryUtils::getProgramDirectory();
    if (!QDir(programDirPath + "config").exists())
    {
        return;
    }

    QDir programDir(programDirPath);
    programDir.rename(
        "./config/dict/dictionaries.sqlite",
        "./config/dictionaries.sqlite"
    );

    QDir dictDir(programDirPath + "config/dict");
    dictDir.removeRecursively();

    QString configPath = QStandardPaths::writableLocation(
        QStandardPaths::AppConfigLocation
    );
    configPath.chop(sizeof("memento") - 1);
    QDir configDir(configPath);
    configDir.removeRecursively();

    programDir.rename("./config", configDir.absolutePath());
#endif
}

/**
 * @brief Migrate settings from version 2 to 3.
 */
void migrate2to3()
{
    QSettings settings;
    bool list = settings.value("search/list-result", true).toBool();
    settings.setValue(
        "list-result",
        static_cast<int>(
            list ?
                Setting::GlossaryStyle::GlossaryStyleBullet :
                Setting::GlossaryStyle::GlossaryStyleLineBreak
        )
    );
}

/**
 * @brief Migrate settings from version 3 to 4.
 */
void migrate3to4()
{
    QSettings settings;
    settings.remove("behavior/file-open-custom");
    settings.remove("dictionaries");
    settings.remove("interface");
    settings.remove("search/modifier");
    settings.remove(Keys::Window::GROUP);
}

/**
 * @brief Migrate settings from version 4 to 5.
 */
void migrate4to5()
{
    QSettings settings;
    settings.beginGroup(Keys::Interface::GROUP);
    migrateFontSetting4To5(
        settings,
        Keys::Interface::Subtitle::FONT,
        Keys::Interface::Subtitle::FONT_DEFAULT
    );
    migrateFontSetting4To5(
        settings,
        Keys::Interface::SEARCH_EXPRESSION_FONT,
        Keys::Interface::SEARCH_EXPRESSION_FONT_DEFAULT
    );
    migrateFontSetting4To5(
        settings,
        Keys::Interface::SEARCH_READING_FONT,
        Keys::Interface::SEARCH_READING_FONT_DEFAULT
    );
    migrateFontSetting4To5(
        settings,
        Keys::Interface::SEARCH_CONJ_FONT,
        Keys::Interface::SEARCH_CONJ_FONT_DEFAULT
    );
    migrateFontSetting4To5(
        settings,
        Keys::Interface::SEARCH_TAG_FONT,
        Keys::Interface::SEARCH_TAG_FONT_DEFAULT
    );
    migrateFontSetting4To5(
        settings,
        Keys::Interface::SEARCH_GLOSSARY_FONT,
        Keys::Interface::SEARCH_GLOSSARY_FONT_DEFAULT
    );
    migrateFontSetting4To5(
        settings,
        Keys::Interface::SEARCH_KANJI_FONT,
        Keys::Interface::SEARCH_KANJI_FONT_DEFAULT
    );
    migrateFontSetting4To5(
        settings,
        Keys::Interface::SubtitleList::PRIMARY_FONT,
        Keys::Interface::SubtitleList::PRIMARY_FONT_DEFAULT
    );
    migrateFontSetting4To5(
        settings,
        Keys::Interface::SubtitleList::SECONDARY_FONT,
        Keys::Interface::SubtitleList::SECONDARY_FONT_DEFAULT
    );
    settings.endGroup();
}


/**
 * @brief Migrate settings from version 5 to 6.
 */
void migrate5to6()
{
#if defined(Q_OS_UNIX) && !defined(Q_OS_MACOS)
    constexpr const char *KEY_WINDOW = "main-window";
    constexpr const char *KEY_WINDOW_SUBTITLE_LIST =
        "main-window/subtitle-list";
    constexpr const char *KEY_WINDOW_SEARCH =
        "main-window/search";

    constexpr const char *KEY_INTERNAL = "internal";
    constexpr const char *KEY_INTERNAL_AUTO_UPDATE_OPT_IN_SHOWN =
        "internal/auto-update-opt-in-shown";

    constexpr const char *KEY_RECENT = "recent";
    constexpr const char *KEY_RECENT_FILES = "recent/files";

    constexpr const char *KEY_DICTIONARY = "dictionary";
    constexpr const char *KEY_DICTIONARY_ORDER = "dictionary/order";

    const auto writeablePath =
    [] (QStandardPaths::StandardLocation location) -> QString
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
    };

    QString configDir = writeablePath(QStandardPaths::AppConfigLocation);
    QString dataDir = writeablePath(QStandardPaths::AppDataLocation);
    QString cacheDir = writeablePath(QStandardPaths::CacheLocation);

    /* Move Dictionary DB */
    const QString configDictionaryDb = configDir + "dictionaries.sqlite";
    const QString dataDictionaryDb = dataDir + "dictionaries.sqlite";
    if (QFile(configDictionaryDb).exists())
    {
        QFile(dataDictionaryDb).remove();
        QFile(configDictionaryDb).rename(dataDictionaryDb);
    }

    /* Move Dictionary Resource Directory */
    const QString configDictionaryRes = configDir + "res";
    const QString dataDictionaryRes = dataDir + "res";
    if (QDir(configDictionaryRes).exists())
    {
        QDir(dataDictionaryRes).removeRecursively();
        if (!QDir().rename(configDictionaryRes, dataDictionaryRes))
        {
            QDir().mkpath(dataDictionaryRes);
        }
    }

    QSettings oldSettings;
    QSettings newSettings(cacheDir + "memento.conf", QSettings::NativeFormat);

    /* Move Window Settings */
    const bool windowSubtitleList =
        oldSettings.value(KEY_WINDOW_SUBTITLE_LIST, false).toBool();
    const bool windowSearch =
        oldSettings.value(KEY_WINDOW_SEARCH, false).toBool();
    oldSettings.remove(KEY_WINDOW);
    newSettings.setValue(KEY_WINDOW_SUBTITLE_LIST, windowSubtitleList);
    newSettings.setValue(KEY_WINDOW_SEARCH, windowSearch);

    /* Move Internal Settings */
    const bool internalAutoUpdateOptInShown = oldSettings.value(
        KEY_INTERNAL_AUTO_UPDATE_OPT_IN_SHOWN,
        false
    ).toBool();
    oldSettings.remove(KEY_INTERNAL);
    newSettings.setValue(
        KEY_INTERNAL_AUTO_UPDATE_OPT_IN_SHOWN,
        internalAutoUpdateOptInShown
    );

    /* Move Recent Settings */
    QStringList recents = oldSettings.value(KEY_RECENT_FILES).toStringList();
    oldSettings.remove(KEY_RECENT);
    newSettings.setValue(KEY_RECENT_FILES, recents);

    /* Move Dictionary Settings */
    QVariantList order = oldSettings.value(KEY_DICTIONARY_ORDER).toList();
    oldSettings.remove(KEY_DICTIONARY);
    newSettings.setValue(KEY_DICTIONARY_ORDER, order);
#endif
}

} // namespace

void Migration::updateSettings()
{
    QSettings settings;
    uint version = settings.value(Keys::Version::VERSION, 0).toUInt();
    if (version == Keys::Version::CURRENT)
    {
        return;
    }
    else if (version > Keys::Version::CURRENT)
    {
        qWarning() <<
            "The Memento settings found belong to a newer version.\n"
            "No guarantees can be made that nothing will break or get lost.";
    }

    /* Migrate the settings */
    switch (version)
    {
        case 0:
            break;

        case 1:
            migrate1to2();
            [[fallthrough]];

        case 2:
            migrate2to3();
            [[fallthrough]];

        case 3:
            migrate3to4();
            [[fallthrough]];

        case 4:
            migrate4to5();
            [[fallthrough]];

        case 5:
            migrate5to6();
            break;
    }

    settings.setValue(Keys::Version::VERSION, Keys::Version::CURRENT);
}
