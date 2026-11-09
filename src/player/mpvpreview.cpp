#include "player/mpvpreview.h"

void MpvPreview::initOptions()
{
    const std::pair<const char *, const char *> options[] = {
        {"vo", "libmpv"}, {"config", "no"}, {"load-scripts", "no"},
        {"ytdl", "no"}, {"osc", "no"}, {"terminal", "no"},
        {"input-default-bindings", "no"}, {"input-vo-keyboard", "no"},
        {"load-stats-overlay", "no"}, {"save-position-on-quit", "no"},
        {"sub-auto", "no"}, {"audio-file-auto", "no"},
        {"sid", "no"}, {"secondary-sid", "no"},
        {"pause", "yes"}, {"keep-open", "yes"}, {"volume", "0"},
        {"osd-level", "0"},
    };
    for (const auto &[name, value] : options)
        ::mpv_set_option_string(handle(), name, value);
}
