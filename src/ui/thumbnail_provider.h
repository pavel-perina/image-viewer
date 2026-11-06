#pragma once

#include <QQuickAsyncImageProvider>
#include <QString>

namespace iv {

// Serves image://thumbnails/<id> from the shared work queue. Requests behind visible items
// run before older ones and are cancelled when their delegate goes away, so scrolling
// through a huge directory only decodes what is on screen.
class ThumbnailProvider : public QQuickAsyncImageProvider {
public:
    static constexpr const char* kProviderId = "thumbnails";

    // The URL to give an Image item for this file.
    static QString urlForPath(const QString& path);

    QQuickImageResponse* requestImageResponse(const QString& id, const QSize& requestedSize) override;
};

}  // namespace iv
