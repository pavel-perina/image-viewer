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

public:
    explicit ThumbnailStore(QObject* parent = nullptr);
    ~ThumbnailStore() override;

    Q_INVOKABLE void measure();
    Q_INVOKABLE void clear();

signals:
    // Bytes as a double, which QML takes without losing anything that matters here.
    void measured(bool available, double count, double bytes);
    void cleared(bool done);

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace iv
