#include "ImageBackgroundWidget.h"

#include <QPainter>

ImageBackgroundWidget::ImageBackgroundWidget(QWidget* parent)
    : QWidget(parent), m_background(QStringLiteral(":/assets/cybersecurity_network_background.png"))
{
    setAttribute(Qt::WA_OpaquePaintEvent);
}

void ImageBackgroundWidget::paintEvent(QPaintEvent*)
{
    QPainter painter(this);
    if (m_background.isNull()) {
        painter.fillRect(rect(), QColor(QStringLiteral("#06122f")));
        return;
    }
    const QPixmap scaled = m_background.scaled(size(), Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
    const int x = (scaled.width() - width()) / 2;
    const int y = (scaled.height() - height()) / 2;
    painter.drawPixmap(rect(), scaled, QRect(x, y, width(), height()));
}
