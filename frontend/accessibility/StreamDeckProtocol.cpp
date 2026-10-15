#include "StreamDeckProtocol.hpp"

#include <algorithm>
#include <cstddef>
#include <iterator>

namespace OBS::Accessibility::StreamDeck {

namespace {
constexpr uint8_t keyReportId = 0x01;
constexpr uint8_t imageReportId = 0x02;
constexpr size_t imageReportLength = 1024;

constexpr size_t miniFeatureLength = 17;
constexpr size_t miniKeyOffset = 1;
constexpr size_t miniImageHeaderLength = 16;

constexpr size_t currentFeatureLength = 32;
constexpr size_t currentKeyOffset = 4;
constexpr size_t currentImageHeaderLength = 8;

size_t FeatureLength(const Model &model)
{
	return model.generation == Generation::Mini ? miniFeatureLength : currentFeatureLength;
}

size_t ImageHeaderLength(const Model &model)
{
	return model.generation == Generation::Mini ? miniImageHeaderLength : currentImageHeaderLength;
}

Row RowName(int row, int rows)
{
	if (rows == 1) {
		return Row::Middle;
	}
	if (rows > 3) {
		return Row::Invalid;
	}
	if (row == 1) {
		return Row::Top;
	}
	return row == rows ? Row::Bottom : Row::Middle;
}

Column ColumnName(int column, int columns)
{
	if (columns == 1) {
		return Column::Center;
	}
	if (columns > 3) {
		return Column::Invalid;
	}
	if (column == 1) {
		return Column::Left;
	}
	return column == columns ? Column::Right : Column::Center;
}
} // namespace

const std::vector<Model> &Models()
{
	static const std::vector<Model> models = {
		{0x0063, "Stream Deck Mini", Generation::Mini, 6, 3, 2, 80, ImageFormat::Bmp, 90, false, true},
		{0x00B3, "Stream Deck Mini", Generation::Mini, 6, 3, 2, 80, ImageFormat::Bmp, 90, false, true},
		{0x0090, "Stream Deck Mini MK.2", Generation::Mini, 6, 3, 2, 80, ImageFormat::Bmp, 90, false, true},
		{0x00B8, "Stream Deck Mini MK.2", Generation::Mini, 6, 3, 2, 80, ImageFormat::Bmp, 90, false, true},
		{0x006D, "Stream Deck", Generation::Current, 15, 5, 3, 72, ImageFormat::Jpeg, 0, true, true},
		{0x0080, "Stream Deck MK.2", Generation::Current, 15, 5, 3, 72, ImageFormat::Jpeg, 0, true, true},
		{0x00A5, "Stream Deck MK.2", Generation::Current, 15, 5, 3, 72, ImageFormat::Jpeg, 0, true, true},
		{0x00B9, "Stream Deck MK.2", Generation::Current, 15, 5, 3, 72, ImageFormat::Jpeg, 0, true, true},
		{0x006C, "Stream Deck XL", Generation::Current, 32, 8, 4, 96, ImageFormat::Jpeg, 0, true, true},
		{0x008F, "Stream Deck XL", Generation::Current, 32, 8, 4, 96, ImageFormat::Jpeg, 0, true, true},
		{0x00BA, "Stream Deck XL", Generation::Current, 32, 8, 4, 96, ImageFormat::Jpeg, 0, true, true},
	};
	return models;
}

const Model *FindModel(uint16_t vendorId, uint16_t productId)
{
	if (vendorId != elgatoVendorId) {
		return nullptr;
	}

	for (const Model &model : Models()) {
		if (model.productId == productId) {
			return &model;
		}
	}
	return nullptr;
}

std::vector<bool> ParseKeyStates(const Model &model, const std::vector<uint8_t> &report)
{
	size_t offset = model.generation == Generation::Mini ? miniKeyOffset : currentKeyOffset;
	size_t keyCount = static_cast<size_t>(model.keyCount);

	if (report.empty() || report[0] != keyReportId || report.size() < offset + keyCount) {
		return {};
	}

	std::vector<bool> states;
	states.reserve(keyCount);
	for (size_t i = 0; i < keyCount; i++) {
		states.push_back(report[offset + i] != 0);
	}
	return states;
}

std::vector<uint8_t> ResetReport(const Model &model)
{
	std::vector<uint8_t> report(FeatureLength(model), 0);
	if (model.generation == Generation::Mini) {
		report[0] = 0x0B;
		report[1] = 0x63;
	} else {
		report[0] = 0x03;
		report[1] = 0x02;
	}
	return report;
}

std::vector<uint8_t> BrightnessReport(const Model &model, int percent)
{
	uint8_t value = static_cast<uint8_t>(std::clamp(percent, 0, 100));

	std::vector<uint8_t> report(FeatureLength(model), 0);
	if (model.generation == Generation::Mini) {
		const uint8_t prefix[] = {0x05, 0x55, 0xAA, 0xD1, 0x01};
		std::copy(std::begin(prefix), std::end(prefix), report.begin());
		report[5] = value;
	} else {
		report[0] = 0x03;
		report[1] = 0x08;
		report[2] = value;
	}
	return report;
}

std::vector<std::vector<uint8_t>> ImageReports(const Model &model, int key, const std::vector<uint8_t> &image)
{
	std::vector<std::vector<uint8_t>> reports;
	if (key < 0 || key >= model.keyCount) {
		return reports;
	}

	size_t headerLength = ImageHeaderLength(model);
	size_t payloadLength = imageReportLength - headerLength;
	size_t sent = 0;
	int page = 0;

	do {
		size_t length = std::min(image.size() - sent, payloadLength);
		bool last = sent + length == image.size();

		std::vector<uint8_t> report(imageReportLength, 0);
		report[0] = imageReportId;
		if (model.generation == Generation::Mini) {
			report[1] = 0x01;
			report[2] = static_cast<uint8_t>(page);
			report[4] = last ? 1 : 0;
			report[5] = static_cast<uint8_t>(key + 1);
		} else {
			report[1] = 0x07;
			report[2] = static_cast<uint8_t>(key);
			report[3] = last ? 1 : 0;
			report[4] = static_cast<uint8_t>(length & 0xFF);
			report[5] = static_cast<uint8_t>(length >> 8);
			report[6] = static_cast<uint8_t>(page & 0xFF);
			report[7] = static_cast<uint8_t>(page >> 8);
		}
		std::copy_n(image.begin() + static_cast<ptrdiff_t>(sent), length,
			    report.begin() + static_cast<ptrdiff_t>(headerLength));

		reports.push_back(std::move(report));
		sent += length;
		page++;
	} while (sent < image.size());

	return reports;
}

KeyPosition PositionOf(const Model &model, int key)
{
	KeyPosition position = {};
	if (key < 0 || key >= model.keyCount) {
		return position;
	}

	position.row = key / model.columns + 1;
	position.column = key % model.columns + 1;

	bool named = model.rows <= 3 && model.columns <= 3;
	position.rowName = named ? RowName(position.row, model.rows) : Row::Invalid;
	position.columnName = named ? ColumnName(position.column, model.columns) : Column::Invalid;
	return position;
}

} // namespace OBS::Accessibility::StreamDeck
