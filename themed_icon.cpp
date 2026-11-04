#include <QApplication>
#include <QFile>
#include <QIconEngine>
#include <QPainter>
#include <QStyle>
#include <QStyleOption>
#include <QSvgRenderer>

#include "themed_icon.h"

namespace
{
// 图标原色（为亮色背景设计）与暗色主题下的替换色。
constexpr std::pair<const char*, const char*> kDarkColors[] = {
    {"#354863", "#D5E0EE"},  // 主要控件
    {"#64758D", "#A2B2C7"},  // 次要控件
    {"#9DA9B8", "#5F7088"},  // 不可用状态，需比次要控件更暗
    {"#F5F8FC", "#253244"},  // 与按钮底色相同的衬底描边
};

class themed_svg_engine final : public QIconEngine
{
   public:
    explicit themed_svg_engine(QString resource) : resource_(std::move(resource)) {}

    void paint(QPainter* painter, const QRect& rect, QIcon::Mode /*mode*/, QIcon::State /*state*/) override
    {
        QSvgRenderer renderer(svg_data());
        renderer.render(painter, rect);
    }

    QPixmap pixmap(const QSize& size, QIcon::Mode mode, QIcon::State state) override
    {
        QPixmap result(size);
        result.fill(Qt::transparent);
        {
            QPainter painter(&result);
            paint(&painter, QRect(QPoint(0, 0), size), mode, state);
        }
        if (mode == QIcon::Disabled)
        {
            QStyleOption option;
            option.palette = QApplication::palette();
            return QApplication::style()->generatedIconPixmap(QIcon::Disabled, result, &option);
        }
        return result;
    }

    QIconEngine* clone() const override { return new themed_svg_engine(resource_); }

   private:
    QByteArray svg_data()
    {
        const bool dark = qApp->property("darkMode").toBool();
        QByteArray& data = dark ? dark_data_ : light_data_;
        if (data.isEmpty())
        {
            QFile file(resource_);
            if (file.open(QFile::ReadOnly)) data = file.readAll();
            if (dark)
            {
                for (const auto& [light, replacement] : kDarkColors) data.replace(light, replacement);
            }
        }
        return data;
    }

    QString resource_;
    QByteArray light_data_;
    QByteArray dark_data_;
};
}  // namespace

QIcon themed_icon(const QString& resource) { return QIcon(new themed_svg_engine(resource)); }
