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

/* A face counts as partly outside the window when this share of it is. */
constexpr double partlyOutsideShare = 0.15;

struct Point {
	double x = 0.0;
	double y = 0.0;
};

/* An axis-aligned rectangle. */
struct Rect {
	double left = 0.0;
	double top = 0.0;
	double right = 0.0;
	double bottom = 0.0;

	bool IsEmpty() const { return right <= left || bottom <= top; }
};

Point Map(const Affine2D &t, Point p)
{
	return {t.xx * p.x + t.yx * p.y + t.x0, t.xy * p.x + t.yy * p.y + t.y0};
}

bool Invert(const Affine2D &t, Affine2D &inverse)
{
	double determinant = t.xx * t.yy - t.yx * t.xy;
	if (std::fabs(determinant) < 1e-12) {
		return false;
	}

	inverse.xx = t.yy / determinant;
	inverse.yx = -t.yx / determinant;
	inverse.xy = -t.xy / determinant;
	inverse.yy = t.xx / determinant;
	inverse.x0 = -(inverse.xx * t.x0 + inverse.yx * t.y0);
	inverse.y0 = -(inverse.xy * t.x0 + inverse.yy * t.y0);
	return true;
}

/* The bounding box of a rectangle after the transform. */
Rect MapRect(const Affine2D &t, const Rect &rect)
{
	Rect mapped = {HUGE_VAL, HUGE_VAL, -HUGE_VAL, -HUGE_VAL};
	for (double x : {rect.left, rect.right}) {
		for (double y : {rect.top, rect.bottom}) {
			Point p = Map(t, {x, y});
			mapped.left = std::min(mapped.left, p.x);
			mapped.top = std::min(mapped.top, p.y);
			mapped.right = std::max(mapped.right, p.x);
			mapped.bottom = std::max(mapped.bottom, p.y);
		}
	}
	return mapped;
}

Rect Intersect(const Rect &a, const Rect &b)
{
	return {std::max(a.left, b.left), std::max(a.top, b.top), std::min(a.right, b.right),
		std::min(a.bottom, b.bottom)};
}

/* The share of the face outside the window. */
double OutsideShare(const FaceBox &face, const FaceBox &window)
{
	double area = face.width * face.height;
	if (area <= 0.0) {
		return 0.0;
	}

	Rect inside = Intersect({face.left, face.top, face.left + face.width, face.top + face.height},
				{window.left, window.top, window.left + window.width, window.top + window.height});
	double insideArea = inside.IsEmpty() ? 0.0 : (inside.right - inside.left) * (inside.bottom - inside.top);
	return 1.0 - insideArea / area;
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
	       verticalAmount == other.verticalAmount && distance == other.distance &&
	       visibility == other.visibility && light == other.light;
}

FaceBox VisibleWindow(const ItemPlacement &placement)
{
	double width = placement.sourceWidth - placement.cropLeft - placement.cropRight;
	double height = placement.sourceHeight - placement.cropTop - placement.cropBottom;
	Affine2D toPicture;
	if (placement.sourceWidth <= 0.0 || placement.sourceHeight <= 0.0 || width <= 0.0 || height <= 0.0 ||
	    !Invert(placement.draw, toPicture)) {
		return {};
	}

	/* The picture as drawn starts after the crop, and after the part
	 * cropped to fit the bounding box. */
	Rect drawn = {0.0, 0.0, width, height};
	double offsetX = placement.cropLeft;
	double offsetY = placement.cropTop;

	if (placement.cropToBox) {
		Rect box = MapRect(toPicture, MapRect(placement.box, {0.0, 0.0, 1.0, 1.0}));
		double overdrawX = std::max(0.0, width - (box.right - box.left));
		double overdrawY = std::max(0.0, height - (box.bottom - box.top));

		/* A flipped picture is cropped on the other side. */
		double alignX = placement.draw.xx < 0.0 ? 1.0 - placement.boxAlignX : placement.boxAlignX;
		double alignY = placement.draw.yy < 0.0 ? 1.0 - placement.boxAlignY : placement.boxAlignY;
		offsetX += overdrawX * alignX;
		offsetY += overdrawY * alignY;
		drawn.right -= overdrawX;
		drawn.bottom -= overdrawY;
	}

	Rect canvas = MapRect(toPicture, {0.0, 0.0, placement.canvasWidth, placement.canvasHeight});
	Rect visible = Intersect(drawn, canvas);
	if (visible.IsEmpty()) {
		return {};
	}

	return {(visible.left + offsetX) / placement.sourceWidth, (visible.top + offsetY) / placement.sourceHeight,
		(visible.right - visible.left) / placement.sourceWidth,
		(visible.bottom - visible.top) / placement.sourceHeight};
}

FramingAdvice AnalyzeFraming(const std::vector<FaceBox> &faces, const FaceBox &window, double meanLuma,
			     bool mirrored)
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

	if (window.width <= 0.0 || window.height <= 0.0) {
		return advice;
	}

	double outside = OutsideShare(face, window);
	if (outside >= 1.0 - 1e-9) {
		advice.visibility = FramingVisibility::Outside;
	} else if (outside > partlyOutsideShare) {
		advice.visibility = FramingVisibility::PartlyOutside;
	}

	/* Positions and sizes as a share of the window. */
	/* The camera faces the person, so the left of the picture is the
	 * person's right, unless the picture is mirrored. A face on the left of
	 * the picture is then on the person's right of the camera's axis, and
	 * the person gets back to the middle by moving to their left. */
	double offsetX = (face.left + face.width / 2.0 - window.left) / window.width - 0.5;
	if (std::fabs(offsetX) > centeredTolerance) {
		bool faceOnPictureLeft = offsetX < 0.0;
		advice.horizontal = faceOnPictureLeft != mirrored ? FramingMove::Left : FramingMove::Right;
		advice.horizontalAmount = AmountFor(offsetX, largeOffset);
	}

	double offsetY = (face.top + face.height / 2.0 - window.top) / window.height - verticalTarget;
	if (std::fabs(offsetY) > verticalTolerance) {
		advice.vertical = offsetY < 0.0 ? FramingMove::FaceTooHigh : FramingMove::FaceTooLow;
		advice.verticalAmount = AmountFor(offsetY, largeVerticalOffset);
	}

	double faceHeight = face.height / window.height;
	if (faceHeight < tooFarHeight) {
		advice.distance = FramingDistance::TooFar;
	} else if (faceHeight > tooCloseHeight) {
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
