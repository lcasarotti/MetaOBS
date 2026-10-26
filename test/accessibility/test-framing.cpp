/* Checks the advice on the framing given from the faces found in a camera's
 * picture: which way to move, how far from the camera to sit and how bright
 * the picture is. */

#include "a11y-test.hpp"

#include <accessibility/FramingAnalysis.hpp>

#include <cmath>
#include <vector>

using OBS::Accessibility::Affine2D;
using OBS::Accessibility::AnalyzeFraming;
using OBS::Accessibility::FaceBox;
using OBS::Accessibility::FramingAdvice;
using OBS::Accessibility::FramingAmount;
using OBS::Accessibility::FramingDistance;
using OBS::Accessibility::FramingLight;
using OBS::Accessibility::FramingMove;
using OBS::Accessibility::FramingVisibility;
using OBS::Accessibility::ItemPlacement;
using OBS::Accessibility::MeanLuma;
using OBS::Accessibility::RgbaToGray;
using OBS::Accessibility::VisibleWindow;
using OBS::Accessibility::wholePicture;

namespace {
constexpr double goodLuma = 128.0;

/* A face of the given size whose center is at (centerX, centerY). */
FaceBox Face(double centerX, double centerY, double width = 0.2, double height = 0.35)
{
	return {centerX - width / 2.0, centerY - height / 2.0, width, height};
}

bool Near(double a, double b)
{
	return std::fabs(a - b) < 0.0001;
}

bool SameBox(const FaceBox &a, const FaceBox &b)
{
	return Near(a.left, b.left) && Near(a.top, b.top) && Near(a.width, b.width) && Near(a.height, b.height);
}

Affine2D Scale(double x, double y, double left = 0.0, double top = 0.0)
{
	Affine2D transform;
	transform.xx = x;
	transform.yy = y;
	transform.x0 = left;
	transform.y0 = top;
	return transform;
}

/* A 1280x720 camera on a 1920x1080 canvas. */
ItemPlacement Camera(const Affine2D &draw)
{
	ItemPlacement placement;
	placement.sourceWidth = 1280.0;
	placement.sourceHeight = 720.0;
	placement.canvasWidth = 1920.0;
	placement.canvasHeight = 1080.0;
	placement.draw = draw;
	return placement;
}
} // namespace

A11Y_TEST(FramingCenteredFace)
{
	FramingAdvice advice = AnalyzeFraming({Face(0.5, 0.45)}, wholePicture, goodLuma, false);
	A11Y_CHECK(advice.faceCount == 1);
	A11Y_CHECK(advice.IsCentered());
	A11Y_CHECK(advice.distance == FramingDistance::Good);
	A11Y_CHECK(advice.light == FramingLight::Good);

	/* Within the tolerance. */
	A11Y_CHECK(AnalyzeFraming({Face(0.56, 0.52)}, wholePicture, goodLuma, false).IsCentered());
}

A11Y_TEST(FramingMoveIsSeenFromThePerson)
{
	/* A face on the left of a camera's picture is on the person's right
	 * of the camera's axis: they move to their left. */
	FramingAdvice advice = AnalyzeFraming({Face(0.35, 0.45)}, wholePicture, goodLuma, false);
	A11Y_CHECK(advice.horizontal == FramingMove::Left);
	A11Y_CHECK(advice.horizontalAmount == FramingAmount::Slight);
	A11Y_CHECK(advice.vertical == FramingMove::None);

	advice = AnalyzeFraming({Face(0.85, 0.45)}, wholePicture, goodLuma, false);
	A11Y_CHECK(advice.horizontal == FramingMove::Right);
	A11Y_CHECK(advice.horizontalAmount == FramingAmount::Large);

	/* A mirrored picture shows the person's left on the left. */
	A11Y_CHECK(AnalyzeFraming({Face(0.35, 0.45)}, wholePicture, goodLuma, true).horizontal == FramingMove::Right);
}

A11Y_TEST(FramingVerticalPosition)
{
	FramingAdvice advice = AnalyzeFraming({Face(0.5, 0.2)}, wholePicture, goodLuma, false);
	A11Y_CHECK(advice.vertical == FramingMove::FaceTooHigh);
	A11Y_CHECK(advice.verticalAmount == FramingAmount::Large);

	advice = AnalyzeFraming({Face(0.5, 0.6)}, wholePicture, goodLuma, false);
	A11Y_CHECK(advice.vertical == FramingMove::FaceTooLow);
	A11Y_CHECK(advice.verticalAmount == FramingAmount::Slight);
}

A11Y_TEST(FramingDistanceFollowsFaceHeight)
{
	A11Y_CHECK(AnalyzeFraming({Face(0.5, 0.45, 0.06, 0.1)}, wholePicture, goodLuma, false).distance ==
		   FramingDistance::TooFar);
	A11Y_CHECK(AnalyzeFraming({Face(0.5, 0.45, 0.4, 0.7)}, wholePicture, goodLuma, false).distance ==
		   FramingDistance::TooClose);
}

A11Y_TEST(FramingUsesTheLargestFace)
{
	std::vector<FaceBox> faces = {Face(0.1, 0.45, 0.05, 0.08), Face(0.5, 0.45), Face(0.9, 0.45, 0.05, 0.08)};
	FramingAdvice advice = AnalyzeFraming(faces, wholePicture, goodLuma, false);
	A11Y_CHECK(advice.faceCount == 3);
	A11Y_CHECK(advice.IsCentered());
	A11Y_CHECK(advice.distance == FramingDistance::Good);
}

A11Y_TEST(FramingNoFaceStillReportsLight)
{
	FramingAdvice advice = AnalyzeFraming({}, wholePicture, 2.0, false);
	A11Y_CHECK(advice.faceCount == 0);
	A11Y_CHECK(advice.light == FramingLight::Black);

	A11Y_CHECK(AnalyzeFraming({}, wholePicture, 30.0, false).light == FramingLight::VeryDark);
	A11Y_CHECK(AnalyzeFraming({}, wholePicture, 60.0, false).light == FramingLight::Dark);
	A11Y_CHECK(AnalyzeFraming({}, wholePicture, 240.0, false).light == FramingLight::VeryBright);
}

A11Y_TEST(FramingAdviceComparesAllParts)
{
	FramingAdvice a = AnalyzeFraming({Face(0.35, 0.45)}, wholePicture, goodLuma, false);
	FramingAdvice b = AnalyzeFraming({Face(0.35, 0.45)}, wholePicture, 60.0, false);
	A11Y_CHECK(a == a);
	A11Y_CHECK(a != b);
}

A11Y_TEST(FramingGrayConversion)
{
	/* White, black, pure red and a padded line. */
	const uint8_t rgba[] = {255, 255, 255, 255, 0, 0, 0, 255, 0xAA, 0xAA, 0xAA, 0xAA,
				255, 0,   0,   255, 0, 0, 0, 255, 0xAA, 0xAA, 0xAA, 0xAA};
	uint8_t gray[4] = {};
	RgbaToGray(rgba, 2, 2, 12, gray);

	A11Y_CHECK(gray[0] == 255);
	A11Y_CHECK(gray[1] == 0);
	A11Y_CHECK(gray[2] == 76);
	A11Y_CHECK(gray[3] == 0);
	A11Y_CHECK(std::fabs(MeanLuma(gray, 2, 2, 2) - (255.0 + 76.0) / 4.0) < 0.0001);
}

A11Y_TEST(FramingWindowOfAFullScreenCamera)
{
	A11Y_CHECK(SameBox(VisibleWindow(Camera(Scale(1.5, 1.5))), wholePicture));

	/* Flipped, the same pixels are on the canvas. */
	A11Y_CHECK(SameBox(VisibleWindow(Camera(Scale(-1.5, 1.5, 1920.0))), wholePicture));
}

A11Y_TEST(FramingWindowLeavesOutWhatIsOffTheCanvas)
{
	/* Half of the camera is past the right edge. */
	A11Y_CHECK(SameBox(VisibleWindow(Camera(Scale(1.0, 1.0, 1280.0, 0.0))), {0.0, 0.0, 0.5, 1.0}));

	A11Y_CHECK(VisibleWindow(Camera(Scale(1.0, 1.0, 2000.0, 0.0))).width == 0.0);
}

A11Y_TEST(FramingWindowFollowsTheCrop)
{
	ItemPlacement placement = Camera(Scale(1.5, 1.5));
	placement.cropLeft = 320.0;
	placement.cropRight = 320.0;
	A11Y_CHECK(SameBox(VisibleWindow(placement), {0.25, 0.0, 0.5, 1.0}));
}

A11Y_TEST(FramingWindowFollowsTheCropToTheBoundingBox)
{
	/* Scaled to fill a 720x720 box at x 600: libobs crops 560 pixels of
	 * width, on the side the alignment leaves out. */
	ItemPlacement placement = Camera(Scale(1.0, 1.0, 600.0, 0.0));
	placement.cropToBox = true;
	placement.box = Scale(720.0, 720.0, 600.0, 0.0);

	A11Y_CHECK(SameBox(VisibleWindow(placement), {280.0 / 1280.0, 0.0, 720.0 / 1280.0, 1.0}));

	placement.boxAlignX = 0.0;
	A11Y_CHECK(SameBox(VisibleWindow(placement), {0.0, 0.0, 720.0 / 1280.0, 1.0}));
}

A11Y_TEST(FramingAdviceIsAboutTheWindow)
{
	const FaceBox middleHalf = {0.25, 0.0, 0.5, 1.0};

	FramingAdvice advice = AnalyzeFraming({Face(0.5, 0.45)}, middleHalf, goodLuma, false);
	A11Y_CHECK(advice.IsCentered());
	A11Y_CHECK(advice.visibility == FramingVisibility::Inside);

	/* On the left of the window, and partly out of it. */
	advice = AnalyzeFraming({Face(0.3, 0.45)}, middleHalf, goodLuma, false);
	A11Y_CHECK(advice.horizontal == FramingMove::Left);
	A11Y_CHECK(advice.horizontalAmount == FramingAmount::Large);
	A11Y_CHECK(advice.visibility == FramingVisibility::PartlyOutside);

	advice = AnalyzeFraming({Face(0.1, 0.45)}, middleHalf, goodLuma, false);
	A11Y_CHECK(advice.visibility == FramingVisibility::Outside);
	A11Y_CHECK(advice.horizontal == FramingMove::Left);
}
