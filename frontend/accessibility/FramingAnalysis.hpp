#pragma once

#include <cstdint>
#include <vector>

/* Turns the faces found in a picture of a camera into advice on the framing:
 * where to move, how far from the camera to sit and how bright the picture
 * is. Nothing here depends on the running application, so the unit tests can
 * cover it. */
namespace OBS::Accessibility {

/* A face, as a share of the picture's width and height (0 to 1). */
struct FaceBox {
	double left = 0.0;
	double top = 0.0;
	double width = 0.0;
	double height = 0.0;
};

/* Where the person should move, seen from the person in front of the camera,
 * or where the face is too high or too low. */
enum class FramingMove {
	Invalid = 0,
	None,
	Left,
	Right,
	FaceTooHigh,
	FaceTooLow,
};

enum class FramingAmount {
	Invalid = 0,
	Slight,
	Large,
};

enum class FramingDistance {
	Invalid = 0,
	Good,
	TooFar,
	TooClose,
};

enum class FramingLight {
	Invalid = 0,
	Good,
	Black,
	VeryDark,
	Dark,
	VeryBright,
};

struct FramingAdvice {
	int faceCount = 0;
	/* The rest of the advice is about the largest face, the one closest to
	 * the camera, and is only meaningful when faceCount is not 0. */
	FramingMove horizontal = FramingMove::None;
	FramingAmount horizontalAmount = FramingAmount::Slight;
	FramingMove vertical = FramingMove::None;
	FramingAmount verticalAmount = FramingAmount::Slight;
	FramingDistance distance = FramingDistance::Good;
	FramingLight light = FramingLight::Good;

	bool IsCentered() const { return horizontal == FramingMove::None && vertical == FramingMove::None; }
	bool operator==(const FramingAdvice &other) const;
	bool operator!=(const FramingAdvice &other) const { return !(*this == other); }
};

/* meanLuma is the average brightness of the picture, from 0 to 255. Set
 * mirrored when the picture is flipped horizontally, as in a mirror: then a
 * face on the left of the picture is on the person's left too. */
FramingAdvice AnalyzeFraming(const std::vector<FaceBox> &faces, double meanLuma, bool mirrored);

/* The average brightness of a grayscale picture, from 0 to 255. */
double MeanLuma(const uint8_t *gray, int width, int height, int stride);

/* Converts RGBA pixels to grayscale with the BT.601 weights. */
void RgbaToGray(const uint8_t *rgba, int width, int height, int rgbaStride, uint8_t *gray);

} // namespace OBS::Accessibility
