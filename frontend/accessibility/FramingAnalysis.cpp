#include "FramingAnalysis.hpp"

#include <algorithm>
#include <cmath>

namespace OBS::Accessibility {

namespace {
/* How far the center of the face may be from the target, as a share of the
 * picture, before the person is told to move, and from where the move is a
 * large one. */
constexpr double centeredTolerance = 0.08;
constexpr double largeOffset = 0.2;

/* Faces look best a little above the middle, with less room over the head
 * than under the chin. */
constexpr double verticalTarget = 0.45;
constexpr double verticalTolerance = 0.1;
constexpr double largeVerticalOffset = 0.22;

/* The height of the face as a share of the picture. A face a meter or two
 * from a usual webcam fills about a third of it. */
constexpr double tooFarHeight = 0.15;
constexpr double tooCloseHeight = 0.6;

constexpr double blackLuma = 8.0;
constexpr double veryDarkLuma = 45.0;
constexpr double darkLuma = 75.0;
constexpr double veryBrightLuma = 215.0;

FramingLight LightFor(double meanLuma)
{
	if (meanLuma < blackLuma) {
		return FramingLight::Black;
	}
	if (meanLuma < veryDarkLuma) {
		return FramingLight::VeryDark;
	}
	if (meanLuma < darkLuma) {
		return FramingLight::Dark;
	}
	if (meanLuma > veryBrightLuma) {
		return FramingLight::VeryBright;
	}
	return FramingLight::Good;
}

FramingAmount AmountFor(double offset, double large)
{
	return std::fabs(offset) > large ? FramingAmount::Large : FramingAmount::Slight;
}
} // namespace

bool FramingAdvice::operator==(const FramingAdvice &other) const
{
	return faceCount == other.faceCount && horizontal == other.horizontal &&
	       horizontalAmount == other.horizontalAmount && vertical == other.vertical &&
	       verticalAmount == other.verticalAmount && distance == other.distance && light == other.light;
}

FramingAdvice AnalyzeFraming(const std::vector<FaceBox> &faces, double meanLuma, bool mirrored)
{
	FramingAdvice advice;
	advice.faceCount = (int)faces.size();
	advice.light = LightFor(meanLuma);

	if (faces.empty()) {
		return advice;
	}

	const FaceBox &face = *std::max_element(faces.begin(), faces.end(), [](const FaceBox &a, const FaceBox &b) {
		return a.width * a.height < b.width * b.height;
	});

	/* The camera faces the person, so the left of the picture is the
	 * person's right, unless the picture is mirrored. A face on the left of
	 * the picture is then on the person's right of the camera's axis, and
	 * the person gets back to the middle by moving to their left. */
	double offsetX = face.left + face.width / 2.0 - 0.5;
	if (std::fabs(offsetX) > centeredTolerance) {
		bool faceOnPictureLeft = offsetX < 0.0;
		advice.horizontal = faceOnPictureLeft != mirrored ? FramingMove::Left : FramingMove::Right;
		advice.horizontalAmount = AmountFor(offsetX, largeOffset);
	}

	double offsetY = face.top + face.height / 2.0 - verticalTarget;
	if (std::fabs(offsetY) > verticalTolerance) {
		advice.vertical = offsetY < 0.0 ? FramingMove::FaceTooHigh : FramingMove::FaceTooLow;
		advice.verticalAmount = AmountFor(offsetY, largeVerticalOffset);
	}

	if (face.height < tooFarHeight) {
		advice.distance = FramingDistance::TooFar;
	} else if (face.height > tooCloseHeight) {
		advice.distance = FramingDistance::TooClose;
	}

	return advice;
}

double MeanLuma(const uint8_t *gray, int width, int height, int stride)
{
	if (width <= 0 || height <= 0) {
		return 0.0;
	}

	uint64_t sum = 0;
	for (int y = 0; y < height; y++) {
		const uint8_t *line = gray + (size_t)y * stride;
		for (int x = 0; x < width; x++) {
			sum += line[x];
		}
	}

	return (double)sum / ((double)width * (double)height);
}

void RgbaToGray(const uint8_t *rgba, int width, int height, int rgbaStride, uint8_t *gray)
{
	for (int y = 0; y < height; y++) {
		const uint8_t *line = rgba + (size_t)y * rgbaStride;
		uint8_t *out = gray + (size_t)y * width;
		for (int x = 0; x < width; x++) {
			const uint8_t *pixel = line + x * 4;
			out[x] = (uint8_t)((77 * pixel[0] + 150 * pixel[1] + 29 * pixel[2]) >> 8);
		}
	}
}

} // namespace OBS::Accessibility
