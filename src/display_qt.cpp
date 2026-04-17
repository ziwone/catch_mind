#include "display.h"

#include <QApplication>
#include <QImage>
#include <QLabel>
#include <QPainter>
#include <QPixmap>
#include <QFont>
#include <QCoreApplication>

#include <string>

Display::Display()
    : qtApp(nullptr),
      qtWindow(nullptr),
      qtImage(nullptr),
      width(1024),
      height(600),
    bitsPerPixel(32),
    pageFlipEnabled(false),
    softwareDoubleBufferEnabled(true),
      inFrame(false),
      isInitialized(false) {
}

Display::~Display() {
    close_fb();
}

bool Display::init() {
    if (isInitialized) {
        return true;
    }

    if (qtApp == nullptr) {
        static int argc = 1;
        static char arg0[] = "catch_mind_qt";
        static char *argv[] = {arg0, nullptr};
        qtApp = new QApplication(argc, argv);
    }

    if (qtWindow == nullptr) {
        QLabel *label = new QLabel();
        label->setWindowTitle("CatchMind Qt Preview");
        label->resize(width, height);
        label->setMinimumSize(width, height);
        label->show();
        qtWindow = label;
    }

    if (qtImage == nullptr) {
        QImage *image = new QImage(width, height, QImage::Format_RGB888);
        image->fill(Qt::black);
        qtImage = image;
    }

    isInitialized = true;
    QCoreApplication::processEvents();
    return true;
}

void Display::close_fb() {
    if (!isInitialized) {
        return;
    }

    if (qtImage != nullptr) {
        delete static_cast<QImage *>(qtImage);
        qtImage = nullptr;
    }

    if (qtWindow != nullptr) {
        delete static_cast<QLabel *>(qtWindow);
        qtWindow = nullptr;
    }

    if (qtApp != nullptr) {
        delete static_cast<QApplication *>(qtApp);
        qtApp = nullptr;
    }

    isInitialized = false;
}

void Display::beginFrame() {
    inFrame = true;
}

void Display::endFrame() {
    if (!isInitialized || qtWindow == nullptr || qtImage == nullptr) {
        return;
    }

    QLabel *label = static_cast<QLabel *>(qtWindow);
    QImage *image = static_cast<QImage *>(qtImage);
    label->setPixmap(QPixmap::fromImage(*image));
    QCoreApplication::processEvents();
    inFrame = false;
}

void Display::clearScreen(unsigned int color) {
    drawRect(0, 0, width, height, color);
}

void Display::drawPixel(int x, int y, unsigned int color) {
    if (!isInitialized || qtImage == nullptr) {
        return;
    }
    if (x < 0 || x >= width || y < 0 || y >= height) {
        return;
    }

    QImage *image = static_cast<QImage *>(qtImage);
    int r = (int)((color >> 16) & 0xff);
    int g = (int)((color >> 8) & 0xff);
    int b = (int)(color & 0xff);
    image->setPixelColor(x, y, QColor(r, g, b));
}

void Display::drawRect(int x, int y, int w, int h, unsigned int color) {
    if (!isInitialized || qtImage == nullptr || w <= 0 || h <= 0) {
        return;
    }

    QImage *image = static_cast<QImage *>(qtImage);
    QPainter painter(image);
    QColor qc((int)((color >> 16) & 0xff),
              (int)((color >> 8) & 0xff),
              (int)(color & 0xff));
    painter.fillRect(x, y, w, h, qc);
}

void Display::drawText(int x, int y, const std::string &text, unsigned int color, int scale) {
    if (!isInitialized || qtImage == nullptr) {
        return;
    }

    if (scale < 1) {
        scale = 1;
    }

    QImage *image = static_cast<QImage *>(qtImage);
    QPainter painter(image);
    QColor qc((int)((color >> 16) & 0xff),
              (int)((color >> 8) & 0xff),
              (int)(color & 0xff));
    painter.setPen(qc);

    QFont font("Monospace");
    font.setStyleHint(QFont::TypeWriter);
    font.setPixelSize(8 * scale);
    painter.setFont(font);

    painter.drawText(x, y + (10 * scale), QString::fromStdString(text));
}

bool Display::saveFrameToPPM(const std::string &path) const {
    if (!isInitialized || qtImage == nullptr) {
        return false;
    }
    QImage *image = static_cast<QImage *>(qtImage);
    return image->save(QString::fromStdString(path), "PPM");
}

const unsigned int Display::COLOR_RED = 0xff0000;
const unsigned int Display::COLOR_GREEN = 0x00ff00;
const unsigned int Display::COLOR_BLUE = 0x0000ff;
const unsigned int Display::COLOR_BLACK = 0x000000;
const unsigned int Display::COLOR_WHITE = 0xffffff;
const unsigned int Display::COLOR_YELLOW = 0xffff00;
