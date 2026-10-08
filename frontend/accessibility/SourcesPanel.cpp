#include "SourcesPanel.hpp"
#include "AccessibleBridge.hpp"
#include "AccessibleCommon.hpp"
#include "LayoutMode.hpp"
#include "TransitionDialogs.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <qt-wrappers.hpp>

#include <QAction>
#include <QMenu>
#include <QStandardItemModel>
#include <QTimer>
#include <QTreeView>
#include <QVBoxLayout>

#include <algorithm>

#include "moc_SourcesPanel.cpp"

namespace OBS::Accessibility {

namespace {
constexpr int sceneUuidRole = Qt::UserRole;
constexpr int itemIdRole = Qt::UserRole + 1;
constexpr int isGroupRole = Qt::UserRole + 2;

constexpr const char *sceneSignalNames[] = {"item_add",  "item_remove",  "reorder",
					    "refresh",   "item_visible", "item_locked"};

QString ItemKey(const QString &sceneUuid, int64_t itemId)
{
	return sceneUuid + "/" + QString::number(itemId);
}

QString ItemKey(const QModelIndex &index)
{
	return ItemKey(index.data(sceneUuidRole).toString(), index.data(itemIdRole).toLongLong());
}

QString DescribeItem(obs_sceneitem_t *item)
{
	obs_source_t *source = obs_sceneitem_get_source(item);
	QString text = QT_UTF8(obs_source_get_name(source));

	const char *typeName = obs_source_get_display_name(obs_source_get_id(source));
	if (typeName && *typeName) {
		text += ", " + QT_UTF8(typeName);
	}
	if (!obs_sceneitem_visible(item)) {
		text += ", " + QTStr("Accessibility.State.Hidden");
	}
	if (obs_sceneitem_locked(item)) {
		text += ", " + QTStr("Accessibility.State.Locked");
	}

	return text;
}

std::vector<OBSSceneItem> EnumItemsTopFirst(obs_scene_t *scene, obs_sceneitem_t *group)
{
	std::vector<OBSSceneItem> items;
	auto collect = [](obs_scene_t *, obs_sceneitem_t *item, void *param) {
		static_cast<std::vector<OBSSceneItem> *>(param)->emplace_back(item);
		return true;
	};

	if (group) {
		obs_sceneitem_group_enum_items(group, collect, &items);
	} else {
		obs_scene_enum_items(scene, collect, &items);
	}

	/* Scenes enumerate from the bottom; lists show the top item first. */
	std::reverse(items.begin(), items.end());
	return items;
}

class SourceTreeView : public QTreeView {
public:
	using QTreeView::QTreeView;
	SourcesPanel *panel = nullptr;

protected:
	void contextMenuEvent(QContextMenuEvent *event) override { ExecContextMenu(this, panel->Menu(), event); }
};
} // namespace

SourcesPanel::SourcesPanel(OBSBasic *main_, QWidget *parent) : QGroupBox(parent), main(main_)
{
	SourceTreeView *treeView = new SourceTreeView(this);
	treeView->panel = this;
	view = treeView;
	view->setHeaderHidden(true);
	/* Several items can be selected with Shift and the arrows, or with
	 * Ctrl+Space, to remove, move or group them together. */
	view->setSelectionMode(QAbstractItemView::ExtendedSelection);
	view->setEditTriggers(QAbstractItemView::NoEditTriggers);

	model = new QStandardItemModel(this);
	view->setModel(model);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addWidget(view);

	addAction = CreatePanelAction(view, QTStr("Accessibility.Sources.Add"));
	propertiesAction = CreatePanelAction(view, QTStr("Properties"));
	filtersAction = CreatePanelAction(view, QTStr("Filters"), {QKeySequence(Qt::CTRL | Qt::Key_F)});
	transformAction = CreatePanelAction(view, QTStr("Accessibility.Sources.EditTransform"),
					    {QKeySequence(Qt::CTRL | Qt::Key_T)});
	interactAction = CreatePanelAction(view, QTStr("Interact"), {QKeySequence(Qt::CTRL | Qt::Key_I)});
	renameAction = CreatePanelAction(view, QTStr("Rename"), {QKeySequence(Qt::Key_F2)});
	visibilityAction = CreatePanelAction(view, QTStr("Accessibility.Sources.ToggleVisibility"),
					     {QKeySequence(Qt::Key_Space)});
	lockAction = CreatePanelAction(view, QTStr("Accessibility.Sources.ToggleLock"),
				       {QKeySequence(Qt::CTRL | Qt::Key_L)});
	removeAction = CreatePanelAction(view, QTStr("Remove"), {QKeySequence(QKeySequence::Delete)});
	moveUpAction = CreatePanelAction(view, QTStr("MoveUp"), {QKeySequence(Qt::ALT | Qt::Key_Up)});
	moveDownAction = CreatePanelAction(view, QTStr("MoveDown"), {QKeySequence(Qt::ALT | Qt::Key_Down)});
	moveToTopAction = CreatePanelAction(view, QTStr("Accessibility.Sources.MoveToTop"),
					    {QKeySequence(Qt::ALT | Qt::Key_Home)});
	moveToBottomAction = CreatePanelAction(view, QTStr("Accessibility.Sources.MoveToBottom"),
					       {QKeySequence(Qt::ALT | Qt::Key_End)});
	groupAction =
		CreatePanelAction(view, QTStr("Basic.Main.GroupItems"), {QKeySequence(Qt::CTRL | Qt::Key_G)});
	ungroupAction = CreatePanelAction(view, QTStr("Basic.Main.Ungroup"),
					  {QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_G)});
	copyFiltersAction = CreatePanelAction(view, QTStr("Copy.Filters"));
	pasteFiltersAction = CreatePanelAction(view, QTStr("Paste.Filters"));
	showTransitionAction = CreatePanelAction(view, QTStr("Accessibility.Sources.ShowTransition"));
	hideTransitionAction = CreatePanelAction(view, QTStr("Accessibility.Sources.HideTransition"));
	hideInMixerAction = CreatePanelAction(view, QTStr("HideMixer"));
	hideInMixerAction->setCheckable(true);
	resizeOutputAction = CreatePanelAction(view, QTStr("ResizeOutputSizeOfSource"));
	screenshotAction = CreatePanelAction(view, QTStr("Screenshot.Source"));
	layoutModeAction = CreatePanelAction(view, QTStr("Accessibility.Sources.LayoutMode"),
					     {QKeySequence(Qt::CTRL | Qt::Key_M)});
	describeLayoutAction = CreatePanelAction(view, QTStr("Accessibility.Sources.DescribeLayout"));

	layoutMode = new LayoutMode(main, view, [this]() { return GetSelectedItem(); });

	connect(addAction, &QAction::triggered, this,
		[this]() { OBSAccessibleBridge::AddSourceAction(main)->trigger(); });
	connect(propertiesAction, &QAction::triggered, this, &SourcesPanel::OpenProperties);
	connect(filtersAction, &QAction::triggered, this, &SourcesPanel::OpenFilters);
	connect(transformAction, &QAction::triggered, this, &SourcesPanel::OpenTransform);
	connect(interactAction, &QAction::triggered, this, &SourcesPanel::OpenInteraction);
	connect(renameAction, &QAction::triggered, this, &SourcesPanel::RenameSelected);
	connect(visibilityAction, &QAction::triggered, this, &SourcesPanel::ToggleVisibility);
	connect(lockAction, &QAction::triggered, this, &SourcesPanel::ToggleLock);
	connect(removeAction, &QAction::triggered, this, &SourcesPanel::RemoveSelected);
	connect(moveUpAction, &QAction::triggered, this, [this]() { MoveSelected(OBS_ORDER_MOVE_UP); });
	connect(moveDownAction, &QAction::triggered, this, [this]() { MoveSelected(OBS_ORDER_MOVE_DOWN); });
	connect(moveToTopAction, &QAction::triggered, this, [this]() { MoveSelected(OBS_ORDER_MOVE_TOP); });
	connect(moveToBottomAction, &QAction::triggered, this, [this]() { MoveSelected(OBS_ORDER_MOVE_BOTTOM); });
	connect(groupAction, &QAction::triggered, this, [this]() {
		if (GetSelectedItems().size() > 1) {
			SyncSelection();
			OBSAccessibleBridge::GroupSelectedSources(main);
		}
	});
	connect(ungroupAction, &QAction::triggered, this, [this]() {
		OBSSceneItem item = GetSelectedItem();
		if (item && obs_sceneitem_is_group(item)) {
			SyncSelection();
			OBSAccessibleBridge::UngroupSelectedSources(main);
		}
	});
	connect(copyFiltersAction, &QAction::triggered, this, [this]() {
		if (OBSSceneItem item = GetSelectedItem()) {
			OBSAccessibleBridge::CopyFilters(main, obs_sceneitem_get_source(item));
		}
	});
	connect(pasteFiltersAction, &QAction::triggered, this, [this]() {
		if (OBSSceneItem item = GetSelectedItem()) {
			OBSAccessibleBridge::PasteFilters(main, obs_sceneitem_get_source(item));
		}
	});
	connect(showTransitionAction, &QAction::triggered, this, [this]() {
		if (OBSSceneItem item = GetSelectedItem()) {
			EditVisibilityTransition(main, item, true, window());
		}
	});
	connect(hideTransitionAction, &QAction::triggered, this, [this]() {
		if (OBSSceneItem item = GetSelectedItem()) {
			EditVisibilityTransition(main, item, false, window());
		}
	});
	connect(hideInMixerAction, &QAction::triggered, this, [this](bool checked) {
		if (OBSSceneItem item = GetSelectedItem()) {
			SetHiddenInMixer(main, obs_sceneitem_get_source(item), checked);
		}
	});
	connect(resizeOutputAction, &QAction::triggered, this, [this]() {
		if (GetSelectedItem()) {
			SyncSelection();
			OBSAccessibleBridge::ResizeOutputToSelectedSource(main);
		}
	});
	connect(screenshotAction, &QAction::triggered, this, [this]() {
		if (OBSSceneItem item = GetSelectedItem()) {
			OBSAccessibleBridge::Screenshot(main, obs_sceneitem_get_source(item));
		}
	});
	connect(layoutModeAction, &QAction::triggered, this, [this]() {
		if (layoutMode->IsActive()) {
			layoutMode->Exit();
		} else {
			layoutMode->Enter();
		}
	});
	connect(describeLayoutAction, &QAction::triggered, this,
		[this]() { layoutMode->DescribeItem(GetSelectedItem()); });

	CreateMenu();

	/* Enter and double-click open the properties, as double-click does in
	 * the main window. */
	connect(view, &QTreeView::activated, this, &SourcesPanel::OpenProperties);
	connect(view->selectionModel(), &QItemSelectionModel::selectionChanged, this, &SourcesPanel::SyncSelection);

	refreshTimer = new QTimer(this);
	refreshTimer->setSingleShot(true);
	refreshTimer->setInterval(30);
	connect(refreshTimer, &QTimer::timeout, this, &SourcesPanel::Refresh);

	renameSignal.Connect(obs_get_signal_handler(), "source_rename", OnSceneSignal, this);
	obs_frontend_add_event_callback(OnFrontendEvent, this);

	Refresh();
}

SourcesPanel::~SourcesPanel()
{
	obs_frontend_remove_event_callback(OnFrontendEvent, this);
	sceneSignals.clear();
	renameSignal.Disconnect();
}

void SourcesPanel::CreateMenu()
{
	/* A single menu serves the menu bar and the context menu: Qt caches
	 * each action's accessible interface with the first menu that shows
	 * it, so an action in a second, temporary menu becomes invisible to
	 * screen readers in both. */
	menu = new QMenu(QTStr("Accessibility.Menu.Source"), this);
	menu->addAction(addAction);
	menu->addSeparator();
	menu->addAction(propertiesAction);
	menu->addAction(filtersAction);
	menu->addAction(transformAction);
	menu->addAction(interactAction);
	menu->addAction(layoutModeAction);
	menu->addAction(describeLayoutAction);
	menu->addSeparator();
	menu->addAction(visibilityAction);
	menu->addAction(lockAction);
	menu->addAction(renameAction);
	menu->addAction(removeAction);
	menu->addSeparator();

	orderMenu = menu->addMenu(QTStr("Basic.MainMenu.Edit.Order"));
	orderMenu->addAction(moveUpAction);
	orderMenu->addAction(moveDownAction);
	orderMenu->addAction(moveToTopAction);
	orderMenu->addAction(moveToBottomAction);

	/* AddMirroredActions() inserts the Transform submenu and the source
	 * clipboard items here. */
	clipboardSeparator = menu->addSeparator();
	menu->addAction(copyFiltersAction);
	menu->addAction(pasteFiltersAction);
	menu->addSeparator();
	menu->addAction(groupAction);
	menu->addAction(ungroupAction);
	menu->addSeparator();

	scaleFilteringMenu = menu->addMenu(QTStr("ScaleFiltering"));
	blendingModeMenu = menu->addMenu(QTStr("BlendingMode"));
	blendingMethodMenu = menu->addMenu(QTStr("BlendingMethod"));
	deinterlacingMenu = menu->addMenu(QTStr("Deinterlacing"));
	menu->addAction(showTransitionAction);
	menu->addAction(hideTransitionAction);
	menu->addSeparator();
	menu->addAction(hideInMixerAction);
	menu->addAction(resizeOutputAction);
	menu->addSeparator();
	projectorMenu = menu->addMenu(QTStr("Projector.Open.Source"));
	menu->addAction(screenshotAction);

	/* The main window builds these submenus for one scene item, so they
	 * are built again each time they open. */
	connect(scaleFilteringMenu, &QMenu::aboutToShow, this,
		[this]() { FillItemMenu(scaleFilteringMenu, &OBSBasic::AddScaleFilteringMenu); });
	connect(blendingModeMenu, &QMenu::aboutToShow, this,
		[this]() { FillItemMenu(blendingModeMenu, &OBSBasic::AddBlendingModeMenu); });
	connect(blendingMethodMenu, &QMenu::aboutToShow, this,
		[this]() { FillItemMenu(blendingMethodMenu, &OBSBasic::AddBlendingMethodMenu); });
	connect(deinterlacingMenu, &QMenu::aboutToShow, this, [this]() {
		deinterlacingMenu->clear();
		if (OBSSceneItem item = GetSelectedItem()) {
			SyncSelection();
			main->AddDeinterlacingMenu(deinterlacingMenu, obs_sceneitem_get_source(item));
		}
	});
	connect(projectorMenu, &QMenu::aboutToShow, this, [this]() {
		obs_source_t *source = obs_sceneitem_get_source(GetSelectedItem());
		FillProjectorMenu(projectorMenu, "Source", QT_UTF8(obs_source_get_name(source)));
	});

	connect(menu, &QMenu::aboutToShow, this, &SourcesPanel::UpdateMenuState);
}

void SourcesPanel::AddMirroredActions(const QList<QKeySequence> &reserved)
{
	QMenu *transformMenu = new QMenu(QTStr("Basic.MainMenu.Edit.Transform"), menu);
	for (QAction *action : OBSAccessibleBridge::TransformMenu(main)->actions()) {
		if (action->isSeparator()) {
			transformMenu->addSeparator();
		} else {
			transformMenu->addAction(CreateMirrorAction(action, transformMenu, reserved));
		}
	}
	menu->insertMenu(clipboardSeparator, transformMenu);
	menu->insertSeparator(clipboardSeparator);

	for (QAction *action : OBSAccessibleBridge::SourceClipboardActions(main)) {
		menu->insertAction(clipboardSeparator, CreateMirrorAction(action, menu, reserved));
	}
}

void SourcesPanel::FillItemMenu(QMenu *itemMenu, QMenu *(OBSBasic::*fill)(QMenu *, obs_sceneitem_t *))
{
	itemMenu->clear();

	/* The main window's actions apply to its current scene item, which
	 * follows the selection. */
	if (OBSSceneItem item = GetSelectedItem()) {
		SyncSelection();
		(main->*fill)(itemMenu, item);
	}
}

/* The items that depend on the selected source are updated when the menu
 * opens. Items with shortcuts stay enabled, since their shortcuts work
 * without the menu. */
void SourcesPanel::UpdateMenuState()
{
	OBSSceneItem item = GetSelectedItem();
	obs_source_t *source = obs_sceneitem_get_source(item);
	uint32_t flags = source ? obs_source_get_output_flags(source) : 0;
	bool hasVideo = (flags & OBS_SOURCE_VIDEO) != 0;
	bool hasAudio = (flags & OBS_SOURCE_AUDIO) != 0;

	for (QMenu *videoMenu : {scaleFilteringMenu, blendingModeMenu, blendingMethodMenu, projectorMenu}) {
		videoMenu->menuAction()->setVisible(hasVideo);
	}
	deinterlacingMenu->menuAction()->setVisible((flags & OBS_SOURCE_ASYNC_VIDEO) == OBS_SOURCE_ASYNC_VIDEO);

	showTransitionAction->setEnabled(item != nullptr);
	hideTransitionAction->setEnabled(item != nullptr);
	screenshotAction->setVisible(hasVideo);
	describeLayoutAction->setVisible(hasVideo);
	resizeOutputAction->setVisible(hasVideo);
	resizeOutputAction->setEnabled(hasVideo && !obs_video_active() && obs_source_get_width(source) >= 32 &&
				       obs_source_get_height(source) >= 32);

	hideInMixerAction->setVisible(hasAudio);
	hideInMixerAction->setChecked(hasAudio && IsHiddenInMixer(source));

	copyFiltersAction->setEnabled(source && obs_source_filter_count(source) > 0);
	pasteFiltersAction->setEnabled(source && OBSAccessibleBridge::HasCopiedFilters(main));
}

void SourcesPanel::OnFrontendEvent(enum obs_frontend_event event, void *data)
{
	auto panel = static_cast<SourcesPanel *>(data);

	switch (event) {
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
		panel->ScheduleRefresh();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		panel->sceneSignals.clear();
		panel->currentScene = nullptr;
		break;
	default:
		break;
	}
}

/* Scene signals arrive on any thread. */
void SourcesPanel::OnSceneSignal(void *data, calldata_t *)
{
	auto panel = static_cast<SourcesPanel *>(data);
	QMetaObject::invokeMethod(panel, [panel]() { panel->ScheduleRefresh(); }, Qt::QueuedConnection);
}

void SourcesPanel::ScheduleRefresh()
{
	refreshTimer->start();
}

void SourcesPanel::ConnectSceneSignals()
{
	sceneSignals.clear();

	OBSScene scene = main->GetCurrentScene();
	obs_source_t *sceneSource = obs_scene_get_source(scene);
	currentScene = OBSGetWeakRef(sceneSource);
	if (!sceneSource) {
		return;
	}

	std::vector<obs_source_t *> sources = {sceneSource};
	for (const OBSSceneItem &item : EnumItemsTopFirst(scene, nullptr)) {
		if (obs_sceneitem_is_group(item)) {
			sources.push_back(obs_sceneitem_get_source(item));
		}
	}

	for (obs_source_t *source : sources) {
		signal_handler_t *handler = obs_source_get_signal_handler(source);
		for (const char *signal : sceneSignalNames) {
			sceneSignals.emplace_back(handler, signal, OnSceneSignal, this);
		}
	}
}

std::vector<SourcesPanel::Entry> SourcesPanel::CollectEntries(obs_scene_t *scene)
{
	std::vector<Entry> entries;

	for (const OBSSceneItem &item : EnumItemsTopFirst(scene, nullptr)) {
		Entry entry;
		entry.sceneUuid = QT_UTF8(obs_source_get_uuid(obs_scene_get_source(scene)));
		entry.itemId = obs_sceneitem_get_id(item);
		entry.text = DescribeItem(item);
		entry.isGroup = obs_sceneitem_is_group(item);

		if (entry.isGroup) {
			QString groupUuid = QT_UTF8(obs_source_get_uuid(obs_sceneitem_get_source(item)));
			for (const OBSSceneItem &child : EnumItemsTopFirst(nullptr, item)) {
				Entry childEntry;
				childEntry.sceneUuid = groupUuid;
				childEntry.itemId = obs_sceneitem_get_id(child);
				childEntry.text = DescribeItem(child);
				childEntry.isGroup = false;
				entry.children.push_back(childEntry);
			}
		}

		entries.push_back(entry);
	}

	return entries;
}

void SourcesPanel::CollectKeys(const std::vector<Entry> &entries, QStringList &keys)
{
	for (const Entry &entry : entries) {
		keys.append(ItemKey(entry.sceneUuid, entry.itemId));
		if (entry.isGroup) {
			keys.append("[");
			CollectKeys(entry.children, keys);
			keys.append("]");
		}
	}
}

void SourcesPanel::AppendEntries(QStandardItem *parent, const std::vector<Entry> &entries)
{
	for (const Entry &entry : entries) {
		QStandardItem *item = new QStandardItem(entry.text);
		item->setData(entry.sceneUuid, sceneUuidRole);
		item->setData(static_cast<qlonglong>(entry.itemId), itemIdRole);
		item->setData(entry.isGroup, isGroupRole);
		parent->appendRow(item);

		if (entry.isGroup) {
			AppendEntries(item, entry.children);
		}
	}
}

void SourcesPanel::UpdateTexts(QStandardItem *parent, const std::vector<Entry> &entries)
{
	for (int row = 0; row < static_cast<int>(entries.size()) && row < parent->rowCount(); row++) {
		QStandardItem *item = parent->child(row);
		if (item->text() != entries[row].text) {
			item->setText(entries[row].text);
		}
		UpdateTexts(item, entries[row].children);
	}
}

void SourcesPanel::UpdateTitle()
{
	OBSSource scene = OBSGetStrongRef(currentScene);
	QString title = scene ? QTStr("Accessibility.Panel.SourcesIn").arg(QT_UTF8(obs_source_get_name(scene)))
			      : QTStr("Basic.Main.Sources");
	setTitle(title);
	view->setAccessibleName(title);
}

void SourcesPanel::Refresh()
{
	OBSScene scene = main->GetCurrentScene();
	obs_source_t *sceneSource = obs_scene_get_source(scene);
	bool sceneChanged = OBSGetStrongRef(currentScene).Get() != sceneSource;

	std::vector<Entry> entries = scene ? CollectEntries(scene) : std::vector<Entry>();
	QStringList keys;
	CollectKeys(entries, keys);

	if (sceneChanged) {
		layoutMode->Exit();
	}

	if (!sceneChanged && keys == structureKeys) {
		/* Only names or states changed: keep the screen reader's
		 * position by updating the texts in place. */
		UpdateTexts(model->invisibleRootItem(), entries);
		UpdateTitle();
		return;
	}

	QString currentKey = view->currentIndex().isValid() ? ItemKey(view->currentIndex()) : QString();

	if (!sceneChanged) {
		for (int row = 0; row < model->rowCount(); row++) {
			QModelIndex index = model->index(row, 0);
			if (index.data(isGroupRole).toBool()) {
				QString key = ItemKey(index);
				if (view->isExpanded(index)) {
					collapsedGroups.remove(key);
				} else {
					collapsedGroups.insert(key);
				}
			}
		}
	}

	model->clear();
	AppendEntries(model->invisibleRootItem(), entries);

	for (int row = 0; row < model->rowCount(); row++) {
		QModelIndex index = model->index(row, 0);
		if (index.data(isGroupRole).toBool() && !collapsedGroups.contains(ItemKey(index))) {
			view->expand(index);
		}
	}

	structureKeys = keys;
	ConnectSceneSignals();
	UpdateTitle();

	QModelIndex restore;
	if (!currentKey.isEmpty() && !sceneChanged) {
		QModelIndexList matches = model->match(model->index(0, 0), sceneUuidRole,
						       currentKey.section('/', 0, 0), -1,
						       Qt::MatchExactly | Qt::MatchRecursive);
		for (const QModelIndex &match : matches) {
			if (ItemKey(match) == currentKey) {
				restore = match;
				break;
			}
		}
	}
	if (!restore.isValid() && model->rowCount() > 0) {
		restore = model->index(0, 0);
	}
	if (restore.isValid()) {
		view->selectionModel()->setCurrentIndex(restore, QItemSelectionModel::ClearAndSelect);
	}
}

OBSSceneItem SourcesPanel::GetSelectedItem() const
{
	QModelIndex index = view->currentIndex();
	if (!index.isValid()) {
		return nullptr;
	}

	return FindSceneItem(index.data(sceneUuidRole).toString(), index.data(itemIdRole).toLongLong());
}

std::vector<OBSSceneItem> SourcesPanel::GetSelectedItems() const
{
	std::vector<OBSSceneItem> items;
	for (const QModelIndex &index : view->selectionModel()->selectedIndexes()) {
		QString sceneUuid = index.data(sceneUuidRole).toString();
		if (OBSSceneItem item = FindSceneItem(sceneUuid, index.data(itemIdRole).toLongLong())) {
			items.push_back(item);
		}
	}
	return items;
}

/* Selects the selected items in the scene too, or the current item when
 * nothing is selected, so the main window's actions act on them. */
void SourcesPanel::SyncSelection()
{
	std::vector<OBSSceneItem> selected = GetSelectedItems();
	if (selected.empty()) {
		if (OBSSceneItem item = GetSelectedItem()) {
			selected.push_back(item);
		}
	}
	if (selected.empty()) {
		return;
	}

	std::vector<obs_sceneitem_t *> items(selected.begin(), selected.end());
	SelectOnly(main->GetCurrentScene(), items);
}

void SourcesPanel::OpenProperties()
{
	if (OBSSceneItem item = GetSelectedItem()) {
		main->CreatePropertiesWindow(obs_sceneitem_get_source(item));
	}
}

void SourcesPanel::OpenFilters()
{
	if (OBSSceneItem item = GetSelectedItem()) {
		main->CreateFiltersWindow(obs_sceneitem_get_source(item));
	}
}

void SourcesPanel::OpenTransform()
{
	if (OBSSceneItem item = GetSelectedItem()) {
		main->CreateEditTransformWindow(item);
	}
}

void SourcesPanel::OpenInteraction()
{
	OBSSceneItem item = GetSelectedItem();
	obs_source_t *source = obs_sceneitem_get_source(item);
	if (source && (obs_source_get_output_flags(source) & OBS_SOURCE_INTERACTION) != 0) {
		main->CreateInteractionWindow(source);
	}
}

void SourcesPanel::RenameSelected()
{
	if (OBSSceneItem item = GetSelectedItem()) {
		RenameSourceWithUndo(window(), main, obs_sceneitem_get_source(item));
	}
}

void SourcesPanel::ToggleVisibility()
{
	OBSSceneItem item = GetSelectedItem();
	if (!item) {
		return;
	}

	bool visible = !obs_sceneitem_visible(item);
	SetItemVisibleWithUndo(main, item, visible);

	const char *name = obs_source_get_name(obs_sceneitem_get_source(item));
	Announce(QTStr(visible ? "Accessibility.Announce.ItemShown" : "Accessibility.Announce.ItemHidden").arg(name));
}

void SourcesPanel::ToggleLock()
{
	OBSSceneItem item = GetSelectedItem();
	if (!item) {
		return;
	}

	bool locked = !obs_sceneitem_locked(item);
	obs_sceneitem_set_locked(item, locked);

	const char *name = obs_source_get_name(obs_sceneitem_get_source(item));
	Announce(QTStr(locked ? "Accessibility.Announce.ItemLocked" : "Accessibility.Announce.ItemUnlocked").arg(name));
}

void SourcesPanel::RemoveSelected()
{
	if (GetSelectedItem()) {
		SyncSelection();
		OBSAccessibleBridge::RemoveSelectedSources(main);
	}
}

void SourcesPanel::MoveSelected(obs_order_movement movement)
{
	if (GetSelectedItem()) {
		SyncSelection();
		OBSAccessibleBridge::MoveSelectedSources(main, movement);
	}
}

} // namespace OBS::Accessibility
