#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QVariantList>
#include <QtQml/qqmlregistration.h>

#include <memory>

class QAbstractItemModel;
Q_MOC_INCLUDE(<QAbstractItemModel>)

namespace iv {

// One browsing context: a directory, its images, the current one and the selection. Everything
// a tab or file panel owns lives here, so several can exist side by side.
class BrowseContext : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString directory READ directory NOTIFY directoryChanged)
    Q_PROPERTY(QVariantList breadcrumbs READ breadcrumbs NOTIFY directoryChanged)
    Q_PROPERTY(QAbstractItemModel* files READ files CONSTANT)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentChanged)
    Q_PROPERTY(QString currentName READ currentName NOTIFY currentChanged)
    Q_PROPERTY(int currentIndex READ currentIndex NOTIFY currentChanged)
    Q_PROPERTY(int fileCount READ fileCount NOTIFY filesChanged)
    Q_PROPERTY(bool hasNext READ hasNext NOTIFY currentChanged)
    Q_PROPERTY(bool hasPrevious READ hasPrevious NOTIFY currentChanged)
    Q_PROPERTY(int selectedCount READ selectedCount NOTIFY selectionChanged)
    Q_PROPERTY(QStringList selectedPaths READ selectedPaths NOTIFY selectionChanged)

public:
    explicit BrowseContext(QObject* parent = nullptr);
    ~BrowseContext() override;

    QString directory() const;
    // One {name, path} map per level of the directory, starting at the root.
    QVariantList breadcrumbs() const;
    // Images of the directory, with the roles name, path, thumbnail and selected.
    QAbstractItemModel* files() const;
    QString currentPath() const;
    QString currentName() const;
    int currentIndex() const;
    int fileCount() const;
    bool hasNext() const;
    bool hasPrevious() const;
    int selectedCount() const;
    QStringList selectedPaths() const;  // in the order of the file list

    // Opens an image file and scans its directory, or scans a directory and opens its
    // first image. Scanning happens in the background.
    Q_INVOKABLE void openPath(const QString& path);
    Q_INVOKABLE void openUrl(const QUrl& url);
    // Moving to another image selects only that image.
    Q_INVOKABLE void setCurrentIndex(int index);
    // A click on a file. Plain selects only it, ctrl toggles it, and shift selects the range
    // from the anchor, the file last clicked without shift. Ctrl and shift adds that range.
    Q_INVOKABLE void clickFile(int index, bool ctrl, bool shift);
    // Moves the current image and selects from the anchor to it, as shift and an arrow key do.
    Q_INVOKABLE void extendSelectionTo(int index);
    Q_INVOKABLE void selectAll();
    Q_INVOKABLE void clearSelection();  // the current image stays current
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();

signals:
    void directoryChanged();
    void currentChanged();
    void filesChanged();
    void selectionChanged();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace iv
