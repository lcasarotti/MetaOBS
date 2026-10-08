#pragma once

#include "AccessibleLayout.hpp"

#include <obs.hpp>

#include <QObject>
#include <QString>

#include <functional>
#include <string>

class OBSBasic;
class QAbstractItemView;
class QKeyEvent;
class QTimer;

namespace OBS::Accessibility {

/* Moves, resizes, crops and rotates a scene item from the keyboard while the
 * Sources list has focus, and describes where it is. While the mode is on, it
 * takes the arrow keys and a few letters from the list:
 * - arrows move the item by the step
 * - Ctrl+arrows change its width or height, + and - its size in proportion
 * - Alt+arrows crop that side, Alt+Shift+arrows uncrop it
 * - R and Shift+R rotate by 90 degrees around the center
 * - S and Shift+S change the step, D describes the item, H lists the keys
 * - Escape or Ctrl+M leaves the mode
 * Changes made close together are undone together, like the main window's
 * nudges. */
class LayoutMode : public QObject {
	Q_OBJECT

public:
	using ItemGetter = std::function<OBSSceneItem()>;

	LayoutMode(OBSBasic *main, QAbstractItemView *view, ItemGetter currentItem);
	~LayoutMode();

	bool IsActive() const { return active; }

	void Enter();
	void Exit(bool announce = true);

	/* Speaks the position, size, region and overlaps of the item. */
	void DescribeItem(obs_sceneitem_t *item);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	bool HandleKey(QKeyEvent *event, bool apply);
	OBSSceneItem Target() const;
	bool CanChange(obs_sceneitem_t *item);

	void Move(int dx, int dy);
	void Resize(int dx, int dy);
	void ResizeProportionally(int delta);
	void Crop(Qt::Key side, int delta);
	void Rotate(float degrees);
	void ChangeStep(int direction);

	void BeginChange();
	void CommitChange();

	QString DescribeBox(obs_sceneitem_t *item) const;

	OBSBasic *main;
	QAbstractItemView *view;
	ItemGetter currentItem;

	bool active = false;
	QString sceneUuid;
	int64_t itemId = 0;
	int stepIndex;

	/* The transform states before the first change of a group of changes,
	 * which is added to the undo stack once the changes stop. */
	std::string undoData;
	QTimer *undoTimer;
};

} // namespace OBS::Accessibility
