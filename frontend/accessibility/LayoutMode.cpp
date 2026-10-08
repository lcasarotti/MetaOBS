#include "LayoutMode.hpp"
#include "AccessibleCommon.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <qt-wrappers.hpp>

#include <QAbstractItemView>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QStringList>
#include <QTimer>

#include <algorithm>
#include <cmath>
#include <iterator>
#include <vector>

#include "moc_LayoutMode.cpp"

/* Loads saved transform states and shows their scene, for undo and redo. */
extern void undo_redo(const std::string &data);

namespace OBS::Accessibility {

namespace {
constexpr int steps[] = {1, 5, 10, 50, 100};
constexpr int defaultStepIndex = 2;
constexpr int undoGroupMs = 1000;
constexpr double fullCanvasShare = 0.9;

constexpr const char *regionKeys[] = {
	"Accessibility.Layout.Region.TopLeft",    "Accessibility.Layout.Region.Top",
	"Accessibility.Layout.Region.TopRight",   "Accessibility.Layout.Region.Left",
	"Accessibility.Layout.Region.Center",     "Accessibility.Layout.Region.Right",
	"Accessibility.Layout.Region.BottomLeft", "Accessibility.Layout.Region.Bottom",
	"Accessibility.Layout.Region.BottomRight",
};

/* The bounding box of an item in canvas pixels. Items in a group are placed
 * relative to the group. */
LayoutRect ItemBox(obs_scene_t *scene, obs_sceneitem_t *item)
{
	matrix4 transform;
	obs_sceneitem_get_box_transform(item, &transform);

	if (obs_sceneitem_t *group = scene ? obs_sceneitem_get_group(scene, item) : nullptr) {
		matrix4 groupTransform;
		obs_sceneitem_get_draw_transform(group, &groupTransform);
		matrix4_mul(&transform, &transform, &groupTransform);
	}

	LayoutRect box = {HUGE_VAL, HUGE_VAL, -HUGE_VAL, -HUGE_VAL};
	for (float x : {0.0f, 1.0f}) {
		for (float y : {0.0f, 1.0f}) {
			vec3 corner;
			vec3_set(&corner, x, y, 0.0f);
			vec3_transform(&corner, &corner, &transform);
			box.left = std::min(box.left, static_cast<double>(corner.x));
			box.top = std::min(box.top, static_cast<double>(corner.y));
			box.right = std::max(box.right, static_cast<double>(corner.x));
			box.bottom = std::max(box.bottom, static_cast<double>(corner.y));
		}
	}
	return box;
}

LayoutRect CanvasRect()
{
	obs_video_info ovi = {};
	obs_get_video_info(&ovi);
	return {0.0, 0.0, static_cast<double>(ovi.base_width), static_cast<double>(ovi.base_height)};
}

/* The items of the scene from the bottom up, with the items of groups in
 * place of the groups. */
std::vector<OBSSceneItem> DrawOrder(obs_scene_t *scene)
{
	std::vector<OBSSceneItem> items;
	auto collect = [](obs_scene_t *, obs_sceneitem_t *item, void *param) {
		auto items = static_cast<std::vector<OBSSceneItem> *>(param);
		if (obs_sceneitem_is_group(item)) {
			obs_sceneitem_group_enum_items(
				item,
				[](obs_scene_t *, obs_sceneitem_t *child, void *param) {
					static_cast<std::vector<OBSSceneItem> *>(param)->emplace_back(child);
					return true;
				},
				items);
		} else {
			items->emplace_back(item);
		}
		return true;
	};
	obs_scene_enum_items(scene, collect, &items);
	return items;
}

bool IsQuarterTurn(float degrees)
{
	float turn = std::fmod(std::fabs(degrees), 180.0f);
	return std::fabs(turn - 90.0f) < 1.0f;
}

QString Round(double value)
{
	return QString::number(std::lround(value));
}

QString ItemName(obs_sceneitem_t *item)
{
	return QT_UTF8(obs_source_get_name(obs_sceneitem_get_source(item)));
}

/* "partly outside the canvas" and similar, or nothing when the whole item is
 * on the canvas. */
QString DescribeCanvasFit(const LayoutRect &box)
{
	double inside = CoveredShare(box, CanvasRect());
	if (box.Area() <= 0.0 || inside >= 1.0) {
		return {};
	}
	if (inside <= 0.0) {
		return QTStr("Accessibility.Layout.Outside");
	}
	return QTStr("Accessibility.Layout.PartlyOutside").arg(ToPercent(inside));
}
} // namespace

LayoutMode::LayoutMode(OBSBasic *main_, QAbstractItemView *view_, ItemGetter currentItem_)
	: QObject(view_),
	  main(main_),
	  view(view_),
	  currentItem(std::move(currentItem_)),
	  stepIndex(defaultStepIndex)
{
	undoTimer = new QTimer(this);
	undoTimer->setSingleShot(true);
	undoTimer->setInterval(undoGroupMs);
	connect(undoTimer, &QTimer::timeout, this, &LayoutMode::CommitChange);
}

LayoutMode::~LayoutMode()
{
	CommitChange();
}

void LayoutMode::Enter()
{
	OBSSceneItem item = currentItem();
	if (!item) {
		AnnounceRequested(QTStr("Accessibility.Layout.NoItem"));
		return;
	}

	obs_source_t *source = obs_sceneitem_get_source(item);
	if ((obs_source_get_output_flags(source) & OBS_SOURCE_VIDEO) == 0) {
		AnnounceRequested(QTStr("Accessibility.Layout.NoVideo").arg(ItemName(item)));
		return;
	}

	if (active) {
		Exit(false);
	}

	sceneUuid = QT_UTF8(obs_source_get_uuid(obs_scene_get_source(obs_sceneitem_get_scene(item))));
	itemId = obs_sceneitem_get_id(item);
	active = true;
	view->setFocus(Qt::OtherFocusReason);
	view->installEventFilter(this);

	AnnounceRequested(QTStr("Accessibility.Layout.Entered").arg(ItemName(item)).arg(steps[stepIndex]));
}

void LayoutMode::Exit(bool announce)
{
	if (!active) {
		return;
	}

	active = false;
	view->removeEventFilter(this);
	CommitChange();

	if (announce) {
		AnnounceRequested(QTStr("Accessibility.Layout.Left"));
	}
}

OBSSceneItem LayoutMode::Target() const
{
	return FindSceneItem(sceneUuid, itemId);
}

bool LayoutMode::eventFilter(QObject *watched, QEvent *event)
{
	if (watched != view || !active) {
		return false;
	}

	switch (event->type()) {
	case QEvent::ShortcutOverride:
		/* Take the keys of the mode from the list's shortcuts, such as
		 * Alt+Up, which moves the item up in the list. */
		if (HandleKey(static_cast<QKeyEvent *>(event), false)) {
			event->accept();
			return true;
		}
		break;
	case QEvent::KeyPress:
		return HandleKey(static_cast<QKeyEvent *>(event), true);
	case QEvent::FocusOut: {
		/* Menus and other windows take focus for a while; moving to
		 * another control leaves the mode. */
		Qt::FocusReason reason = static_cast<QFocusEvent *>(event)->reason();
		if (reason != Qt::PopupFocusReason && reason != Qt::ActiveWindowFocusReason) {
			Exit();
		}
		break;
	}
	default:
		break;
	}

	return false;
}

bool LayoutMode::HandleKey(QKeyEvent *event, bool apply)
{
	Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
	int key = event->key();
	int step = steps[stepIndex];

	auto handle = [apply](const std::function<void()> &action) {
		if (apply) {
			action();
		}
		return true;
	};

	if ((key == Qt::Key_Escape && modifiers == Qt::NoModifier) ||
	    (key == Qt::Key_M && modifiers == Qt::ControlModifier)) {
		return handle([this]() { Exit(); });
	}

	if (key == Qt::Key_Left || key == Qt::Key_Right || key == Qt::Key_Up || key == Qt::Key_Down) {
		int dx = key == Qt::Key_Left ? -1 : (key == Qt::Key_Right ? 1 : 0);
		int dy = key == Qt::Key_Up ? -1 : (key == Qt::Key_Down ? 1 : 0);

		if (modifiers == Qt::NoModifier) {
			return handle([&]() { Move(dx * step, dy * step); });
		}
		if (modifiers == Qt::ControlModifier) {
			return handle([&]() { Resize(dx * step, dy * step); });
		}
		if (modifiers == Qt::AltModifier) {
			return handle([&]() { Crop(static_cast<Qt::Key>(key), step); });
		}
		if (modifiers == (Qt::AltModifier | Qt::ShiftModifier)) {
			return handle([&]() { Crop(static_cast<Qt::Key>(key), -step); });
		}
		return false;
	}

	/* + is shifted on some keyboards and not on others. */
	bool plain = modifiers == Qt::NoModifier;
	bool shifted = modifiers == Qt::ShiftModifier;
	if ((key == Qt::Key_Plus || key == Qt::Key_Equal) && (plain || shifted)) {
		return handle([&]() { ResizeProportionally(step); });
	}
	if (key == Qt::Key_Minus && plain) {
		return handle([&]() { ResizeProportionally(-step); });
	}

	if (!plain && !shifted) {
		return false;
	}

	switch (key) {
	case Qt::Key_R:
		return handle([&]() { Rotate(shifted ? -90.0f : 90.0f); });
	case Qt::Key_S:
		return handle([&]() { ChangeStep(shifted ? -1 : 1); });
	case Qt::Key_D:
		return plain && handle([this]() { DescribeItem(Target()); });
	case Qt::Key_H:
		return plain && handle([]() { AnnounceRequested(QTStr("Accessibility.Layout.Help")); });
	default:
		return false;
	}
}

bool LayoutMode::CanChange(obs_sceneitem_t *item)
{
	if (!item) {
		Exit(false);
		AnnounceRequested(QTStr("Accessibility.Layout.NoItem"));
		return false;
	}

	if (obs_sceneitem_locked(item)) {
		AnnounceRequested(QTStr("Accessibility.Announce.ItemLocked").arg(ItemName(item)));
		return false;
	}

	return true;
}

void LayoutMode::Move(int dx, int dy)
{
	OBSSceneItem item = Target();
	if (!CanChange(item)) {
		return;
	}

	BeginChange();
	vec2 position;
	obs_sceneitem_get_pos(item, &position);
	position.x += static_cast<float>(dx);
	position.y += static_cast<float>(dy);
	obs_sceneitem_set_pos(item, &position);

	LayoutRect box = ItemBox(main->GetCurrentScene(), item);
	QString text = QTStr("Accessibility.Layout.Position").arg(Round(box.left), Round(box.top));
	QString fit = DescribeCanvasFit(box);
	AnnounceRequested(fit.isEmpty() ? text : text + ", " + fit);
}

void LayoutMode::Resize(int dx, int dy)
{
	OBSSceneItem item = Target();
	if (!CanChange(item)) {
		return;
	}

	obs_transform_info info;
	obs_sceneitem_get_info2(item, &info);

	/* Width and height follow the screen, not the rotated source. */
	if (IsQuarterTurn(info.rot)) {
		std::swap(dx, dy);
	}

	if (info.bounds_type != OBS_BOUNDS_NONE) {
		info.bounds.x = std::max(1.0f, info.bounds.x + static_cast<float>(dx));
		info.bounds.y = std::max(1.0f, info.bounds.y + static_cast<float>(dy));
	} else {
		obs_sceneitem_crop crop;
		obs_sceneitem_get_crop(item, &crop);
		obs_source_t *source = obs_sceneitem_get_source(item);
		float width = static_cast<float>(int(obs_source_get_width(source)) - crop.left - crop.right);
		float height = static_cast<float>(int(obs_source_get_height(source)) - crop.top - crop.bottom);
		if (width <= 0.0f || height <= 0.0f) {
			return;
		}

		float newWidth = std::max(1.0f, width * std::fabs(info.scale.x) + static_cast<float>(dx));
		float newHeight = std::max(1.0f, height * std::fabs(info.scale.y) + static_cast<float>(dy));
		info.scale.x = std::copysign(newWidth / width, info.scale.x);
		info.scale.y = std::copysign(newHeight / height, info.scale.y);
	}

	BeginChange();
	obs_sceneitem_set_info2(item, &info);

	LayoutRect box = ItemBox(main->GetCurrentScene(), item);
	AnnounceRequested(QTStr("Accessibility.Layout.Size").arg(Round(box.Width()), Round(box.Height())));
}

void LayoutMode::ResizeProportionally(int delta)
{
	OBSSceneItem item = Target();
	if (!CanChange(item)) {
		return;
	}

	LayoutRect box = ItemBox(main->GetCurrentScene(), item);
	if (box.Width() <= 0.0) {
		return;
	}

	float factor = static_cast<float>(std::max(1.0, box.Width() + delta) / box.Width());

	obs_transform_info info;
	obs_sceneitem_get_info2(item, &info);
	if (info.bounds_type != OBS_BOUNDS_NONE) {
		info.bounds.x = std::max(1.0f, info.bounds.x * factor);
		info.bounds.y = std::max(1.0f, info.bounds.y * factor);
	} else {
		info.scale.x *= factor;
		info.scale.y *= factor;
	}

	BeginChange();
	obs_sceneitem_set_info2(item, &info);

	box = ItemBox(main->GetCurrentScene(), item);
	AnnounceRequested(QTStr("Accessibility.Layout.Size").arg(Round(box.Width()), Round(box.Height())));
}

void LayoutMode::Crop(Qt::Key side, int delta)
{
	OBSSceneItem item = Target();
	if (!CanChange(item)) {
		return;
	}

	obs_sceneitem_crop crop;
	obs_sceneitem_get_crop(item, &crop);
	obs_source_t *source = obs_sceneitem_get_source(item);
	int width = static_cast<int>(obs_source_get_width(source));
	int height = static_cast<int>(obs_source_get_height(source));

	/* Leave at least one pixel of the source. */
	int *value = nullptr;
	int maximum = 0;
	const char *key = nullptr;
	switch (side) {
	case Qt::Key_Left:
		value = &crop.left;
		maximum = width - crop.right - 1;
		key = "Accessibility.Layout.CropLeft";
		break;
	case Qt::Key_Right:
		value = &crop.right;
		maximum = width - crop.left - 1;
		key = "Accessibility.Layout.CropRight";
		break;
	case Qt::Key_Up:
		value = &crop.top;
		maximum = height - crop.bottom - 1;
		key = "Accessibility.Layout.CropTop";
		break;
	case Qt::Key_Down:
		value = &crop.bottom;
		maximum = height - crop.top - 1;
		key = "Accessibility.Layout.CropBottom";
		break;
	default:
		return;
	}

	*value = std::clamp(*value + delta, 0, std::max(maximum, 0));

	BeginChange();
	obs_sceneitem_set_crop(item, &crop);
	AnnounceRequested(QTStr(key).arg(*value));
}

void LayoutMode::Rotate(float degrees)
{
	OBSSceneItem item = Target();
	if (!CanChange(item)) {
		return;
	}

	obs_scene_t *scene = main->GetCurrentScene();
	LayoutRect before = ItemBox(scene, item);

	float rotation = std::fmod(obs_sceneitem_get_rot(item) + degrees, 360.0f);
	if (rotation < 0.0f) {
		rotation += 360.0f;
	}

	BeginChange();
	obs_sceneitem_set_rot(item, rotation);

	/* Items rotate around their position, usually a corner; keep the
	 * center where it was instead. */
	LayoutRect after = ItemBox(scene, item);
	vec2 position;
	obs_sceneitem_get_pos(item, &position);
	position.x += static_cast<float>((before.left + before.right - after.left - after.right) / 2.0);
	position.y += static_cast<float>((before.top + before.bottom - after.top - after.bottom) / 2.0);
	obs_sceneitem_set_pos(item, &position);

	AnnounceRequested(QTStr("Accessibility.Layout.Rotation").arg(Round(rotation)));
}

void LayoutMode::ChangeStep(int direction)
{
	int count = static_cast<int>(std::size(steps));
	stepIndex = std::clamp(stepIndex + direction, 0, count - 1);
	AnnounceRequested(QTStr("Accessibility.Layout.Step").arg(steps[stepIndex]));
}

void LayoutMode::BeginChange()
{
	if (undoData.empty()) {
		OBSDataAutoRelease states = obs_scene_save_transform_states(main->GetCurrentScene(), true);
		undoData = obs_data_get_json(states);
	}
	undoTimer->start();
}

void LayoutMode::CommitChange()
{
	undoTimer->stop();
	if (undoData.empty()) {
		return;
	}

	std::string undo = std::move(undoData);
	undoData.clear();

	OBSScene scene = main->GetCurrentScene();
	if (!scene) {
		return;
	}

	OBSDataAutoRelease states = obs_scene_save_transform_states(scene, true);
	std::string redo = obs_data_get_json(states);
	if (redo == undo) {
		return;
	}

	const char *sceneName = obs_source_get_name(obs_scene_get_source(scene));
	main->undo_s.add_action(QTStr("Undo.Transform").arg(sceneName), undo_redo, undo_redo, undo, redo);
}

QString LayoutMode::DescribeBox(obs_sceneitem_t *item) const
{
	LayoutRect canvas = CanvasRect();
	LayoutRect box = ItemBox(main->GetCurrentScene(), item);

	QString place;
	if (CoveredShare(canvas, box) >= fullCanvasShare) {
		place = QTStr("Accessibility.Layout.FullCanvas");
	} else {
		CanvasRegion region = RegionOf(box, canvas.Width(), canvas.Height());
		int index = static_cast<int>(region) - static_cast<int>(CanvasRegion::TopLeft);
		if (index >= 0 && index < static_cast<int>(std::size(regionKeys))) {
			place = QTStr(regionKeys[index]);
		}
	}

	QStringList parts;
	parts << QTStr("Accessibility.Layout.Describe")
			 .arg(ItemName(item), Round(box.left), Round(box.top), Round(box.Width()),
			      Round(box.Height()), place)
			 .arg(ToPercent(CoveredShare(canvas, box)));

	QString fit = DescribeCanvasFit(box);
	if (!fit.isEmpty()) {
		parts << fit;
	}

	float rotation = obs_sceneitem_get_rot(item);
	if (std::lround(rotation) % 360 != 0) {
		parts << QTStr("Accessibility.Layout.Rotation").arg(Round(rotation));
	}

	obs_sceneitem_crop crop;
	obs_sceneitem_get_crop(item, &crop);
	if (crop.left || crop.right || crop.top || crop.bottom) {
		parts << QTStr("Accessibility.Layout.Crop")
				 .arg(crop.left)
				 .arg(crop.right)
				 .arg(crop.top)
				 .arg(crop.bottom);
	}

	if (!obs_sceneitem_visible(item)) {
		parts << QTStr("Accessibility.State.Hidden");
	}

	/* Visible items that share part of the canvas with this one: those
	 * below are partly hidden by it, those above hide part of it. */
	QStringList covers;
	QStringList coveredBy;
	bool above = false;
	for (const OBSSceneItem &other : DrawOrder(main->GetCurrentScene())) {
		if (other.Get() == item) {
			above = true;
			continue;
		}
		if (!obs_sceneitem_visible(other)) {
			continue;
		}

		LayoutRect otherBox = ItemBox(main->GetCurrentScene(), other);
		double share = above ? CoveredShare(box, otherBox) : CoveredShare(otherBox, box);
		if (share <= 0.0) {
			continue;
		}

		QString entry = QTStr("Accessibility.Layout.Overlap").arg(ItemName(other)).arg(ToPercent(share));
		(above ? coveredBy : covers) << entry;
	}

	/* The nearest items come first. */
	std::reverse(covers.begin(), covers.end());
	if (!covers.isEmpty()) {
		parts << QTStr("Accessibility.Layout.Covers").arg(covers.join(", "));
	}
	if (!coveredBy.isEmpty()) {
		parts << QTStr("Accessibility.Layout.CoveredBy").arg(coveredBy.join(", "));
	}

	return parts.join("; ");
}

void LayoutMode::DescribeItem(obs_sceneitem_t *item)
{
	if (!item) {
		AnnounceRequested(QTStr("Accessibility.Layout.NoItem"));
		return;
	}
	AnnounceRequested(DescribeBox(item));
}

} // namespace OBS::Accessibility
