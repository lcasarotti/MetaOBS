#pragma once

#include <cstdint>
#include <vector>

/* What MetaOBS needs to know to talk to an Elgato Stream Deck over USB HID,
 * kept free of any I/O so it can be tested. The reports follow the ones
 * documented by the python-elgato-streamdeck project. */
namespace OBS::Accessibility::StreamDeck {

constexpr uint16_t elgatoVendorId = 0x0FD9;

/* The two report layouts Stream Decks use. The Mini keeps the layout of the
 * first Stream Deck; the 15 and 32 key models moved to a newer one. */
enum class Generation {
	Invalid = 0,
	Mini = 1,
	Current = 2,
};

enum class ImageFormat {
	Invalid = 0,
	Bmp = 1,
	Jpeg = 2,
};

struct Model {
	uint16_t productId;
	const char *name;
	Generation generation;
	int keyCount;
	int columns;
	int rows;
	int imageSize;
	ImageFormat imageFormat;
	/* Clockwise rotation, in degrees, applied to an image before it is sent. */
	int imageRotation;
	bool flipHorizontally;
	bool flipVertically;
};

/* The models whose keys MetaOBS can read, or nullptr. */
const Model *FindModel(uint16_t vendorId, uint16_t productId);
const std::vector<Model> &Models();

/* The pressed state of each key in an input report, which starts with its
 * report ID. Returns an empty list when the report is not a key report or is
 * too short. */
std::vector<bool> ParseKeyStates(const Model &model, const std::vector<uint8_t> &report);

/* Feature reports. Each starts with its report ID and has the length the
 * model expects. */
std::vector<uint8_t> ResetReport(const Model &model);
std::vector<uint8_t> BrightnessReport(const Model &model, int percent);

/* Output reports that, written in order, show an encoded image on a key. */
std::vector<std::vector<uint8_t>> ImageReports(const Model &model, int key, const std::vector<uint8_t> &image);

/* Where a key is on the device, for its spoken name. Grids too large for words
 * like "top left" use the row and column numbers instead. */
enum class Row {
	Invalid = 0,
	Top = 1,
	Middle = 2,
	Bottom = 3,
};

enum class Column {
	Invalid = 0,
	Left = 1,
	Center = 2,
	Right = 3,
};

struct KeyPosition {
	/* Counted from 1. */
	int row;
	int column;
	/* Invalid when the grid has more than three rows or columns. */
	Row rowName;
	Column columnName;
};

KeyPosition PositionOf(const Model &model, int key);

} // namespace OBS::Accessibility::StreamDeck
