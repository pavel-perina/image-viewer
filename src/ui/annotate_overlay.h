#pragma once

#include <QColor>
#include <QImage>
#include <QRect>
#include <QStringList>
#include <QtQml/qqmlregistration.h>

#include <vector>

#include "core/shapes.h"
#include "ui/image_overlay.h"

namespace iv {

// Annotations being made: shapes drawn on the image, which can be selected, moved, reshaped
// by their grips and removed until they are applied, and are then burned into the picture as
// one edit. With a drawing tool, a drag draws a new shape and a click selects the one under
// it; with Select, a drag moves it. Delete removes the shape selected, Esc deselects it. The
// Text tool draws a box, or with a click places one, whose words are then set from outside.
//
// The style shown and set (colour, width and so on) is that of the shape selected, or else
// that of the tool, which each tool remembers between runs.
class AnnotateOverlay : public ImageOverlay {
    Q_OBJECT
    QML_ELEMENT
    Q_PROPERTY(Tool tool READ tool WRITE setTool NOTIFY toolChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    Q_PROPERTY(int selected READ selected NOTIFY selectedChanged)
    // The tool whose style is shown: that of the shape selected, or the tool chosen.
    Q_PROPERTY(Tool styledTool READ styledTool NOTIFY styleValuesChanged)
    Q_PROPERTY(QColor colour READ colour WRITE setColour NOTIFY styleValuesChanged)
    // In image pixels: of lines, or for Pixelate of its blocks.
    Q_PROPERTY(double lineWidth READ lineWidth WRITE setLineWidth NOTIFY styleValuesChanged)
    Q_PROPERTY(bool filled READ filled WRITE setFilled NOTIFY styleValuesChanged)
    Q_PROPERTY(bool outline READ outline WRITE setOutline NOTIFY styleValuesChanged)
    Q_PROPERTY(bool shadow READ shadow WRITE setShadow NOTIFY styleValuesChanged)
    // The height of letters, in image pixels.
    Q_PROPERTY(double textSize READ textSize WRITE setTextSize NOTIFY styleValuesChanged)
    Q_PROPERTY(QString fontFamily READ fontFamily WRITE setFontFamily NOTIFY styleValuesChanged)
    Q_PROPERTY(bool bold READ bold WRITE setBold NOTIFY styleValuesChanged)
    // The typefaces to choose from.
    Q_PROPERTY(QStringList fontFamilies READ fontFamilies CONSTANT)
    // What the text box selected says; empty and unchangeable with none selected.
    Q_PROPERTY(QString text READ text WRITE setText NOTIFY textChanged)

public:
    enum class Tool { Select, Line, Arrow, Rectangle, Marker, Pixelate, Text };
    Q_ENUM(Tool)

    explicit AnnotateOverlay(QQuickItem* parent = nullptr);

    Tool tool() const { return m_tool; }
    void setTool(Tool tool);
    int count() const { return static_cast<int>(m_shapes.size()); }
    int selected() const { return m_selected; }
    Tool styledTool() const;
    QColor colour() const;
    void setColour(const QColor& colour);
    double lineWidth() const;
    void setLineWidth(double width);
    bool filled() const { return shownStyle().filled; }
    void setFilled(bool filled);
    bool outline() const { return shownStyle().outline; }
    void setOutline(bool outline);
    bool shadow() const { return shownStyle().shadow; }
    void setShadow(bool shadow);
    double textSize() const { return shownStyle().textSize; }
    void setTextSize(double size);
    QString fontFamily() const;
    void setFontFamily(const QString& family);
    bool bold() const { return shownStyle().bold; }
    void setBold(bool bold);
    QStringList fontFamilies() const;
    QString text() const;
    void setText(const QString& text);

    const std::vector<Shape>& shapes() const { return m_shapes; }

    Q_INVOKABLE void removeSelected();
    Q_INVOKABLE void deselect() { select(-1); }
    // Removes every shape, as when leaving without applying.
    Q_INVOKABLE void clear();
    // Burns the shapes into the picture as one edit, and removes them here.
    Q_INVOKABLE void apply();

    void paint(QPainter* painter) override;

signals:
    void toolChanged();
    void countChanged();
    void selectedChanged();
    void styleValuesChanged();
    void textChanged();
    // A text box was made and waits for its words.
    void textWanted();

protected:
    void viewAttached() override;
    void pressed(QPointF itemPoint) override;
    void dragged(QPointF itemPoint, bool moved) override;
    void released(QPointF itemPoint) override;
    Qt::CursorShape cursorAt(QPointF itemPoint) const override;
    bool keyPressed(QKeyEvent* event) override;

private:
    ShapeStyle styleOf(Tool tool) const;
    ShapeStyle shownStyle() const;
    // Changes the style shown: of the shape selected, or of the tool, which remembers it.
    template <typename Change>
    void changeStyle(const char* key, const QVariant& value, Change change);
    ShapeHit hitAt(QPointF itemPoint) const;
    // The point for a shape of a kind: on a whole pixel for the kinds that keep to them.
    static PointD placed(ShapeKind kind, PointD imagePoint);
    void paintPreview(QPainter* painter, std::size_t index);
    void select(int index);
    void shapesChanged();

    std::vector<Shape> m_shapes;
    int m_selected = -1;
    Tool m_tool = Tool::Arrow;

    enum class Drag { None, Grip, Move, Draw, Click };
    Drag m_drag = Drag::None;
    ShapeHit m_hit;
    Shape m_atPress;
    PointD m_pressImage;

    // What the shapes that depend on the picture under them will give, by their place in the
    // list, each kept until its shape changes.
    struct Preview {
        Shape shape;
        QImage pixels;
        QRect where;
    };
    std::vector<Preview> m_previews;
};

}  // namespace iv
