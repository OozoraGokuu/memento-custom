#pragma once

#include <QFutureWatcher>
#include <QObject>
#include <QTimer>
#include <QTemporaryDir>
#include <QVariantMap>
#include "player/mpvcontroller.h"

class Context;

/** A standalone encoder job; no playback objects are accessed by the worker. */
class VideoExporter : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY changed)
    Q_PROPERTY(QString status READ status NOTIFY changed)
    Q_PROPERTY(QString outputPath READ outputPath NOTIFY changed)
    Q_PROPERTY(QString previewPath READ previewPath NOTIFY previewChanged)
    Q_PROPERTY(bool preparingPreview READ preparingPreview NOTIFY changed)
public:
    explicit VideoExporter(Context *context, QObject *parent = nullptr);
    ~VideoExporter() override;
    bool busy() const { return m_busy; }
    double progress() const { return m_progress; }
    QString status() const { return m_status; }
    QString outputPath() const { return m_outputPath; }
    QString previewPath() const { return m_previewPath; }
    bool preparingPreview() const { return m_busy && m_previewJob; }

    Q_INVOKABLE QVariantMap estimate(double start, double end, int maxHeight,
        int quality, bool audio) const;
    Q_INVOKABLE QString destination() const;
    Q_INVOKABLE bool start(const QString &mediaPath, double start, double end,
        int maxHeight, int quality, int primaryId, int secondaryId, bool audio);
    Q_INVOKABLE bool preview(const QString &mediaPath, double start, double end,
        int maxHeight, int quality, int primaryId, int secondaryId, bool audio);
    Q_INVOKABLE void clearPreview();
    Q_INVOKABLE void cancel();
    Q_INVOKABLE void openFolder() const;

    static QVariantMap plan(int width, int height, double duration,
        double start, double end, int maxHeight, int quality, bool audio);
signals:
    void changed();
    void previewChanged();
    void finished(bool success, const QString &message);
private:
    bool run(const QString &mediaPath, double start, double end, int maxHeight,
        int quality, int primaryId, int secondaryId, bool audio, bool preview);
    Context *m_context;
    QFutureWatcher<QVariantMap> m_watcher;
    QTimer m_timer;
    std::shared_ptr<MpvEncodingControl> m_control;
    bool m_busy{false};
    double m_progress{0.0};
    QString m_status;
    QString m_outputPath;
    QString m_previewPath;
    std::unique_ptr<QTemporaryDir> m_previewDirectory;
    bool m_previewJob{false};
    bool m_discardPreview{false};
};
