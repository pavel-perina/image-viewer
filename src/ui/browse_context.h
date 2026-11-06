#pragma once

#include <QObject>
#include <QString>
#include <QUrl>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace iv {

// One browsing context: a directory, its images and the current one. Everything a tab or
// file panel owns lives here, so several can exist side by side.
class BrowseContext : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString directory READ directory NOTIFY directoryChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY filesChanged)
    Q_PROPERTY(bool hasNext READ hasNext NOTIFY currentChanged)
    Q_PROPERTY(bool hasPrevious READ hasPrevious NOTIFY currentChanged)

public:
    explicit BrowseContext(QObject* parent = nullptr);
    ~BrowseContext() override;

    QString directory() const;
    QString currentPath() const;
    QString currentName() const;
    int currentIndex() const;
    int fileCount() const;
    bool hasNext() const;
    bool hasPrevious() const;

    // Opens an image file and scans its directory, or scans a directory and opens its
    // first image. Scanning happens in the background.
    Q_INVOKABLE void openPath(const QString& path);
    Q_INVOKABLE void openUrl(const QUrl& url);
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();

signals:
    void directoryChanged();
    void currentChanged();
    void filesChanged();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace iv
