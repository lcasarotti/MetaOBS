/* Checks the Stream Deck reports MetaOBS reads and writes, and the positions
 * that name the keys. */

#include "a11y-test.hpp"

#include <accessibility/StreamDeckProtocol.hpp>

namespace StreamDeck = OBS::Accessibility::StreamDeck;

namespace {
const StreamDeck::Model &Mini()
{
	return *StreamDeck::FindModel(StreamDeck::elgatoVendorId, 0x0090);
}

const StreamDeck::Model &MK2()
{
	return *StreamDeck::FindModel(StreamDeck::elgatoVendorId, 0x0080);
}
} // namespace

A11Y_TEST(StreamDeckModelsAreFoundByTheirIds)
{
	const StreamDeck::Model *mini = StreamDeck::FindModel(0x0FD9, 0x0090);
	A11Y_CHECK(mini != nullptr);
	A11Y_CHECK(mini && mini->keyCount == 6 && mini->columns == 3 && mini->rows == 2);

	A11Y_CHECK(StreamDeck::FindModel(0x0FD9, 0x1234) == nullptr);
	/* Another vendor's device with a Stream Deck product ID. */
	A11Y_CHECK(StreamDeck::FindModel(0x046D, 0x0090) == nullptr);

	for (const StreamDeck::Model &model : StreamDeck::Models()) {
		A11Y_CHECK_MESSAGE(model.columns * model.rows == model.keyCount, model.name);
	}
}

A11Y_TEST(StreamDeckMiniKeyReportsAreRead)
{
	std::vector<uint8_t> report(17, 0);
	report[0] = 0x01;
	report[1 + 2] = 1;
	report[1 + 5] = 1;

	std::vector<bool> states = StreamDeck::ParseKeyStates(Mini(), report);
	A11Y_CHECK(states.size() == 6);
	A11Y_CHECK(states.size() == 6 && !states[0] && states[2] && states[5]);
}

A11Y_TEST(StreamDeckCurrentKeyReportsSkipTheirHeader)
{
	std::vector<uint8_t> report(512, 0);
	report[0] = 0x01;
	report[2] = 15;
	report[4 + 0] = 1;

	std::vector<bool> states = StreamDeck::ParseKeyStates(MK2(), report);
	A11Y_CHECK(states.size() == 15);
	A11Y_CHECK(states.size() == 15 && states[0] && !states[1]);
}

A11Y_TEST(StreamDeckOtherReportsAreIgnored)
{
	std::vector<uint8_t> wrongId(17, 0);
	wrongId[0] = 0x02;
	A11Y_CHECK(StreamDeck::ParseKeyStates(Mini(), wrongId).empty());

	std::vector<uint8_t> tooShort = {0x01, 0, 0};
	A11Y_CHECK(StreamDeck::ParseKeyStates(Mini(), tooShort).empty());
	A11Y_CHECK(StreamDeck::ParseKeyStates(Mini(), {}).empty());
}

A11Y_TEST(StreamDeckFeatureReportsMatchTheModel)
{
	std::vector<uint8_t> reset = StreamDeck::ResetReport(Mini());
	A11Y_CHECK(reset.size() == 17 && reset[0] == 0x0B && reset[1] == 0x63);

	std::vector<uint8_t> brightness = StreamDeck::BrightnessReport(Mini(), 150);
	A11Y_CHECK(brightness.size() == 17);
	A11Y_CHECK(brightness[0] == 0x05 && brightness[4] == 0x01 && brightness[5] == 100);

	std::vector<uint8_t> current = StreamDeck::BrightnessReport(MK2(), 40);
	A11Y_CHECK(current.size() == 32 && current[0] == 0x03 && current[1] == 0x08 && current[2] == 40);
}

A11Y_TEST(StreamDeckMiniImagesAreSplitIntoPages)
{
	/* An 80 by 80 BMP: 54 header bytes and three bytes per pixel. */
	std::vector<uint8_t> image(54 + 80 * 80 * 3);
	for (size_t i = 0; i < image.size(); i++) {
		image[i] = static_cast<uint8_t>(i % 251);
	}

	std::vector<std::vector<uint8_t>> reports = StreamDeck::ImageReports(Mini(), 4, image);
	const size_t payload = 1024 - 16;
	A11Y_CHECK(reports.size() == (image.size() + payload - 1) / payload);

	for (size_t page = 0; page < reports.size(); page++) {
		const std::vector<uint8_t> &report = reports[page];
		bool last = page + 1 == reports.size();
		A11Y_CHECK(report.size() == 1024);
		A11Y_CHECK(report[0] == 0x02 && report[1] == 0x01 && report[2] == page);
		A11Y_CHECK(report[4] == (last ? 1 : 0) && report[5] == 5);
		A11Y_CHECK(report[16] == image[page * payload]);
	}
}

A11Y_TEST(StreamDeckCurrentImagesCarryTheirLength)
{
	std::vector<uint8_t> image(1500, 0xAB);

	std::vector<std::vector<uint8_t>> reports = StreamDeck::ImageReports(MK2(), 0, image);
	A11Y_CHECK(reports.size() == 2);
	if (reports.size() != 2) {
		return;
	}

	A11Y_CHECK(reports[0][1] == 0x07 && reports[0][2] == 0 && reports[0][3] == 0);
	A11Y_CHECK((reports[0][4] | reports[0][5] << 8) == 1016);
	A11Y_CHECK(reports[1][3] == 1 && (reports[1][4] | reports[1][5] << 8) == 484 && reports[1][6] == 1);

	A11Y_CHECK(StreamDeck::ImageReports(MK2(), 15, image).empty());
}

A11Y_TEST(StreamDeckKeysAreNamedByPosition)
{
	using StreamDeck::Column;
	using StreamDeck::Row;

	StreamDeck::KeyPosition first = StreamDeck::PositionOf(Mini(), 0);
	A11Y_CHECK(first.row == 1 && first.column == 1);
	A11Y_CHECK(first.rowName == Row::Top && first.columnName == Column::Left);

	StreamDeck::KeyPosition second = StreamDeck::PositionOf(Mini(), 1);
	A11Y_CHECK(second.rowName == Row::Top && second.columnName == Column::Center);

	StreamDeck::KeyPosition last = StreamDeck::PositionOf(Mini(), 5);
	A11Y_CHECK(last.row == 2 && last.column == 3);
	A11Y_CHECK(last.rowName == Row::Bottom && last.columnName == Column::Right);

	/* The 15 key model has five columns: numbers only. */
	StreamDeck::KeyPosition wide = StreamDeck::PositionOf(MK2(), 7);
	A11Y_CHECK(wide.row == 2 && wide.column == 3);
	A11Y_CHECK(wide.rowName == Row::Invalid && wide.columnName == Column::Invalid);

	A11Y_CHECK(StreamDeck::PositionOf(Mini(), 6).row == 0);
}
