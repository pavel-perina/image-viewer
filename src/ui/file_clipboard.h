#pragma once

#include <QImage>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include "core/work_queue.h"

namespace iv {

// Files and text on the system clipboard, and files dragged out, as other applications take
// them. One for the whole application, as the clipboard is.
class FileClipboard : public QObject {
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON
    // Files or an image are there for pasting.
    Q_PROPERTY(bool canPaste READ canPaste NOTIFY changed)

public:
    explicit FileClipboard(QObject* parent = nullptr);
    ~FileClipboard() override;

    bool canPaste() const;

    // Puts the files on the clipboard for other applications, and a single file also as an
    // image, which arrives there a moment later as it has to be decoded.
    Q_INVOKABLE void copyFiles(const QStringList& paths);
    // Files marked as cut, as file managers mark them: the one that pastes moves them. Without
    // an image, as the files are to go rather than be copied.
    Q_INVOKABLE void cutFiles(const QStringList& paths);
    Q_INVOKABLE void copyPaths(const QStringList& paths);  // as text, with the system's separators
    Q_INVOKABLE void copyText(const QString& text);
    // Local files on the clipboard, which take precedence over an image there.
    Q_INVOKABLE QStringList files() const;
    Q_INVOKABLE bool isCut() const;
    Q_INVOKABLE QString text() const;
    Q_INVOKABLE void clear();

    // Drags the files out, to another application or a folder of the tree, until the button is
    // released. A copy unless Ctrl, Shift or the other side asks for a move, as in file managers;
    // Qt Quick's own drag would make a move the default. A moved file is never deleted here: the
    // other side moves it, or it stays a copy. The image, when not null, is shown by the pointer.
    Q_INVOKABLE void dragFiles(const QStringList& paths, const QImage& image);

signals:
    void changed();

private:
    WorkQueue& m_queue = WorkQueue::global();
    WorkQueue::GroupId m_group = m_queue.createGroup();
};

}  // namespace iv
