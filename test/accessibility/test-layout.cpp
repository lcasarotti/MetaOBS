/* Checks the geometry behind the spoken layout descriptions: where an item is
 * on the canvas and how much of it other items cover. */

#include "a11y-test.hpp"

#include <accessibility/AccessibleLayout.hpp>

#include <cmath>

using OBS::Accessibility::CanvasRegion;
using OBS::Accessibility::CoveredShare;
using OBS::Accessibility::Intersect;
using OBS::Accessibility::LayoutRect;
using OBS::Accessibility::RegionOf;
using OBS::Accessibility::ToPercent;

namespace {
constexpr double canvasWidth = 1920.0;
constexpr double canvasHeight = 1080.0;

bool Near(double a, double b)
{
	return std::fabs(a - b) < 0.0001;
}

LayoutRect Rect(double left, double top, double width, double height)
{
	return {left, top, left + width, top + height};
}
} // namespace

A11Y_TEST(LayoutIntersectionKeepsTheSharedPart)
{
	LayoutRect overlap = Intersect(Rect(0, 0, 100, 100), Rect(50, 50, 100, 100));
	A11Y_CHECK(Near(overlap.left, 50) && Near(overlap.top, 50));
	A11Y_CHECK(Near(overlap.Width(), 50) && Near(overlap.Height(), 50));

	A11Y_CHECK(Intersect(Rect(0, 0, 100, 100), Rect(200, 0, 100, 100)).Area() == 0.0);
	/* Touching edges share no area. */
	A11Y_CHECK(Intersect(Rect(0, 0, 100, 100), Rect(100, 0, 100, 100)).Area() == 0.0);
}

A11Y_TEST(LayoutCoveredShareIsRelativeToTheFirstRect)
{
	LayoutRect small = Rect(0, 0, 100, 100);
	LayoutRect large = Rect(0, 0, 400, 100);

	A11Y_CHECK(Near(CoveredShare(small, large), 1.0));
	A11Y_CHECK(Near(CoveredShare(large, small), 0.25));
	A11Y_CHECK(CoveredShare(LayoutRect(), large) == 0.0);
}

A11Y_TEST(LayoutRegionFollowsTheCenter)
{
	A11Y_CHECK(RegionOf(Rect(0, 0, 320, 180), canvasWidth, canvasHeight) == CanvasRegion::TopLeft);
	A11Y_CHECK(RegionOf(Rect(800, 0, 320, 180), canvasWidth, canvasHeight) == CanvasRegion::Top);
	A11Y_CHECK(RegionOf(Rect(1600, 900, 320, 180), canvasWidth, canvasHeight) == CanvasRegion::BottomRight);
	A11Y_CHECK(RegionOf(Rect(0, 0, 1920, 1080), canvasWidth, canvasHeight) == CanvasRegion::Center);
	A11Y_CHECK(RegionOf(Rect(-500, 450, 600, 180), canvasWidth, canvasHeight) == CanvasRegion::Left);

	/* Items outside the canvas fall in the nearest region. */
	A11Y_CHECK(RegionOf(Rect(3000, 2000, 10, 10), canvasWidth, canvasHeight) == CanvasRegion::BottomRight);
	A11Y_CHECK(RegionOf(Rect(0, 0, 10, 10), 0, 0) == CanvasRegion::Invalid);
}

A11Y_TEST(LayoutPercentKeepsSliversVisible)
{
	A11Y_CHECK(ToPercent(0.0) == 0);
	A11Y_CHECK(ToPercent(1.0) == 100);
	A11Y_CHECK(ToPercent(0.5) == 50);
	A11Y_CHECK(ToPercent(0.001) == 1);
	A11Y_CHECK(ToPercent(0.999) == 99);
	A11Y_CHECK(ToPercent(1.5) == 100);
}
