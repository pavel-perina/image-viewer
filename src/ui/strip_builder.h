#pragma once

#include <QAbstractListModel>
#include <QSize>
#include <QString>
#include <QStringList>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace iv {

// The pictures of an image strip, in their order, and how they are put together: a list for
// the dialog to show and reorder, the size of the result as the options change, and the
// result itself, made only when it is saved. Sizes are read from the files in the background.
class StripBuilder : public QAbstractListModel {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QStringList paths READ paths WRITE setPaths NOTIFY picturesChanged)
    Q_PROPERTY(int count READ count NOTIFY picturesChanged)
    Q_PROPERTY(bool down READ down WRITE setDown NOTIFY optionsChanged)  // a column, not a row
    // The height across, the width down, that every picture is brought to.
    Q_PROPERTY(int extent READ extent WRITE setExtent NOTIFY optionsChanged)
    // 0 to bring every picture to the extent, 1 only those larger, 2 none, as StripResize.
    Q_PROPERTY(int resize READ resize WRITE setResize NOTIFY optionsChanged)
    Q_PROPERTY(int spacing READ spacing WRITE setSpacing NOTIFY optionsChanged)
    Q_PROPERTY(int margin READ margin WRITE setMargin NOTIFY optionsChanged)
    Q_PROPERTY(int shadow READ shadow WRITE setShadow NOTIFY optionsChanged)
    // On transparency rather than white.
    Q_PROPERTY(bool transparent READ transparent WRITE setTransparent NOTIFY optionsChanged)
    // While the sizes of the pictures are being read.
    Q_PROPERTY(bool reading READ reading NOTIFY resultChanged)
    Q_PROPERTY(QSize resultSize READ resultSize NOTIFY resultChanged)
    // Why the strip cannot be made, or empty.
    Q_PROPERTY(QString problem READ problem NOTIFY resultChanged)

public:
    static constexpr int kMaxPictures = 50;
    enum Role { NameRole = Qt::UserRole + 1, PathRole, ThumbnailRole };

    explicit StripBuilder(QObject* parent = nullptr);
    ~StripBuilder() override;

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    QStringList paths() const;
    void setPaths(const QStringList& paths);  // beyond kMaxPictures they are left out
    int count() const;
    bool down() const;
    void setDown(bool down);
    int extent() const;
    void setExtent(int extent);
    int resize() const;
    void setResize(int resize);
    int spacing() const;
    void setSpacing(int spacing);
    int margin() const;
    void setMargin(int margin);
    int shadow() const;
    void setShadow(int shadow);
    bool transparent() const;
    void setTransparent(bool transparent);
    bool reading() const;
    QSize resultSize() const;
    QString problem() const;

    Q_INVOKABLE void move(int from, int to);
    Q_INVOKABLE void remove(int index);
    // The strip, for BrowseContext::saveAs as an edited image; made as it is read, by the
    // save job, so that it can be cancelled. Empty while it cannot be made.
    Q_INVOKABLE QVariant strip() const;
    // A name for the new file, from the time, such as 2026-11-16_234516.webp.
    Q_INVOKABLE QString suggestedName() const;

signals:
    void picturesChanged();
    void optionsChanged();
    void resultChanged();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace iv
