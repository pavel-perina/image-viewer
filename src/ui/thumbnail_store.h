#pragma once

#include <QObject>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace iv {

// The thumbnails kept on disk, for the user to see how much there is and to clear it. Both run
// in the background and answer with a signal.
class ThumbnailStore : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    Q_PROPERTY(bool available READ available NOTIFY usageChanged)
    Q_PROPERTY(bool measuredOnce READ measuredOnce NOTIFY usageChanged)
    Q_PROPERTY(double count READ count NOTIFY usageChanged)
    Q_PROPERTY(double bytes READ bytes NOTIFY usageChanged)
    Q_PROPERTY(bool busy READ busy NOTIFY busyChanged)
    Q_PROPERTY(QString usageText READ usageText NOTIFY usageChanged)

public:
    explicit ThumbnailStore(QObject* parent = nullptr);
    ~ThumbnailStore() override;

    Q_INVOKABLE void measure();
    Q_INVOKABLE void requestClear(QObject* owner);
    Q_INVOKABLE bool clear();
    bool available() const;
    bool measuredOnce() const;
    double count() const;
    double bytes() const;
    bool busy() const;
    QString usageText() const;

signals:
    // Bytes as a double, which QML takes without losing anything that matters here.
    void measured(bool available, double count, double bytes);
    void cleared(bool done);
    void usageChanged();
    void busyChanged();
    void clearRequested(QObject* owner, bool available, double count, double bytes);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace iv
