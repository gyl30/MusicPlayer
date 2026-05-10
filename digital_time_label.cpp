#include <array>

#include <QPainter>
#include <QPaintEvent>
#include <QPalette>

#include "digital_time_label.h"

namespace
{
constexpr int kPreferredHeight = 24;
constexpr int kMinimumDigitHeight = 10;

constexpr std::array<std::array<bool, 7>, 10> kSegments = {{
    {{true, true, true, true, true, true, false}},
    {{false, true, true, false, false, false, false}},
    {{true, true, false, true, true, false, true}},
    {{true, true, true, true, false, false, true}},
    {{false, true, true, false, false, true, true}},
    {{true, false, true, true, false, true, true}},
    {{true, false, true, true, true, true, true}},
    {{true, true, true, false, false, false, false}},
    {{true, true, true, true, true, true, true}},
    {{true, true, true, true, false, true, true}},
}};

int digit_width_for_height(int digit_height)
{
    return qMax(8, static_cast<int>(static_cast<double>(digit_height) * 0.58));
}

int gap_for_height(int digit_height)
{
    return qMax(2, static_cast<int>(static_cast<double>(digit_height) * 0.12));
}

int segment_thickness_for_height(int digit_height)
{
    return qMax(2, static_cast<int>(static_cast<double>(digit_height) * 0.16));
}
}  // namespace

digital_time_label::digital_time_label(QWidget* parent) : QLabel(parent)
{
    setText("00:00");
    setAttribute(Qt::WA_TranslucentBackground);
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setFixedHeight(kPreferredHeight);
}

QSize digital_time_label::sizeHint() const
{
    return QSize(text_width(text(), kPreferredHeight - 2), kPreferredHeight);
}

void digital_time_label::paintEvent(QPaintEvent* event)
{
    (void)event;

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);

    const int digit_height = qMax(kMinimumDigitHeight, height() - 2);
    const int digit_width = digit_width_for_height(digit_height);
    const int gap = gap_for_height(digit_height);
    const int colon_width = qMax(3, segment_thickness_for_height(digit_height));
    const int total_width = text_width(text(), digit_height);

    int x = width() - total_width;
    const int y = (height() - digit_height) / 2;
    for (const QChar ch : text())
    {
        if (ch.isDigit())
        {
            draw_digit(painter, x, y, digit_width, digit_height, ch);
            x += digit_width + gap;
            continue;
        }

        if (ch == ':')
        {
            draw_colon(painter, x, y, colon_width, digit_height);
            x += colon_width + gap;
        }
    }
}

int digital_time_label::text_width(const QString& text, int digit_height) const
{
    const int digit_width = digit_width_for_height(digit_height);
    const int gap = gap_for_height(digit_height);
    const int colon_width = qMax(3, segment_thickness_for_height(digit_height));

    int width = 0;
    int visible_chars = 0;
    for (const QChar ch : text)
    {
        if (ch.isDigit())
        {
            width += digit_width;
            ++visible_chars;
        }
        else if (ch == ':')
        {
            width += colon_width;
            ++visible_chars;
        }
    }

    if (visible_chars > 1)
    {
        width += gap * (visible_chars - 1);
    }
    return width;
}

void digital_time_label::draw_digit(QPainter& painter, int x, int y, int width, int height, QChar digit) const
{
    const int digit_value = digit.digitValue();
    if (digit_value < 0 || digit_value > 9)
    {
        return;
    }

    const int thickness = segment_thickness_for_height(height);
    const int half_y = y + (height / 2);
    const int vertical_height = qMax(1, (height - (3 * thickness)) / 2);
    const int radius = qMax(1, thickness / 2);

    const QColor active_color = palette().color(QPalette::WindowText);
    QColor inactive_color = active_color;
    inactive_color.setAlpha(32);

    const std::array<QRect, 7> segment_rects = {{
        QRect(x + thickness, y, width - (2 * thickness), thickness),
        QRect(x + width - thickness, y + thickness, thickness, vertical_height),
        QRect(x + width - thickness, half_y + (thickness / 2), thickness, vertical_height),
        QRect(x + thickness, y + height - thickness, width - (2 * thickness), thickness),
        QRect(x, half_y + (thickness / 2), thickness, vertical_height),
        QRect(x, y + thickness, thickness, vertical_height),
        QRect(x + thickness, half_y - (thickness / 2), width - (2 * thickness), thickness),
    }};

    for (int i = 0; i < static_cast<int>(segment_rects.size()); ++i)
    {
        painter.setBrush(kSegments[static_cast<size_t>(digit_value)][static_cast<size_t>(i)] ? active_color : inactive_color);
        painter.setPen(Qt::NoPen);
        painter.drawRoundedRect(segment_rects[static_cast<size_t>(i)], radius, radius);
    }
}

void digital_time_label::draw_colon(QPainter& painter, int x, int y, int width, int height) const
{
    const int dot_size = qMax(2, width);
    const int radius = qMax(1, dot_size / 2);
    const QColor dot_color = palette().color(QPalette::WindowText);
    const int center_x = x + ((width - dot_size) / 2);
    const int upper_y = y + (height / 3) - (dot_size / 2);
    const int lower_y = y + ((height * 2) / 3) - (dot_size / 2);

    painter.setPen(Qt::NoPen);
    painter.setBrush(dot_color);
    painter.drawRoundedRect(QRect(center_x, upper_y, dot_size, dot_size), radius, radius);
    painter.drawRoundedRect(QRect(center_x, lower_y, dot_size, dot_size), radius, radius);
}
