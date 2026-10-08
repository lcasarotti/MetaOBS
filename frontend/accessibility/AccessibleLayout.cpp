#include "AccessibleLayout.hpp"

#include <algorithm>
#include <cmath>

namespace OBS::Accessibility {

LayoutRect Intersect(const LayoutRect &a, const LayoutRect &b)
{
	LayoutRect result;
	result.left = std::max(a.left, b.left);
	result.top = std::max(a.top, b.top);
	result.right = std::min(a.right, b.right);
	result.bottom = std::min(a.bottom, b.bottom);

	if (result.right < result.left || result.bottom < result.top) {
		return {};
	}
	return result;
}

double CoveredShare(const LayoutRect &rect, const LayoutRect &other)
{
	double area = rect.Area();
	if (area <= 0.0) {
		return 0.0;
	}
	return std::clamp(Intersect(rect, other).Area() / area, 0.0, 1.0);
}

CanvasRegion RegionOf(const LayoutRect &rect, double canvasWidth, double canvasHeight)
{
	if (canvasWidth <= 0.0 || canvasHeight <= 0.0) {
		return CanvasRegion::Invalid;
	}

	auto third = [](double center, double size) {
		double position = center / size;
		return position < 1.0 / 3.0 ? 0 : (position < 2.0 / 3.0 ? 1 : 2);
	};

	int column = third((rect.left + rect.right) / 2.0, canvasWidth);
	int row = third((rect.top + rect.bottom) / 2.0, canvasHeight);
	return static_cast<CanvasRegion>(static_cast<int>(CanvasRegion::TopLeft) + row * 3 + column);
}

int ToPercent(double share)
{
	int percent = static_cast<int>(std::lround(std::clamp(share, 0.0, 1.0) * 100.0));
	if (percent == 100 && share < 1.0) {
		return 99;
	}
	if (percent == 0 && share > 0.0) {
		return 1;
	}
	return percent;
}

} // namespace OBS::Accessibility
