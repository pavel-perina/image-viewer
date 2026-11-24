#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <QtQml/qqmlregistration.h>
#include <memory>

namespace iv {

// The colour tables (.cube files) of a folder and its subfolders, read in the background, for
// Adjust to offer by name. A name is the path below the folder without the extension, with "/"
// between folders ("Film/Portra 400"), and the names are in natural order. The folder is read
// again when it changes and on refresh(); subfolders are not watched.
class ColorTableList : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString folder READ folder WRITE setFolder NOTIFY folderChanged)
    Q_PROPERTY(QStringList names READ names NOTIFY namesChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)

public:
    // A folder with more tables than this lists the first ones only, so that pointing it at a
    // whole disk by mistake costs little.
    static constexpr int kMostTables = 5000;

    explicit ColorTableList(QObject* parent = nullptr);
    ~ColorTableList() override;

    QString folder() const;
    void setFolder(const QString& folder);
    QStringList names() const;
    bool loading() const;

    // The file of a name from names, or empty.
    Q_INVOKABLE QString pathOf(const QString& name) const;
    Q_INVOKABLE QUrl urlOf(const QString& name) const;
    // The name a step or more after a name, or before it for a negative step, with no table ("")
    // before the first: for the wheel to go through them. It stops at either end; a name not in
    // the list counts as no table.
    Q_INVOKABLE QString step(const QString& name, int by) const;
    // The name of a file in the folder, or empty if it is not one of them.
    Q_INVOKABLE QString nameOf(const QString& path) const;
    Q_INVOKABLE void refresh();

signals:
    void folderChanged();
    void namesChanged();
    void loadingChanged();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace iv
