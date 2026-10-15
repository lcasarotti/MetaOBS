#include "StreamDeckImages.hpp"

#include <QBuffer>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QPainter>
#include <QTransform>

namespace OBS::Accessibility::StreamDeck {

namespace {
constexpr int marginPixels = 4;
constexpr int smallestTextPixels = 9;
constexpr int jpegQuality = 95;
constexpr size_t bmpHeaderLength = 54;
/* 72 dots per inch. */
constexpr uint32_t bmpPixelsPerMeter = 2835;

const QColor availableColor(255, 255, 255);
const QColor unavailableColor(128, 128, 128);

void Put16(std::vector<uint8_t> &bytes, size_t offset, uint32_t value)
{
	bytes[offset] = static_cast<uint8_t>(value);
	bytes[offset + 1] = static_cast<uint8_t>(value >> 8);
}

void Put32(std::vector<uint8_t> &bytes, size_t offset, uint32_t value)
{
	Put16(bytes, offset, value);
	Put16(bytes, offset + 2, value >> 16);
}

/* The largest font with which the text fits, wrapped at spaces, or at any
 * character in the smallest size. */
int FitText(QFont &font, const QRect &area, const QString &text)
{
	int flags = Qt::AlignCenter | Qt::TextWordWrap;

	for (int pixels = area.height() / 3; pixels > smallestTextPixels; pixels--) {
		font.setPixelSize(pixels);
		QRect needed = QFontMetrics(font).boundingRect(area, flags, text);
		if (needed.width() <= area.width() && needed.height() <= area.height()) {
			return flags;
		}
	}

	font.setPixelSize(smallestTextPixels);
	return Qt::AlignCenter | Qt::TextWrapAnywhere;
}
} // namespace

QImage RenderLabel(const Model &model, const QString &label, bool available)
{
	QImage image(model.imageSize, model.imageSize, QImage::Format_RGB888);
	image.fill(Qt::black);

	QString text = label.trimmed();
	if (text.isEmpty()) {
		return image;
	}

	QRect area = image.rect().adjusted(marginPixels, marginPixels, -marginPixels, -marginPixels);
	QFont font = QGuiApplication::font();
	font.setBold(true);
	font.setStyleStrategy(QFont::PreferAntialias);
	int flags = FitText(font, area, text);

	QPainter painter(&image);
	painter.setRenderHint(QPainter::TextAntialiasing);
	painter.setFont(font);
	painter.setPen(available ? availableColor : unavailableColor);
	painter.drawText(area, flags, text);
	painter.end();

	return image;
}

QImage ToDeviceOrientation(const Model &model, const QImage &image)
{
	QImage turned = image;
	if (model.imageRotation != 0) {
		/* Qt turns clockwise for positive angles, since y points
		 * down. */
		turned = turned.transformed(QTransform().rotate(-model.imageRotation));
	}

	Qt::Orientations flips;
	if (model.flipHorizontally) {
		flips |= Qt::Horizontal;
	}
	if (model.flipVertically) {
		flips |= Qt::Vertical;
	}
	return flips ? turned.flipped(flips) : turned;
}

std::vector<uint8_t> EncodeBmp(const QImage &image)
{
	QImage rgb = image.convertToFormat(QImage::Format_RGB888);
	auto width = static_cast<uint32_t>(rgb.width());
	auto height = static_cast<uint32_t>(rgb.height());
	uint32_t rowLength = (width * 3 + 3) / 4 * 4;
	uint32_t dataLength = rowLength * height;

	std::vector<uint8_t> bmp(bmpHeaderLength + dataLength, 0);
	bmp[0] = 'B';
	bmp[1] = 'M';
	Put32(bmp, 2, static_cast<uint32_t>(bmp.size()));
	Put32(bmp, 10, bmpHeaderLength);
	Put32(bmp, 14, 40);
	Put32(bmp, 18, width);
	Put32(bmp, 22, height);
	Put16(bmp, 26, 1);
	Put16(bmp, 28, 24);
	Put32(bmp, 34, dataLength);
	Put32(bmp, 38, bmpPixelsPerMeter);
	Put32(bmp, 42, bmpPixelsPerMeter);

	/* Rows go from the bottom up, pixels in blue, green, red order. */
	for (uint32_t y = 0; y < height; y++) {
		const uchar *source = rgb.constScanLine(static_cast<int>(height - 1 - y));
		size_t row = bmpHeaderLength + static_cast<size_t>(y) * rowLength;
		for (uint32_t x = 0; x < width; x++) {
			bmp[row + x * 3] = source[x * 3 + 2];
			bmp[row + x * 3 + 1] = source[x * 3 + 1];
			bmp[row + x * 3 + 2] = source[x * 3];
		}
	}

	return bmp;
}

std::vector<uint8_t> EncodeKeyImage(const Model &model, const QImage &image)
{
	QImage sized = image;
	if (sized.width() != model.imageSize || sized.height() != model.imageSize) {
		sized = sized.scaled(model.imageSize, model.imageSize, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
	}
	QImage turned = ToDeviceOrientation(model, sized);

	if (model.imageFormat == ImageFormat::Bmp) {
		return EncodeBmp(turned);
	}

	QByteArray jpeg;
	QBuffer buffer(&jpeg);
	buffer.open(QIODevice::WriteOnly);
	turned.save(&buffer, "JPG", jpegQuality);
	return std::vector<uint8_t>(jpeg.begin(), jpeg.end());
}

} // namespace OBS::Accessibility::StreamDeck
