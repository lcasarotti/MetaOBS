/* Checks the formatting and arithmetic behind the accessible readouts: the
 * times, volumes and sizes that are spoken, and the lost frames warnings. */

#include "a11y-test.hpp"

#include <accessibility/AccessibleFormat.hpp>

#include <cmath>

using OBS::Accessibility::FormatBytes;
using OBS::Accessibility::FormatDb;
using OBS::Accessibility::FormatDuration;
using OBS::Accessibility::FrameLossWindow;
using OBS::Accessibility::minimumVolumeDb;
using OBS::Accessibility::StepVolumeDb;

namespace {
bool Near(double a, double b)
{
	return std::fabs(a - b) < 0.0001;
}
} // namespace

A11Y_TEST(FormatDurationPadsEachField)
{
	A11Y_CHECK(FormatDuration(0) == "00:00:00");
	A11Y_CHECK(FormatDuration(59) == "00:00:59");
	A11Y_CHECK(FormatDuration(3725) == "01:02:05");
	A11Y_CHECK(FormatDuration(100 * 3600) == "100:00:00");
	A11Y_CHECK(FormatDuration(-5) == "00:00:00");
}

A11Y_TEST(FormatDbRoundsAndShowsSilence)
{
	A11Y_CHECK(FormatDb(-3.04f) == "-3.0 dB");
	A11Y_CHECK(FormatDb(-0.01f) == "0.0 dB");
	A11Y_CHECK(FormatDb(6.0f) == "6.0 dB");
	A11Y_CHECK(FormatDb(minimumVolumeDb) == "-inf dB");
	A11Y_CHECK(FormatDb(-200.0f) == "-inf dB");
}

A11Y_TEST(FormatBytesPicksTheUnit)
{
	A11Y_CHECK(FormatBytes(512ULL * 1024) == "512.0 KB");
	A11Y_CHECK(FormatBytes(1536ULL * 1024 * 1024) == "1.5 GB");
	A11Y_CHECK(FormatBytes(3ULL * 1024 * 1024 * 1024 * 1024) == "3.0 TB");
}

A11Y_TEST(FormatStepVolumeStaysInRange)
{
	/* Normal steps, capped at the maximum. */
	A11Y_CHECK(Near(StepVolumeDb(-10.0f, 1.0f, -60.0f, 0.0f), -9.0f));
	A11Y_CHECK(Near(StepVolumeDb(-0.5f, 1.0f, -60.0f, 0.0f), 0.0f));
	A11Y_CHECK(Near(StepVolumeDb(0.0f, 5.0f, -60.0f, 0.0f), 0.0f));

	/* Below the floor, the next step down is silence, and the next step up
	 * from silence is the floor. */
	A11Y_CHECK(StepVolumeDb(-59.5f, -1.0f, -60.0f, 0.0f) == minimumVolumeDb);
	A11Y_CHECK(StepVolumeDb(minimumVolumeDb, -1.0f, -60.0f, 0.0f) == minimumVolumeDb);
	A11Y_CHECK(Near(StepVolumeDb(minimumVolumeDb, 1.0f, -60.0f, 0.0f), -60.0f));

	/* A volume above the maximum, set elsewhere, goes down by one step and
	 * does not go up. */
	A11Y_CHECK(Near(StepVolumeDb(10.0f, -1.0f, -60.0f, 0.0f), 9.0f));
	A11Y_CHECK(Near(StepVolumeDb(10.0f, 1.0f, -60.0f, 0.0f), 10.0f));
}

A11Y_TEST(FormatFrameLossWindowMeasuresTheWindow)
{
	FrameLossWindow window(10000);

	/* 60 frames per second with no loss for 10 seconds. */
	for (int second = 0; second <= 10; second++) {
		window.Add(second * 1000, second * 60, 0);
	}
	A11Y_CHECK(window.IsFull());
	A11Y_CHECK(Near(window.Ratio(), 0.0));

	/* Then 6 of the next 60 frames are lost: 10% of the last second, but
	 * only 1% of the 10 second window. */
	window.Add(11000, 660, 6);
	A11Y_CHECK(Near(window.Ratio(), 6.0 / 600.0));

	/* Loss in every second of the window. */
	for (int second = 12; second <= 21; second++) {
		window.Add(second * 1000, second * 60, 6 + (second - 11) * 6);
	}
	A11Y_CHECK(Near(window.Ratio(), 0.1));
}

A11Y_TEST(FormatFrameLossWindowHandlesResetsAndFewFrames)
{
	FrameLossWindow window(10000);
	A11Y_CHECK(!window.IsFull());
	A11Y_CHECK(Near(window.Ratio(), 0.0));

	window.Add(0, 1000, 100);
	window.Add(1000, 1060, 160);
	A11Y_CHECK(!window.IsFull());
	A11Y_CHECK(Near(window.Ratio(), 1.0));
	A11Y_CHECK(Near(window.Ratio(100), 0.0));

	/* Counters going back mean a new output: start again. */
	window.Add(2000, 10, 0);
	A11Y_CHECK(Near(window.Ratio(), 0.0));
	window.Add(3000, 70, 3);
	A11Y_CHECK(Near(window.Ratio(), 3.0 / 60.0));
}
