#pragma once

#include <QString>

#include <cstdint>
#include <deque>

/* Formatting and arithmetic for the accessible readouts. Nothing here depends
 * on the running application, so the unit tests can cover it. */
namespace OBS::Accessibility {

/* Volumes at or below this level are treated as silence (-inf dB). */
constexpr float minimumVolumeDb = -96.0f;

/* "hh:mm:ss", as the status bar shows it. */
QString FormatDuration(int64_t seconds);

/* "-3.5 dB", or "-inf dB" for silence. */
QString FormatDb(float db);

/* "512.0 MB", "12.3 GB" and so on. */
QString FormatBytes(uint64_t bytes);

/* Moves a volume by step decibels and keeps it within maximumDb. Going down
 * from floorDb reaches silence, and going up from silence reaches floorDb. */
float StepVolumeDb(float db, float step, float floorDb, float maximumDb);

/* Tracks the share of bad frames (dropped, lagged or skipped) over a sliding
 * time window, from the running totals that libobs reports. */
class FrameLossWindow {
public:
	explicit FrameLossWindow(int64_t windowMs) : windowMs(windowMs) {}

	/* Adds the current totals. A total lower than the previous one means
	 * the counters were reset, and starts the window again. */
	void Add(int64_t timeMs, uint64_t totalFrames, uint64_t badFrames);
	void Clear();

	/* The share of bad frames since the oldest sample of the window, from
	 * 0 to 1, or 0 when the window holds too few frames to tell. */
	double Ratio(uint64_t minimumFrames = 1) const;

	/* Whether the samples cover the whole window. */
	bool IsFull() const;

private:
	struct Sample {
		int64_t timeMs;
		uint64_t totalFrames;
		uint64_t badFrames;
	};

	int64_t windowMs;
	std::deque<Sample> samples;
};

} // namespace OBS::Accessibility
