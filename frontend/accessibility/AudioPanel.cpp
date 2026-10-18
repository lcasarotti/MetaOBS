#include "AudioPanel.hpp"
#include "AccessibleCommon.hpp"
#include "AccessibleFormat.hpp"
#include "AudioActions.hpp"
#include "AudioPropertiesDialog.hpp"
#include "MidiWidgetProperties.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <qt-wrappers.hpp>
#include <util/platform.h>

#include <QAction>
#include <QListView>
#include <QMenu>
#include <QSet>
#include <QStandardItemModel>
#include <QTimer>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>

#include "moc_AudioPanel.cpp"

namespace OBS::Accessibility {

namespace {
constexpr int uuidRole = Qt::UserRole;

constexpr float smallVolumeStep = 1.0f;
constexpr float largeVolumeStep = 5.0f;
/* Lowest level the volume keys reach before silence, and the highest, which
 * matches the mixer's sliders. */
constexpr float volumeKeysFloorDb = -60.0f;
constexpr float volumeKeysMaximumDb = 0.0f;

/* The levels readout reports the highest peak of this period, which says more
 * than the instant value about speech or music. */
constexpr uint64_t peakHoldNs = 2000000000ULL;
constexpr float silenceDb = -60.0f;

constexpr int globalAudioChannels = 6;

constexpr const char *globalSignalNames[] = {"source_create",          "source_destroy",
					     "source_remove",          "source_rename",
					     "source_activate",        "source_deactivate",
					     "source_audio_activate",  "source_audio_deactivate",
					     "source_volume"};
constexpr const char *sourceSignalNames[] = {"mute", "monitor", "update_flags"};

class AudioListView : public QListView {
public:
	using QListView::QListView;
	AudioPanel *panel = nullptr;

protected:
	void contextMenuEvent(QContextMenuEvent *event) override { ExecContextMenu(this, panel->Menu(), event); }
};

QSet<QString> GetStudioModePreviewedSources()
{
	QSet<QString> uuids;
	if (!obs_frontend_preview_program_mode_active()) {
		return uuids;
	}

	OBSSourceAutoRelease previewScene = obs_frontend_get_current_preview_scene();
	obs_scene_t *scene = obs_scene_from_source(previewScene);
	if (!scene) {
		return uuids;
	}

	auto collect = [](obs_scene_t *, obs_sceneitem_t *item, void *param) {
		obs_source_t *source = obs_sceneitem_get_source(item);
		if (obs_sceneitem_visible(item) && (obs_source_get_output_flags(source) & OBS_SOURCE_AUDIO) != 0) {
			static_cast<QSet<QString> *>(param)->insert(QT_UTF8(obs_source_get_uuid(source)));
		}
		return true;
	};
	obs_scene_enum_items(scene, collect, &uuids);

	return uuids;
}

bool GetPinned(obs_source_t *source)
{
	OBSDataAutoRelease settings = obs_source_get_private_settings(source);
	return obs_data_get_bool(settings, "mixer_pinned");
}

float LoudestChannel(const float values[MAX_AUDIO_CHANNELS])
{
	float loudest = minimumVolumeDb;
	for (int channel = 0; channel < MAX_AUDIO_CHANNELS; channel++) {
		if (std::isfinite(values[channel])) {
			loudest = std::max(loudest, values[channel]);
		}
	}
	return loudest;
}
} // namespace

AudioPanel::AudioPanel(OBSBasic *main_, QWidget *parent)
	: QGroupBox(QTStr("Mixer"), parent),
	  main(main_)
{
	AudioListView *listView = new AudioListView(this);
	listView->panel = this;
	view = listView;
	view->setAccessibleName(QTStr("Mixer"));
	view->setProperty(Midi::targetProperty, "audioSources");
	view->setSelectionMode(QAbstractItemView::SingleSelection);
	view->setEditTriggers(QAbstractItemView::NoEditTriggers);

	model = new QStandardItemModel(this);
	view->setModel(model);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addWidget(view);

	volumeUpAction = CreatePanelAction(view, QTStr("Accessibility.Audio.VolumeUp"), {QKeySequence(Qt::Key_Plus)});
	volumeDownAction =
		CreatePanelAction(view, QTStr("Accessibility.Audio.VolumeDown"), {QKeySequence(Qt::Key_Minus)});
	volumeUpLargeAction = CreatePanelAction(view, QTStr("Accessibility.Audio.VolumeUpLarge"),
						{QKeySequence(Qt::CTRL | Qt::Key_Plus)});
	volumeDownLargeAction = CreatePanelAction(view, QTStr("Accessibility.Audio.VolumeDownLarge"),
						  {QKeySequence(Qt::CTRL | Qt::Key_Minus)});
	muteAction = CreatePanelAction(view, QTStr("Accessibility.Audio.ToggleMute"), {QKeySequence(Qt::Key_M)});
	monitoringAction =
		CreatePanelAction(view, QTStr("Accessibility.Audio.ToggleMonitoring"), {QKeySequence(Qt::Key_K)});
	readLevelsAction = CreatePanelAction(view, QTStr("Accessibility.Audio.ReadLevels"), {QKeySequence(Qt::Key_L)});
	audioPropertiesAction = CreatePanelAction(view, QTStr("Accessibility.Audio.Properties"));
	filtersAction = CreatePanelAction(view, QTStr("Filters"), {QKeySequence(Qt::CTRL | Qt::Key_F)});
	propertiesAction = CreatePanelAction(view, QTStr("Properties"));
	advancedAudioAction = CreatePanelAction(view, QTStr("Basic.MainMenu.Edit.AdvAudio"));

	connect(volumeUpAction, &QAction::triggered, this, [this]() { ChangeVolume(smallVolumeStep); });
	connect(volumeDownAction, &QAction::triggered, this, [this]() { ChangeVolume(-smallVolumeStep); });
	connect(volumeUpLargeAction, &QAction::triggered, this, [this]() { ChangeVolume(largeVolumeStep); });
	connect(volumeDownLargeAction, &QAction::triggered, this, [this]() { ChangeVolume(-largeVolumeStep); });
	connect(muteAction, &QAction::triggered, this, &AudioPanel::ToggleMute);
	connect(monitoringAction, &QAction::triggered, this, &AudioPanel::ToggleMonitoring);
	connect(readLevelsAction, &QAction::triggered, this, &AudioPanel::ReadLevels);
	connect(audioPropertiesAction, &QAction::triggered, this, &AudioPanel::OpenAudioProperties);
	connect(filtersAction, &QAction::triggered, this, &AudioPanel::OpenFilters);
	connect(propertiesAction, &QAction::triggered, this, &AudioPanel::OpenProperties);
	connect(advancedAudioAction, &QAction::triggered, this,
		[this]() { main->on_actionAdvAudioProperties_triggered(); });

	CreateMenu();

	/* Enter and double-click open the source's audio properties. */
	connect(view, &QListView::activated, this, &AudioPanel::OpenAudioProperties);
	connect(view->selectionModel(), &QItemSelectionModel::currentChanged, this, &AudioPanel::AttachMeter);

	refreshTimer = new QTimer(this);
	refreshTimer->setSingleShot(true);
	refreshTimer->setInterval(50);
	connect(refreshTimer, &QTimer::timeout, this, &AudioPanel::Refresh);

	/* Hiding, pinning and the mixer's display options change which sources
	 * the mixer shows. */
	connect(main, &OBSBasic::mixerStatusChanged, this, &AudioPanel::ScheduleRefresh);
	connect(main, &OBSBasic::userSettingChanged, this, &AudioPanel::ScheduleRefresh);

	volmeter = obs_volmeter_create(OBS_FADER_LOG);
	obs_volmeter_add_callback(volmeter, OnLevels, this);
	if (config_get_uint(main->Config(), "Audio", "PeakMeterType") == 1) {
		obs_volmeter_set_peak_meter_type(volmeter, TRUE_PEAK_METER);
	}
	ResetLevels();

	ConnectGlobalSignals();
	obs_frontend_add_event_callback(OnFrontendEvent, this);

	Refresh();
}

AudioPanel::~AudioPanel()
{
	obs_frontend_remove_event_callback(OnFrontendEvent, this);
	DisconnectAll();
}

void AudioPanel::CreateMenu()
{
	menu = new QMenu(QTStr("Accessibility.Menu.Audio"), this);
	menu->addAction(volumeUpAction);
	menu->addAction(volumeDownAction);
	menu->addAction(volumeUpLargeAction);
	menu->addAction(volumeDownLargeAction);
	menu->addAction(muteAction);
	menu->addAction(monitoringAction);
	menu->addAction(readLevelsAction);
	menu->addSeparator();
	menu->addAction(audioPropertiesAction);
	menu->addAction(filtersAction);
	menu->addAction(propertiesAction);
	menu->addSeparator();
	menu->addAction(advancedAudioAction);
}

void AudioPanel::ConnectGlobalSignals()
{
	signal_handler_t *handler = obs_get_signal_handler();
	for (const char *signal : globalSignalNames) {
		globalSignals.emplace_back(handler, signal, OnSourceSignal, this);
	}
}

/* Also called when OBS exits, because the window may outlive the signal
 * handlers. */
void AudioPanel::DisconnectAll()
{
	globalSignals.clear();
	sourceSignals.clear();

	if (volmeter) {
		obs_volmeter_remove_callback(volmeter, OnLevels, this);
		obs_volmeter_destroy(volmeter);
		volmeter = nullptr;
	}
}

void AudioPanel::OnFrontendEvent(enum obs_frontend_event event, void *data)
{
	auto panel = static_cast<AudioPanel *>(data);

	switch (event) {
	case OBS_FRONTEND_EVENT_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_PREVIEW_SCENE_CHANGED:
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CHANGED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_ENABLED:
	case OBS_FRONTEND_EVENT_STUDIO_MODE_DISABLED:
		panel->ScheduleRefresh();
		break;
	case OBS_FRONTEND_EVENT_SCENE_COLLECTION_CLEANUP:
		panel->sourceSignals.clear();
		panel->listedUuids.clear();
		if (panel->volmeter) {
			obs_volmeter_detach_source(panel->volmeter);
		}
		panel->meterSource = nullptr;
		break;
	case OBS_FRONTEND_EVENT_EXIT:
		panel->DisconnectAll();
		break;
	default:
		break;
	}
}

/* Source signals arrive on any thread. */
void AudioPanel::OnSourceSignal(void *data, calldata_t *)
{
	auto panel = static_cast<AudioPanel *>(data);
	QMetaObject::invokeMethod(panel, [panel]() { panel->ScheduleRefresh(); }, Qt::QueuedConnection);
}

/* Runs on the audio thread. */
void AudioPanel::OnLevels(void *data, const float magnitude[MAX_AUDIO_CHANNELS], const float peak[MAX_AUDIO_CHANNELS],
			  const float[MAX_AUDIO_CHANNELS])
{
	auto panel = static_cast<AudioPanel *>(data);
	float loudestPeak = LoudestChannel(peak);
	uint64_t now = os_gettime_ns();

	std::lock_guard lock(panel->levelsMutex);
	Levels &levels = panel->levels;
	levels.peak = loudestPeak;
	levels.magnitude = LoudestChannel(magnitude);

	if (loudestPeak >= levels.heldPeak || now - levels.heldPeakTime > peakHoldNs) {
		levels.heldPeak = loudestPeak;
		levels.heldPeakTime = now;
	}
}

void AudioPanel::ScheduleRefresh()
{
	refreshTimer->start();
}

std::vector<AudioPanel::Entry> AudioPanel::CollectEntries() const
{
	struct Candidate {
		OBSSource source;
		int weight;
		QString name;
	};

	struct CollectData {
		QSet<QString> previewed;
		QStringList globalUuids;
		std::vector<Candidate> candidates;
	} data;

	data.previewed = GetStudioModePreviewedSources();
	for (int channel = 1; channel <= globalAudioChannels; channel++) {
		OBSSourceAutoRelease source = obs_get_output_source(channel);
		if (source) {
			data.globalUuids.append(QT_UTF8(obs_source_get_uuid(source)));
		}
	}

	auto collect = [](void *param, obs_source_t *source) {
		auto collectData = static_cast<CollectData *>(param);
		QString uuid = QT_UTF8(obs_source_get_uuid(source));

		if (!IsShownInMixer(source, collectData->previewed.contains(uuid))) {
			return true;
		}

		/* The mixer's order: global sources, pinned sources, then the
		 * others by name. */
		int globalIndex = static_cast<int>(collectData->globalUuids.indexOf(uuid));
		int weight = globalIndex >= 0 ? globalIndex : (GetPinned(source) ? 100 : 200);
		collectData->candidates.push_back({source, weight, QT_UTF8(obs_source_get_name(source))});
		return true;
	};
	obs_enum_sources(collect, &data);

	std::sort(data.candidates.begin(), data.candidates.end(), [](const Candidate &a, const Candidate &b) {
		if (a.weight != b.weight) {
			return a.weight < b.weight;
		}
		return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
	});

	std::vector<Entry> entries;
	for (const Candidate &candidate : data.candidates) {
		obs_source_t *source = candidate.source;
		entries.push_back({QT_UTF8(obs_source_get_uuid(source)), DescribeAudioSource(source)});
	}
	return entries;
}

void AudioPanel::ConnectSourceSignals(const std::vector<Entry> &entries)
{
	sourceSignals.clear();

	for (const Entry &entry : entries) {
		OBSSourceAutoRelease source = obs_get_source_by_uuid(QT_TO_UTF8(entry.uuid));
		if (!source) {
			continue;
		}

		signal_handler_t *handler = obs_source_get_signal_handler(source);
		for (const char *signal : sourceSignalNames) {
			sourceSignals.emplace_back(handler, signal, OnSourceSignal, this);
		}
	}
}

void AudioPanel::Refresh()
{
	std::vector<Entry> entries = CollectEntries();

	QStringList uuids;
	for (const Entry &entry : entries) {
		uuids.append(entry.uuid);
	}

	if (uuids == listedUuids) {
		/* Only volumes or states changed: keep the screen reader's
		 * position by updating the texts in place. */
		for (int row = 0; row < model->rowCount(); row++) {
			QStandardItem *item = model->item(row);
			if (item->text() != entries[row].text) {
				item->setText(entries[row].text);
			}
		}
		return;
	}

	QString currentUuid = view->currentIndex().data(uuidRole).toString();

	model->clear();
	for (const Entry &entry : entries) {
		QStandardItem *item = new QStandardItem(entry.text);
		item->setData(entry.uuid, uuidRole);
		model->appendRow(item);
	}

	listedUuids = uuids;
	ConnectSourceSignals(entries);

	int row = std::max(0, static_cast<int>(uuids.indexOf(currentUuid)));
	if (row < model->rowCount()) {
		view->selectionModel()->setCurrentIndex(model->index(row, 0), QItemSelectionModel::ClearAndSelect);
	}
	AttachMeter();
}

OBSSource AudioPanel::GetSelectedSource() const
{
	QModelIndex index = view->currentIndex();
	if (!index.isValid()) {
		return nullptr;
	}

	OBSSourceAutoRelease source = obs_get_source_by_uuid(QT_TO_UTF8(index.data(uuidRole).toString()));
	return source.Get();
}

void AudioPanel::AttachMeter()
{
	if (!volmeter) {
		return;
	}

	OBSSource source = GetSelectedSource();
	if (source && OBSGetStrongRef(meterSource) == source) {
		return;
	}

	meterSource = OBSGetWeakRef(source);
	ResetLevels();

	if (source) {
		obs_volmeter_attach_source(volmeter, source);
	} else {
		obs_volmeter_detach_source(volmeter);
	}
}

void AudioPanel::ResetLevels()
{
	std::lock_guard lock(levelsMutex);
	levels = {minimumVolumeDb, minimumVolumeDb, minimumVolumeDb, 0};
}

void AudioPanel::ChangeVolume(float step)
{
	OBSSource source = GetSelectedSource();
	if (!source) {
		return;
	}

	float db = StepVolumeDb(GetVolumeDb(source), step, volumeKeysFloorDb, volumeKeysMaximumDb);
	SetVolumeDbWithUndo(main, source, db);
	AnnounceRequested(FormatDb(GetVolumeDb(source)));
}

void AudioPanel::ToggleMute()
{
	OBSSource source = GetSelectedSource();
	if (!source) {
		return;
	}

	bool muted = !obs_source_muted(source);
	SetMutedWithUndo(main, source, muted);

	const char *lookupKey = muted ? "Accessibility.Announce.SourceMuted" : "Accessibility.Announce.SourceUnmuted";
	AnnounceRequested(QTStr(lookupKey).arg(obs_source_get_name(source)));
}

void AudioPanel::ToggleMonitoring()
{
	OBSSource source = GetSelectedSource();
	if (!source) {
		return;
	}

	bool enabled = !obs_source_get_monitoring_enabled(source);
	SetMonitoringWithUndo(main, source, enabled);

	const char *lookupKey = enabled ? "Accessibility.Announce.MonitoringOn"
					: "Accessibility.Announce.MonitoringOff";
	AnnounceRequested(QTStr(lookupKey).arg(obs_source_get_name(source)));
}

void AudioPanel::ReadLevels()
{
	OBSSource source = GetSelectedSource();
	if (!source) {
		return;
	}

	Levels current;
	{
		std::lock_guard lock(levelsMutex);
		current = levels;
	}

	bool stale = os_gettime_ns() - current.heldPeakTime > peakHoldNs;
	float peak = stale ? current.peak : current.heldPeak;

	QString text;
	if (peak <= silenceDb) {
		text = QTStr("Accessibility.Audio.LevelsSilent");
	} else {
		text = QTStr("Accessibility.Audio.Levels").arg(FormatDb(peak), FormatDb(current.magnitude));
	}

	AnnounceRequested(text);
}

void AudioPanel::OpenAudioProperties()
{
	if (OBSSource source = GetSelectedSource()) {
		AudioPropertiesDialog::Open(main, source, window());
	}
}

void AudioPanel::OpenFilters()
{
	if (OBSSource source = GetSelectedSource()) {
		main->CreateFiltersWindow(source);
	}
}

void AudioPanel::OpenProperties()
{
	if (OBSSource source = GetSelectedSource()) {
		main->CreatePropertiesWindow(source);
	}
}

} // namespace OBS::Accessibility
