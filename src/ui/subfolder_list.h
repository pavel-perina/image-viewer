#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace iv {

// The subfolders of one folder in natural order, read in the background; hidden ones only when
// showHidden.
class SubfolderList : public QObject {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool showHidden READ showHidden WRITE setShowHidden NOTIFY showHiddenChanged)
    Q_PROPERTY(QStringList folders READ folders NOTIFY foldersChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY foldersChanged)

public:
    explicit SubfolderList(QObject* parent = nullptr);
    ~SubfolderList() override;

    QString path() const;
    void setPath(const QString& path);  // always reads again, as folders change
    bool showHidden() const;
    void setShowHidden(bool show);
    QStringList folders() const;
    bool loading() const;

signals:
    void pathChanged();
    void showHiddenChanged();
    void foldersChanged();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace iv
