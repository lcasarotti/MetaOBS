#include "ScenesPanel.hpp"
#include "AccessibleBridge.hpp"
#include "AccessibleCommon.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <qt-wrappers.hpp>

#include <QAction>
#include <QContextMenuEvent>
#include <QListView>
#include <QListWidget>
#include <QMenu>
#include <QStandardItemModel>
#include <QTimer>
#include <QVBoxLayout>

#include "moc_ScenesPanel.cpp"

namespace OBS::Accessibility {

namespace {
constexpr int uuidRole = Qt::UserRole;

QAction *CreateAction(QWidget *owner, const QString &text, const QKeySequence &shortcut = {})
{
	QAction *action = new QAction(text, owner);
	if (!shortcut.isEmpty()) {
		action->setShortcut(shortcut);
		action->setShortcutContext(Qt::WidgetShortcut);
		owner->addAction(action);
	}
	return action;
}

/* Opens the scene menu next to the current item, also from the keyboard. */
class SceneListView : public QListView {
public:
	using QListView::QListView;
	ScenesPanel *panel = nullptr;

protected:
	void contextMenuEvent(QContextMenuEvent *event) override
	{
		QPoint position = event->reason() == QContextMenuEvent::Keyboard
					  ? viewport()->mapToGlobal(visualRect(currentIndex()).center())
					  : event->globalPos();
		panel->Menu()->exec(position);
	}
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
	view->setSelectionMode(QAbstractItemView::SingleSelection);
	view->setEditTriggers(QAbstractItemView::NoEditTriggers);

	model = new QStandardItemModel(this);
	view->setModel(model);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addWidget(view);

	switchAction = CreateAction(view, QTStr("Accessibility.Scenes.Switch"));
	addAction = CreateAction(view, QTStr("Accessibility.Scenes.Add"));
	renameAction = CreateAction(view, QTStr("Rename"), QKeySequence(Qt::Key_F2));
	removeAction = CreateAction(view, QTStr("Remove"), QKeySequence(QKeySequence::Delete));
	filtersAction = CreateAction(view, QTStr("Filters"));

	connect(switchAction, &QAction::triggered, this, &ScenesPanel::SwitchToSelectedScene);
	connect(addAction, &QAction::triggered, this, [this]() { OBSAccessibleBridge::AddScene(main); });
	connect(renameAction, &QAction::triggered, this, &ScenesPanel::RenameSelectedScene);
	connect(removeAction, &QAction::triggered, this, &ScenesPanel::RemoveSelectedScene);
	connect(filtersAction, &QAction::triggered, this, &ScenesPanel::OpenSelectedSceneFilters);

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
	menu->addSeparator();
	menu->addAction(addAction);
	menu->addAction(renameAction);
	menu->addAction(removeAction);
	menu->addSeparator();
	menu->addAction(filtersAction);
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
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
		collectionLoading = false;
		ScheduleRefresh();
		break;
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
		ScheduleRefresh();
		if (!collectionLoading) {
			OBSSourceAutoRelease scene = obs_frontend_get_current_scene();
			if (scene) {
				Announce(QTStr("Accessibility.Announce.SceneLive").arg(obs_source_get_name(scene)));
			}
		}
		break;
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
		ScheduleRefresh();
		if (!collectionLoading && obs_frontend_preview_program_mode_active()) {
			OBSSourceAutoRelease scene = obs_frontend_get_current_preview_scene();
			if (scene) {
				Announce(QTStr("Accessibility.Announce.ScenePreview").arg(obs_source_get_name(scene)));
			}
		}
		break;
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
	refreshTimer->start();
}

void ScenesPanel::Refresh()
{
	QListWidget *sceneList = OBSAccessibleBridge::SceneList(main);

	OBSSourceAutoRelease program = obs_frontend_get_current_scene();
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

} // namespace OBS::Accessibility
