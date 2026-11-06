#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace iv {

// The visible subfolders of one folder in natural order, read in the background.
class SubfolderList : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(QStringList folders READ folders NOTIFY foldersChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY foldersChanged)

public:
    explicit SubfolderList(QObject* parent = nullptr);
    ~SubfolderList() override;

    QString path() const;
    void setPath(const QString& path);  // always reads again, as folders change
    QStringList folders() const;
    bool loading() const;

signals:
    void pathChanged();
    void foldersChanged();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace iv
