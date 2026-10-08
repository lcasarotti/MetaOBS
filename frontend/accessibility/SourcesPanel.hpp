#pragma once

#include <obs-frontend-api.h>
#include <obs.hpp>

#include <QGroupBox>
#include <QKeySequence>
#include <QList>
#include <QSet>

#include <vector>

class OBSBasic;
class QAction;
class QMenu;
class QStandardItem;
class QStandardItemModel;
class QTimer;
class QTreeView;

namespace OBS::Accessibility {

class LayoutMode;

/* Tree of the scene items of the current scene (the preview scene in Studio
 * Mode), with groups as real tree nodes and each item's state in its text. */
class SourcesPanel : public QGroupBox {
	Q_OBJECT

public:
	SourcesPanel(OBSBasic *main, QWidget *parent = nullptr);
	~SourcesPanel();

	QTreeView *View() const { return view; }
	QMenu *Menu() const { return menu; }

	/* Adds the Transform submenu and the clipboard items, which mirror
	 * main window actions, leaving out the reserved shortcuts. */
	void AddMirroredActions(const QList<QKeySequence> &reserved);

private:
	struct Entry {
		QString sceneUuid;
		int64_t itemId;
		QString text;
		bool isGroup;
		std::vector<Entry> children;
	};

	static void OnFrontendEvent(enum obs_frontend_event event, void *data);
	static void OnSceneSignal(void *data, calldata_t *params);

	void ConnectSceneSignals();
	void ScheduleRefresh();
	void Refresh();
	void UpdateTitle();
	void CreateMenu();

	static std::vector<Entry> CollectEntries(obs_scene_t *scene);
	static void CollectKeys(const std::vector<Entry> &entries, QStringList &keys);
	void AppendEntries(QStandardItem *parent, const std::vector<Entry> &entries);
	void UpdateTexts(QStandardItem *parent, const std::vector<Entry> &entries);

	OBSSceneItem GetSelectedItem() const;
	std::vector<OBSSceneItem> GetSelectedItems() const;
	void SyncSelection();
	void UpdateMenuState();
	void FillItemMenu(QMenu *itemMenu, QMenu *(OBSBasic::*fill)(QMenu *, obs_sceneitem_t *));

	void OpenProperties();
	void OpenFilters();
	void OpenTransform();
	void OpenInteraction();
	void RenameSelected();
	void ToggleVisibility();
	void ToggleLock();
	void RemoveSelected();
	void MoveSelected(obs_order_movement movement);

	OBSBasic *main;
	QTreeView *view;
	QStandardItemModel *model;
	QTimer *refreshTimer;
	QMenu *menu;

	OBSWeakSource currentScene;
	std::vector<OBSSignal> sceneSignals;
	OBSSignal renameSignal;
	QStringList structureKeys;
	QSet<QString> collapsedGroups;

	QAction *addAction;
	QAction *propertiesAction;
	QAction *filtersAction;
	QAction *transformAction;
	QAction *interactAction;
	QAction *renameAction;
	QAction *visibilityAction;
	QAction *lockAction;
	QAction *removeAction;
	QAction *moveUpAction;
	QAction *moveDownAction;
	QAction *moveToTopAction;
	QAction *moveToBottomAction;
	QAction *groupAction;
	QAction *ungroupAction;
	QAction *copyFiltersAction;
	QAction *pasteFiltersAction;
	QAction *showTransitionAction;
	QAction *hideTransitionAction;
	QAction *hideInMixerAction;
	QAction *resizeOutputAction;
	QAction *screenshotAction;
	QAction *layoutModeAction;
	QAction *describeLayoutAction;
	LayoutMode *layoutMode;

	QMenu *orderMenu;
	QMenu *scaleFilteringMenu;
	QMenu *blendingModeMenu;
	QMenu *blendingMethodMenu;
	QMenu *deinterlacingMenu;
	QMenu *projectorMenu;
	QAction *clipboardSeparator;
};

} // namespace OBS::Accessibility
