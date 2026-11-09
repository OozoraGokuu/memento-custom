#pragma once
#include "player/mpvplayer.h"

/** A separate player for rendered export previews, with no user config or scripts. */
class MpvPreview : public MpvPlayer
{
    Q_OBJECT
public:
    explicit MpvPreview(QQuickItem *parent = nullptr) : MpvPlayer(parent) {}
protected:
    void initOptions() override;
};
