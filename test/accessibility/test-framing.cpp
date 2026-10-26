/* Checks the advice on the framing given from the faces found in a camera's
 * picture: which way to move, how far from the camera to sit and how bright
 * the picture is. */

#include "a11y-test.hpp"

#include <accessibility/FramingAnalysis.hpp>

#include <cmath>
#include <vector>

using OBS::Accessibility::AnalyzeFraming;
using OBS::Accessibility::FaceBox;
using OBS::Accessibility::FramingAdvice;
using OBS::Accessibility::FramingAmount;
using OBS::Accessibility::FramingDistance;
using OBS::Accessibility::FramingLight;
using OBS::Accessibility::FramingMove;
using OBS::Accessibility::MeanLuma;
using OBS::Accessibility::RgbaToGray;

namespace {
constexpr double goodLuma = 128.0;

/* A face of the given size whose center is at (centerX, centerY). */
FaceBox Face(double centerX, double centerY, double width = 0.2, double height = 0.35)
{
	return {centerX - width / 2.0, centerY - height / 2.0, width, height};
}
} // namespace

A11Y_TEST(FramingCenteredFace)
{
	FramingAdvice advice = AnalyzeFraming({Face(0.5, 0.45)}, goodLuma, false);
	A11Y_CHECK(advice.faceCount == 1);
	A11Y_CHECK(advice.IsCentered());
	A11Y_CHECK(advice.distance == FramingDistance::Good);
	A11Y_CHECK(advice.light == FramingLight::Good);

	/* Within the tolerance. */
	A11Y_CHECK(AnalyzeFraming({Face(0.56, 0.52)}, goodLuma, false).IsCentered());
}

A11Y_TEST(FramingMoveIsSeenFromThePerson)
{
	/* A face on the left of a camera's picture is on the person's right
	 * of the camera's axis: they move to their left. */
	FramingAdvice advice = AnalyzeFraming({Face(0.35, 0.45)}, goodLuma, false);
	A11Y_CHECK(advice.horizontal == FramingMove::Left);
	A11Y_CHECK(advice.horizontalAmount == FramingAmount::Slight);
	A11Y_CHECK(advice.vertical == FramingMove::None);

	advice = AnalyzeFraming({Face(0.85, 0.45)}, goodLuma, false);
	A11Y_CHECK(advice.horizontal == FramingMove::Right);
	A11Y_CHECK(advice.horizontalAmount == FramingAmount::Large);

	/* A mirrored picture shows the person's left on the left. */
	A11Y_CHECK(AnalyzeFraming({Face(0.35, 0.45)}, goodLuma, true).horizontal == FramingMove::Right);
}

A11Y_TEST(FramingVerticalPosition)
{
	FramingAdvice advice = AnalyzeFraming({Face(0.5, 0.2)}, goodLuma, false);
	A11Y_CHECK(advice.vertical == FramingMove::FaceTooHigh);
	A11Y_CHECK(advice.verticalAmount == FramingAmount::Large);

	advice = AnalyzeFraming({Face(0.5, 0.6)}, goodLuma, false);
	A11Y_CHECK(advice.vertical == FramingMove::FaceTooLow);
	A11Y_CHECK(advice.verticalAmount == FramingAmount::Slight);
}

A11Y_TEST(FramingDistanceFollowsFaceHeight)
{
	A11Y_CHECK(AnalyzeFraming({Face(0.5, 0.45, 0.06, 0.1)}, goodLuma, false).distance == FramingDistance::TooFar);
	A11Y_CHECK(AnalyzeFraming({Face(0.5, 0.45, 0.4, 0.7)}, goodLuma, false).distance ==
		   FramingDistance::TooClose);
}

A11Y_TEST(FramingUsesTheLargestFace)
{
	std::vector<FaceBox> faces = {Face(0.1, 0.45, 0.05, 0.08), Face(0.5, 0.45), Face(0.9, 0.45, 0.05, 0.08)};
	FramingAdvice advice = AnalyzeFraming(faces, goodLuma, false);
	A11Y_CHECK(advice.faceCount == 3);
	A11Y_CHECK(advice.IsCentered());
	A11Y_CHECK(advice.distance == FramingDistance::Good);
}

A11Y_TEST(FramingNoFaceStillReportsLight)
{
	FramingAdvice advice = AnalyzeFraming({}, 2.0, false);
	A11Y_CHECK(advice.faceCount == 0);
	A11Y_CHECK(advice.light == FramingLight::Black);

	A11Y_CHECK(AnalyzeFraming({}, 30.0, false).light == FramingLight::VeryDark);
	A11Y_CHECK(AnalyzeFraming({}, 60.0, false).light == FramingLight::Dark);
	A11Y_CHECK(AnalyzeFraming({}, 240.0, false).light == FramingLight::VeryBright);
}

A11Y_TEST(FramingAdviceComparesAllParts)
{
	FramingAdvice a = AnalyzeFraming({Face(0.35, 0.45)}, goodLuma, false);
	FramingAdvice b = AnalyzeFraming({Face(0.35, 0.45)}, 60.0, false);
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
