#pragma once

#include "StreamDeckProtocol.hpp"

#include <QImage>
#include <QString>

#include <cstdint>
#include <vector>

/* The pictures shown on Stream Deck keys: the key's label as text, for
 * sighted people working with a screen reader user. */
namespace OBS::Accessibility::StreamDeck {

/* The label in white on black, as large as fits the key, or in gray when the
 * action is not available. An empty label gives a black key. */
QImage RenderLabel(const Model &model, const QString &label, bool available);

/* Turns an image the way the model shows it upside down or sideways. */
QImage ToDeviceOrientation(const Model &model, const QImage &image);

/* A 24 bit Windows bitmap, the format the Mini reads. */
std::vector<uint8_t> EncodeBmp(const QImage &image);

/* The image in the model's size, orientation and format, ready for
 * ImageReports. */
std::vector<uint8_t> EncodeKeyImage(const Model &model, const QImage &image);

} // namespace OBS::Accessibility::StreamDeck
