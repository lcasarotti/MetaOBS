#include "ScenesPanel.hpp"
#include "AccessibleBridge.hpp"
#include "AccessibleCommon.hpp"
#include "MidiWidgetProperties.hpp"
#include "TransitionDialogs.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>
#include <widgets/OBSProjector.hpp>

#include <qt-wrappers.hpp>

#include <QAction>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QStandardItemModel>
#include <QTimer>
#include <QVBoxLayout>

#include "moc_ScenesPanel.cpp"

namespace OBS::Accessibility {

namespace {
/* The screen reader may speak the new state of the focused scene before an
 * announcement sent at the same moment, so the list is updated a little
 * after the program change is announced. */
constexpr int announcedRefreshDelayMs = 250;

constexpr int uuidRole = Qt::UserRole;

/* Opens the scene menu next to the current item, also from the keyboard. */
class SceneListView : public QListView {
public:
	using QListView::QListView;
	ScenesPanel *panel = nullptr;

protected:
	void contextMenuEvent(QContextMenuEvent *event) override { ExecContextMenu(this, panel->Menu(), event); }
};
} // namespace

ScenesPanel::ScenesPanel(OBSBasic *main_, QWidget *parent)
	: QGroupBox(QTStr("Basic.Main.Scenes"), parent),
	  main(main_)
{
	SceneListView *listView = new SceneListView(this);
	listView->panel = this;
	view = listView;
	view->setAccessibleName(QTStr("Basic.Main.Scenes"));
	view->setProperty(Midi::targetProperty, "scenes");
	view->setSelectionMode(QAbstractItemView::SingleSelection);
	view->setEditTriggers(QAbstractItemView::NoEditTriggers);

	model = new QStandardItemModel(this);
	view->setModel(model);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addWidget(view);

	switchAction = CreatePanelAction(view, QTStr("Accessibility.Scenes.Switch"));
	goLiveAction =
		CreatePanelAction(view, QTStr("Accessibility.Scenes.GoLive"),
				  {QKeySequence(Qt::CTRL | Qt::Key_Return), QKeySequence(Qt::CTRL | Qt::Key_Enter)});
	addAction = CreatePanelAction(view, QTStr("Accessibility.Scenes.Add"));
	renameAction = CreatePanelAction(view, QTStr("Rename"), {QKeySequence(Qt::Key_F2)});
	removeAction = CreatePanelAction(view, QTStr("Remove"), {QKeySequence(QKeySequence::Delete)});
	filtersAction = CreatePanelAction(view, QTStr("Filters"));
	duplicateAction = CreatePanelAction(view, QTStr("Duplicate"), {QKeySequence(Qt::CTRL | Qt::Key_D)});
	moveUpAction = CreatePanelAction(view, QTStr("MoveUp"), {QKeySequence(Qt::ALT | Qt::Key_Up)});
	moveDownAction = CreatePanelAction(view, QTStr("MoveDown"), {QKeySequence(Qt::ALT | Qt::Key_Down)});
	moveToTopAction = CreatePanelAction(view, QTStr("Accessibility.Sources.MoveToTop"),
					    {QKeySequence(Qt::ALT | Qt::Key_Home)});
	moveToBottomAction = CreatePanelAction(view, QTStr("Accessibility.Sources.MoveToBottom"),
					       {QKeySequence(Qt::ALT | Qt::Key_End)});
	copyFiltersAction = CreatePanelAction(view, QTStr("Copy.Filters"));
	pasteFiltersAction = CreatePanelAction(view, QTStr("Paste.Filters"));
	screenshotAction = CreatePanelAction(view, QTStr("Screenshot.Scene"));
	transitionOverrideAction = CreatePanelAction(view, QTStr("Accessibility.Scenes.TransitionOverride"));
	multiviewAction = CreatePanelAction(view, QTStr("ShowInMultiview"));
	multiviewAction->setCheckable(true);

	connect(switchAction, &QAction::triggered, this, &ScenesPanel::SwitchToSelectedScene);
	connect(goLiveAction, &QAction::triggered, this, &ScenesPanel::SendSelectedSceneLive);
	connect(addAction, &QAction::triggered, this, [this]() { OBSAccessibleBridge::AddScene(main); });
	connect(renameAction, &QAction::triggered, this, &ScenesPanel::RenameSelectedScene);
	connect(removeAction, &QAction::triggered, this, &ScenesPanel::RemoveSelectedScene);
	connect(filtersAction, &QAction::triggered, this, &ScenesPanel::OpenSelectedSceneFilters);
	connect(duplicateAction, &QAction::triggered, this, &ScenesPanel::DuplicateSelectedScene);
	connect(moveUpAction, &QAction::triggered, this, [this]() { MoveSelectedScene(-1, false); });
	connect(moveDownAction, &QAction::triggered, this, [this]() { MoveSelectedScene(1, false); });
	connect(moveToTopAction, &QAction::triggered, this, [this]() { MoveSelectedScene(-1, true); });
	connect(moveToBottomAction, &QAction::triggered, this, [this]() { MoveSelectedScene(1, true); });
	connect(copyFiltersAction, &QAction::triggered, this, [this]() {
		if (OBSSource source = GetSelectedScene()) {
			OBSAccessibleBridge::CopyFilters(main, source);
		}
	});
	connect(pasteFiltersAction, &QAction::triggered, this, [this]() {
		if (OBSSource source = GetSelectedScene()) {
			OBSAccessibleBridge::PasteFilters(main, source);
		}
	});
	connect(screenshotAction, &QAction::triggered, this, [this]() {
		if (OBSSource source = GetSelectedScene()) {
			OBSAccessibleBridge::Screenshot(main, source);
		}
	});
	connect(transitionOverrideAction, &QAction::triggered, this, [this]() {
		if (OBSSource source = GetSelectedScene()) {
			EditSceneTransitionOverride(source, window());
		}
	});
	connect(multiviewAction, &QAction::triggered, this, [this](bool checked) {
		if (OBSSource source = GetSelectedScene()) {
			OBSDataAutoRelease settings = obs_source_get_private_settings(source);
			obs_data_set_bool(settings, "show_in_multiview", checked);
			OBSProjector::UpdateMultiviewProjectors();
		}
	});

	CreateMenu();

	/* Enter and double-click switch to the scene. */
	connect(view, &QListView::activated, this, &ScenesPanel::SwitchToSelectedScene);

	refreshTimer = new QTimer(this);
	refreshTimer->setSingleShot(true);
	refreshTimer->setInterval(0);
	connect(refreshTimer, &QTimer::timeout, this, &ScenesPanel::Refresh);

	/* The main window's scene list holds the scene order, and reordering
	 * does not send a frontend event. */
	QAbstractItemModel *sceneOrder = OBSAccessibleBridge::SceneList(main)->model();
	connect(sceneOrder, &QAbstractItemModel::rowsInserted, this, &ScenesPanel::ScheduleRefresh);
	connect(sceneOrder, &QAbstractItemModel::rowsRemoved, this, &ScenesPanel::ScheduleRefresh);
	connect(sceneOrder, &QAbstractItemModel::rowsMoved, this, &ScenesPanel::ScheduleRefresh);
	connect(sceneOrder, &QAbstractItemModel::dataChanged, this, &ScenesPanel::ScheduleRefresh);
	connect(sceneOrder, &QAbstractItemModel::modelReset, this, &ScenesPanel::ScheduleRefresh);

	obs_frontend_add_event_callback(OnFrontendEvent, this);

	OBSSourceAutoRelease program = obs_frontend_get_current_scene();
	programScene = OBSGetWeakRef(program);

	Refresh();
}

ScenesPanel::~ScenesPanel()
{
	obs_frontend_remove_event_callback(OnFrontendEvent, this);
}

void ScenesPanel::CreateMenu()
{
	/* A single menu serves the menu bar and the context menu: Qt caches
	 * each action's accessible interface with the first menu that shows
	 * it, so an action in a second, temporary menu becomes invisible to
	 * screen readers in both. */
	menu = new QMenu(QTStr("Accessibility.Menu.Scene"), this);
	menu->addAction(switchAction);
	menu->addAction(goLiveAction);
	menu->addSeparator();
	menu->addAction(addAction);
	menu->addAction(renameAction);
	menu->addAction(removeAction);
	menu->addAction(duplicateAction);
	menu->addSeparator();

	orderMenu = menu->addMenu(QTStr("Basic.MainMenu.Edit.Order"));
	orderMenu->addAction(moveUpAction);
	orderMenu->addAction(moveDownAction);
	orderMenu->addAction(moveToTopAction);
	orderMenu->addAction(moveToBottomAction);
	menu->addSeparator();

	menu->addAction(filtersAction);
	menu->addAction(copyFiltersAction);
	menu->addAction(pasteFiltersAction);
	menu->addSeparator();

	projectorMenu = menu->addMenu(QTStr("Projector.Open.Scene"));
	connect(projectorMenu, &QMenu::aboutToShow, this, [this]() {
		OBSSource source = GetSelectedScene();
		FillProjectorMenu(projectorMenu, "Scene", QT_UTF8(obs_source_get_name(source)));
	});
	menu->addAction(screenshotAction);
	menu->addSeparator();

	menu->addAction(transitionOverrideAction);
	menu->addAction(multiviewAction);

	connect(menu, &QMenu::aboutToShow, this, &ScenesPanel::UpdateMenuState);
}

/* The items that depend on the selected scene are updated when the menu
 * opens, from the menu bar or as a context menu. Items with shortcuts stay
 * enabled, since their shortcuts work without the menu, and do nothing when
 * no scene is selected. */
void ScenesPanel::UpdateMenuState()
{
	OBSSource source = GetSelectedScene();
	bool selected = source != nullptr;

	projectorMenu->setEnabled(selected);
	copyFiltersAction->setEnabled(selected && obs_source_filter_count(source) > 0);
	pasteFiltersAction->setEnabled(selected && OBSAccessibleBridge::HasCopiedFilters(main));
	multiviewAction->setEnabled(selected);

	if (selected) {
		OBSDataAutoRelease settings = obs_source_get_private_settings(source);
		obs_data_set_default_bool(settings, "show_in_multiview", true);
		multiviewAction->setChecked(obs_data_get_bool(settings, "show_in_multiview"));
	}
}

void ScenesPanel::OnFrontendEvent(enum obs_frontend_event event, void *data)
{
	static_cast<ScenesPanel *>(data)->HandleFrontendEvent(event);
}

void ScenesPanel::HandleFrontendEvent(enum obs_frontend_event event)
{
	switch (event) {
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGING:
		collectionLoading = true;
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED: {
		collectionLoading = false;
		OBSSourceAutoRelease program = obs_frontend_get_current_scene();
		programScene = OBSGetWeakRef(program);
		ScheduleRefresh();
		break;
	}
	case OBS_FRONTEND_EVENT_SCENE_CHANGED: {
		ScheduleRefresh();
		OBSSourceAutoRelease scene = obs_frontend_get_current_scene();
		OBSSource previous = OBSGetStrongRef(programScene);
		programScene = OBSGetWeakRef(scene);

		if (collectionLoading || !scene) {
			break;
		}
		if (previous && previous.Get() != scene.Get()) {
			QString from = QT_UTF8(obs_source_get_name(previous));
			QString to = QT_UTF8(obs_source_get_name(scene));
			Announce(QTStr("Accessibility.Announce.SceneTransition").arg(from, to));
		} else {
			Announce(QTStr("Accessibility.Announce.SceneLive").arg(obs_source_get_name(scene)));
		}
		refreshTimer->start(announcedRefreshDelayMs);
		break;
	}
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED: {
		ScheduleRefresh();
		if (collectionLoading || sendingLive || !obs_frontend_preview_program_mode_active()) {
			break;
		}

		OBSSourceAutoRelease scene = obs_frontend_get_current_preview_scene();
		if (!scene) {
			break;
		}

		/* When scenes are swapped, the old program scene goes back to the
		 * preview at the end of the transition, before the program change
		 * is reported. The transition announcement covers it. */
		OBSSource announcedProgram = OBSGetStrongRef(programScene);
		OBSSourceAutoRelease program = obs_frontend_get_current_scene();
		bool swapped = scene.Get() == announcedProgram.Get() && program.Get() != announcedProgram.Get();
		if (!swapped) {
			Announce(QTStr("Accessibility.Announce.ScenePreview").arg(obs_source_get_name(scene)));
		}
		break;
	}
	case OBS_FRONTEND_EVENT_SCENE_LIST_CHANGED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
		ScheduleRefresh();
		break;
	default:
		break;
	}
}

void ScenesPanel::ScheduleRefresh()
{
	/* Do not cut short a refresh delayed after an announcement. */
	if (refreshTimer->isActive() && refreshTimer->remainingTime() > 0) {
		return;
	}
	refreshTimer->start(0);
}

void ScenesPanel::Refresh()
{
	QListWidget *sceneList = OBSAccessibleBridge::SceneList(main);

	/* Without Studio Mode, the frontend reports the new scene as soon as a
	 * transition starts. Mark it live when the transition ends, together
	 * with its announcement, so the screen reader does not read the new
	 * state before the transition. */
	OBSSource program = OBSGetStrongRef(programScene);
	OBSSourceAutoRelease preview = obs_frontend_preview_program_mode_active()
					       ? obs_frontend_get_current_preview_scene()
					       : nullptr;

	QStringList uuids;
	QStringList texts;
	int activeRow = -1;

	for (int row = 0; row < sceneList->count(); row++) {
		OBSScene scene = GetOBSRef<OBSScene>(sceneList->item(row));
		obs_source_t *source = obs_scene_get_source(scene);
		if (!source) {
			continue;
		}

		QString text = QT_UTF8(obs_source_get_name(source));
		if (source == program.Get()) {
			text += ", " + QTStr("Accessibility.State.Live");
		}
		if (preview && source == preview.Get()) {
			text += ", " + QTStr("Accessibility.State.Preview");
		}

		if (source == (preview ? preview.Get() : program.Get())) {
			activeRow = uuids.size();
		}

		uuids.append(QT_UTF8(obs_source_get_uuid(source)));
		texts.append(text);
	}

	bool sameScenes = model->rowCount() == uuids.size();
	for (int row = 0; sameScenes && row < uuids.size(); row++) {
		sameScenes = model->item(row)->data(uuidRole).toString() == uuids[row];
	}

	/* Update the texts in place when only names or states changed, so the
	 * screen reader's position in the list is kept. */
	if (sameScenes) {
		for (int row = 0; row < uuids.size(); row++) {
			if (model->item(row)->text() != texts[row]) {
				model->item(row)->setText(texts[row]);
			}
		}
		return;
	}

	QString currentUuid = view->currentIndex().data(uuidRole).toString();

	model->clear();
	for (int row = 0; row < uuids.size(); row++) {
		QStandardItem *item = new QStandardItem(texts[row]);
		item->setData(uuids[row], uuidRole);
		model->appendRow(item);
	}

	int currentRow = currentUuid.isEmpty() ? -1 : uuids.indexOf(currentUuid);
	if (currentRow < 0) {
		currentRow = activeRow;
	}
	if (currentRow >= 0) {
		view->selectionModel()->setCurrentIndex(model->index(currentRow, 0),
							QItemSelectionModel::ClearAndSelect);
	}
}

OBSSource ScenesPanel::GetSelectedScene() const
{
	QString uuid = view->currentIndex().data(uuidRole).toString();
	if (uuid.isEmpty()) {
		return nullptr;
	}

	OBSSourceAutoRelease source = obs_get_source_by_uuid(QT_TO_UTF8(uuid));
	return source.Get();
}

void ScenesPanel::SwitchToSelectedScene()
{
	OBSSource source = GetSelectedScene();
	if (source) {
		main->SetCurrentScene(source);
	}
}

/* In Studio Mode, makes the selected scene the preview and transitions to
 * it; otherwise switches to it, as Enter does. */
void ScenesPanel::SendSelectedSceneLive()
{
	OBSSource source = GetSelectedScene();
	if (!source) {
		return;
	}

	/* The scene only passes through the preview on its way to the
	 * program, so the preview change is not announced. */
	sendingLive = true;
	main->SetCurrentScene(source);
	sendingLive = false;
	if (obs_frontend_preview_program_mode_active()) {
		obs_frontend_preview_program_trigger_transition();
	}
}

void ScenesPanel::RenameSelectedScene()
{
	OBSSource source = GetSelectedScene();
	if (source) {
		RenameSourceWithUndo(window(), main, source);
	}
}

void ScenesPanel::RemoveSelectedScene()
{
	OBSSource source = GetSelectedScene();
	if (source) {
		OBSAccessibleBridge::RemoveScene(main, obs_scene_from_source(source));
	}
}

void ScenesPanel::OpenSelectedSceneFilters()
{
	OBSSource source = GetSelectedScene();
	if (source) {
		main->CreateFiltersWindow(source);
	}
}

void ScenesPanel::DuplicateSelectedScene()
{
	OBSSource source = GetSelectedScene();
	if (!source) {
		return;
	}

	OBSSource copy = DuplicateSceneWithUndo(window(), main, source);
	if (!copy) {
		return;
	}

	/* Move to the copy in the list once the main window has added it. */
	QString uuid = QT_UTF8(obs_source_get_uuid(copy));
	QTimer::singleShot(0, this, [this, uuid]() {
		Refresh();
		QModelIndexList matches = model->match(model->index(0, 0), uuidRole, uuid, 1, Qt::MatchExactly);
		if (!matches.isEmpty()) {
			view->selectionModel()->setCurrentIndex(matches.first(), QItemSelectionModel::ClearAndSelect);
		}
	});
}

void ScenesPanel::MoveSelectedScene(int offset, bool toEnd)
{
	OBSSource source = GetSelectedScene();
	int row = view->currentIndex().row();
	int count = model->rowCount();
	if (!source || row < 0) {
		return;
	}

	int target = toEnd ? (offset < 0 ? 0 : count - 1) : row + offset;
	if (target < 0 || target >= count || target == row) {
		return;
	}

	if (OBSAccessibleBridge::MoveScene(main, obs_scene_from_source(source), target)) {
		Refresh();
		Announce(QTStr("Accessibility.Announce.MovedTo").arg(target + 1).arg(count));
	}
}

} // namespace OBS::Accessibility
