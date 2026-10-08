#pragma once

/* Geometry behind the spoken descriptions of the layout. Nothing here depends
 * on the running application, so the unit tests can cover it. */
namespace OBS::Accessibility {

/* An axis-aligned rectangle in canvas pixels. */
struct LayoutRect {
	double left = 0.0;
	double top = 0.0;
	double right = 0.0;
	double bottom = 0.0;

	double Width() const { return right - left; }
	double Height() const { return bottom - top; }
	double Area() const { return Width() > 0.0 && Height() > 0.0 ? Width() * Height() : 0.0; }
};

/* The ninth of the canvas that holds the center of a rectangle. */
enum class CanvasRegion {
	Invalid = 0,
	TopLeft,
	Top,
	TopRight,
	Left,
	Center,
	Right,
	BottomLeft,
	Bottom,
	BottomRight,
};

LayoutRect Intersect(const LayoutRect &a, const LayoutRect &b);

/* The share of the area of rect that other covers, from 0 to 1. */
double CoveredShare(const LayoutRect &rect, const LayoutRect &other);

CanvasRegion RegionOf(const LayoutRect &rect, double canvasWidth, double canvasHeight);

/* Rounds to the nearest whole percent, from 0 to 100, without reporting a
 * share that is not complete as 100 or a share that is not empty as 0. */
int ToPercent(double share);

} // namespace OBS::Accessibility
