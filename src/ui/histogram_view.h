#pragma once

#include <QQuickPaintedItem>
#include <QString>
#include <QVariant>
#include <QtQml/qqmlregistration.h>

#include <memory>

namespace iv {

// Draws a histogram: one filled shape for gray, three overlapping ones for RGB. It knows
// nothing about where the data comes from, so any view can bind its histogram here.
class HistogramView : public QQuickPaintedItem {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(QVariant histogram READ histogram WRITE setHistogram NOTIFY histogramChanged)
    Q_PROPERTY(bool logScale READ logScale WRITE setLogScale NOTIFY displayChanged)
    // Spreads the values in use over the full width, for example 12-bit data in 16-bit samples.
    Q_PROPERTY(bool fitRange READ fitRange WRITE setFitRange NOTIFY displayChanged)
    Q_PROPERTY(QString description READ description NOTIFY displayChanged)

public:
    explicit HistogramView(QQuickItem* parent = nullptr);
    ~HistogramView() override;

    QVariant histogram() const;
    void setHistogram(const QVariant& histogram);
    bool logScale() const;
    void setLogScale(bool enabled);
    bool fitRange() const;
    void setFitRange(bool enabled);
    QString description() const;

    void paint(QPainter* painter) override;

signals:
    void histogramChanged();
    void displayChanged();

private:
    struct Impl;
    std::unique_ptr<Impl> m_impl;
};

}  // namespace iv
