#pragma once

#include <QPixmap>
#include <QWidget>

class ImageBackgroundWidget : public QWidget {
public:
    explicit ImageBackgroundWidget(QWidget* parent = nullptr);
protected:
    void paintEvent(QPaintEvent* event) override;
private:
    QPixmap m_background;
};
