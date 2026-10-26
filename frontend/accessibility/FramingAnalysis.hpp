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

/* A 2D affine transform: x' = xx * x + yx * y + x0, y' = xy * x + yy * y + y0. */
struct Affine2D {
	double xx = 1.0;
	double xy = 0.0;
	double yx = 0.0;
	double yy = 1.0;
	double x0 = 0.0;
	double y0 = 0.0;
};

/* How a source is placed in a scene, as libobs computes it. */
struct ItemPlacement {
	double sourceWidth = 0.0;
	double sourceHeight = 0.0;
	/* The item's own crop, in source pixels. */
	double cropLeft = 0.0;
	double cropTop = 0.0;
	double cropRight = 0.0;
	double cropBottom = 0.0;
	/* From the pixels of the picture as drawn to the canvas. */
	Affine2D draw;
	double canvasWidth = 0.0;
	double canvasHeight = 0.0;
	/* Set when the item is cropped to its bounding box: libobs then also
	 * crops the picture to fit the box, on the side the box's alignment
	 * leaves out (0 keeps the left or top, 1 the right or bottom). */
	bool cropToBox = false;
	/* From the unit square to the bounding box on the canvas. */
	Affine2D box;
	double boxAlignX = 0.5;
	double boxAlignY = 0.5;
};

/* The part of the source's picture that is on the canvas, as a share of the
 * picture, or an empty box when none is. Rotations that are not a multiple
 * of 90 degrees give the bounding box of that part. */
FaceBox VisibleWindow(const ItemPlacement &placement);

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

/* Whether the face is in the part of the picture the scene shows. */
enum class FramingVisibility {
	Invalid = 0,
	Inside,
	PartlyOutside,
	Outside,
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
	FramingVisibility visibility = FramingVisibility::Inside;
	FramingLight light = FramingLight::Good;

	bool IsCentered() const { return horizontal == FramingMove::None && vertical == FramingMove::None; }
	bool operator==(const FramingAdvice &other) const;
	bool operator!=(const FramingAdvice &other) const { return !(*this == other); }
};

/* window is the part of the picture the advice is about, usually the part
 * the scene shows (see VisibleWindow), as a share of the picture. meanLuma
 * is the average brightness of the picture, from 0 to 255. Set mirrored when
 * the picture is flipped horizontally, as in a mirror: then a face on the
 * left of the picture is on the person's left too. */
FramingAdvice AnalyzeFraming(const std::vector<FaceBox> &faces, const FaceBox &window, double meanLuma,
			     bool mirrored);

/* The whole picture. */
constexpr FaceBox wholePicture = {0.0, 0.0, 1.0, 1.0};

/* The average brightness of a grayscale picture, from 0 to 255. */
double MeanLuma(const uint8_t *gray, int width, int height, int stride);

/* Converts RGBA pixels to grayscale with the BT.601 weights. */
void RgbaToGray(const uint8_t *rgba, int width, int height, int rgbaStride, uint8_t *gray);

} // namespace OBS::Accessibility
