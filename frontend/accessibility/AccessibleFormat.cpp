#include "AccessibleFormat.hpp"

#include <algorithm>
#include <iterator>

namespace OBS::Accessibility {

QString FormatDuration(int64_t seconds)
{
	seconds = std::max<int64_t>(seconds, 0);

	int64_t hours = seconds / 3600;
	int64_t minutes = seconds / 60 % 60;
	int64_t remainder = seconds % 60;

	return QString("%1:%2:%3")
		.arg(hours, 2, 10, QChar('0'))
		.arg(minutes, 2, 10, QChar('0'))
		.arg(remainder, 2, 10, QChar('0'));
}

QString FormatDb(float db)
{
	if (db <= minimumVolumeDb) {
		return QStringLiteral("-inf dB");
	}

	/* Avoid "-0.0 dB" for values that round to zero. */
	if (db > -0.05f && db < 0.05f) {
		db = 0.0f;
	}

	return QString::number(db, 'f', 1) + QStringLiteral(" dB");
}

QString FormatBytes(uint64_t bytes)
{
	constexpr double kilobyte = 1024.0;
	constexpr const char *units[] = {"KB", "MB", "GB", "TB"};

	double value = static_cast<double>(bytes) / kilobyte;
	int unit = 0;
	while (value >= kilobyte && unit < static_cast<int>(std::size(units)) - 1) {
		value /= kilobyte;
		unit++;
	}

	return QString::number(value, 'f', 1) + " " + units[unit];
}

float StepVolumeDb(float db, float step, float floorDb, float maximumDb)
{
	if (db <= minimumVolumeDb) {
		return step > 0.0f ? std::min(floorDb, maximumDb) : minimumVolumeDb;
	}

	float next = db + step;
	if (step < 0.0f) {
		return next < floorDb ? minimumVolumeDb : next;
	}

	/* A volume already above the maximum, set elsewhere, stays where it
	 * is instead of jumping down. */
	return std::max(db, std::min(next, maximumDb));
}

void FrameLossWindow::Add(int64_t timeMs, uint64_t totalFrames, uint64_t badFrames)
{
	if (!samples.empty() &&
	    (totalFrames < samples.back().totalFrames || badFrames < samples.back().badFrames)) {
		samples.clear();
	}

	samples.push_back({timeMs, totalFrames, badFrames});

	/* Keep one sample older than the window, so the window is covered
	 * from start to end. */
	while (samples.size() > 2 && timeMs - samples[1].timeMs >= windowMs) {
		samples.pop_front();
	}
}

void FrameLossWindow::Clear()
{
	samples.clear();
}

double FrameLossWindow::Ratio(uint64_t minimumFrames) const
{
	if (samples.size() < 2) {
		return 0.0;
	}

	uint64_t frames = samples.back().totalFrames - samples.front().totalFrames;
	uint64_t badFrames = samples.back().badFrames - samples.front().badFrames;

	if (frames < std::max<uint64_t>(minimumFrames, 1)) {
		return 0.0;
	}

	return std::min(1.0, static_cast<double>(badFrames) / static_cast<double>(frames));
}

bool FrameLossWindow::IsFull() const
{
	return samples.size() >= 2 && samples.back().timeMs - samples.front().timeMs >= windowMs;
}

} // namespace OBS::Accessibility
