#include "StreamDeckActions.hpp"

#include <OBSApp.hpp>

#include <obs.hpp>
#include <qt-wrappers.hpp>

#include <algorithm>
#include <iterator>
#include <map>

namespace OBS::Accessibility::StreamDeck {

namespace {
/* A hotkey as found in this session. */
struct Entry {
	obs_hotkey_id id = OBS_INVALID_HOTKEY_ID;
	obs_hotkey_id partner = OBS_INVALID_HOTKEY_ID;
	ActionInfo info;
	/* "Microphone: " for a source's hotkey, so that a toggle reads
	 * "Microphone: Mute / Unmute". */
	QString prefix;
	QString ownDescription;
};

QString Prefixed(const char *contextName)
{
	return QTStr("Accessibility.StreamDeck.ContextPrefix").arg(QT_UTF8(contextName));
}

bool FillEntry(obs_hotkey_t *key, Entry &entry)
{
	void *registerer = obs_hotkey_get_registerer(key);
	HotkeyRef &ref = entry.info.ref;

	switch (obs_hotkey_get_registerer_type(key)) {
	case OBS_HOTKEY_REGISTERER_FRONTEND:
		ref.owner = HotkeyOwner::Frontend;
		entry.info.category = ActionCategory::General;
		break;
	case OBS_HOTKEY_REGISTERER_SOURCE: {
		OBSSourceAutoRelease source = obs_weak_source_get_source(static_cast<obs_weak_source_t *>(registerer));
		const char *name = source ? obs_source_get_name(source) : nullptr;
		if (!name) {
			return false;
		}
		ref.owner = HotkeyOwner::Source;
		ref.context = QT_UTF8(obs_source_get_uuid(source));
		ref.contextName = QT_UTF8(name);
		entry.info.category = obs_scene_from_source(source) ? ActionCategory::Scenes : ActionCategory::Sources;
		entry.prefix = Prefixed(name);
		break;
	}
	case OBS_HOTKEY_REGISTERER_OUTPUT: {
		OBSOutputAutoRelease output = obs_weak_output_get_output(static_cast<obs_weak_output_t *>(registerer));
		if (!output) {
			return false;
		}
		ref.owner = HotkeyOwner::Output;
		ref.context = ref.contextName = QT_UTF8(obs_output_get_name(output));
		entry.info.category = ActionCategory::Outputs;
		entry.prefix = Prefixed(obs_output_get_name(output));
		break;
	}
	case OBS_HOTKEY_REGISTERER_ENCODER: {
		OBSEncoderAutoRelease encoder =
			obs_weak_encoder_get_encoder(static_cast<obs_weak_encoder_t *>(registerer));
		if (!encoder) {
			return false;
		}
		ref.owner = HotkeyOwner::Encoder;
		ref.context = ref.contextName = QT_UTF8(obs_encoder_get_name(encoder));
		entry.info.category = ActionCategory::Encoders;
		entry.prefix = Prefixed(obs_encoder_get_name(encoder));
		break;
	}
	case OBS_HOTKEY_REGISTERER_SERVICE: {
		OBSServiceAutoRelease service =
			obs_weak_service_get_service(static_cast<obs_weak_service_t *>(registerer));
		if (!service) {
			return false;
		}
		ref.owner = HotkeyOwner::Service;
		ref.context = ref.contextName = QT_UTF8(obs_service_get_name(service));
		entry.info.category = ActionCategory::Services;
		entry.prefix = Prefixed(obs_service_get_name(service));
		break;
	}
	default:
		return false;
	}

	ref.name = QT_UTF8(obs_hotkey_get_name(key));
	entry.ownDescription = QT_UTF8(obs_hotkey_get_description(key));
	entry.info.description = entry.prefix + entry.ownDescription;
	entry.id = obs_hotkey_get_id(key);
	entry.partner = obs_hotkey_get_pair_partner_id(key);
	return true;
}

std::vector<Entry> ListEntries()
{
	std::vector<Entry> entries;
	obs_enum_hotkeys(
		[](void *data, obs_hotkey_id, obs_hotkey_t *key) {
			Entry entry;
			if (FillEntry(key, entry)) {
				static_cast<std::vector<Entry> *>(data)->push_back(std::move(entry));
			}
			return true;
		},
		&entries);

	std::map<obs_hotkey_id, QString> descriptions;
	for (const Entry &entry : entries) {
		descriptions[entry.id] = entry.ownDescription;
	}
	for (Entry &entry : entries) {
		auto partner = descriptions.find(entry.partner);
		if (partner != descriptions.end()) {
			entry.info.partnerDescription = partner->second;
		} else {
			entry.partner = OBS_INVALID_HOTKEY_ID;
		}
	}

	return entries;
}

bool Matches(const Entry &entry, const HotkeyRef &ref)
{
	const HotkeyRef &own = entry.info.ref;
	return own.owner == ref.owner && own.name == ref.name && own.context == ref.context;
}

const Entry *FindEntry(const std::vector<Entry> &entries, const HotkeyRef &ref)
{
	auto found = std::find_if(entries.begin(), entries.end(),
				  [&ref](const Entry &entry) { return Matches(entry, ref); });
	return found != entries.end() ? &*found : nullptr;
}

/* Frontend hotkeys whose result OutputAnnouncements or the Scenes panel
 * already speaks. */
bool IsAnnouncedElsewhere(const HotkeyRef &ref)
{
	static const char *const frontendNames[] = {
		"OBSBasic.StartStreaming",    "OBSBasic.StopStreaming",    "OBSBasic.ForceStopStreaming",
		"OBSBasic.StartRecording",    "OBSBasic.StopRecording",    "OBSBasic.PauseRecording",
		"OBSBasic.UnpauseRecording",  "OBSBasic.StartReplayBuffer", "OBSBasic.StopReplayBuffer",
		"OBSBasic.StartVirtualCam",   "OBSBasic.StopVirtualCam",   "OBSBasic.Transition",
	};

	if (ref.owner == HotkeyOwner::Source) {
		return ref.name == "OBSBasic.SelectScene";
	}
	if (ref.owner != HotkeyOwner::Frontend) {
		return false;
	}

	/* The status readouts speak for themselves. */
	if (ref.name.startsWith("Accessibility.")) {
		return true;
	}
	return std::any_of(std::begin(frontendNames), std::end(frontendNames),
			   [&ref](const char *name) { return ref.name == name; });
}

/* The state of a scene item after its Show or Hide hotkey, which is named
 * after the item's ID. Empty if the item cannot be found. */
QString SceneItemFeedback(obs_source_t *sceneSource, const QString &hotkeyName, const QString &prefix)
{
	bool ok = false;
	int64_t id = hotkeyName.mid(prefix.size()).toLongLong(&ok);
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (!scene) {
		scene = obs_group_from_source(sceneSource);
	}
	if (!ok || !scene) {
		return {};
	}

	obs_sceneitem_t *item = obs_scene_find_sceneitem_by_id(scene, id);
	if (!item) {
		return {};
	}

	const char *name = obs_source_get_name(obs_sceneitem_get_source(item));
	const char *lookupKey = obs_sceneitem_visible(item) ? "Accessibility.Announce.ItemShown"
							     : "Accessibility.Announce.ItemHidden";
	return QTStr(lookupKey).arg(QT_UTF8(name));
}

QString SourceFeedback(const HotkeyRef &ref)
{
	static const QString showPrefix = "libobs.show_scene_item.";
	static const QString hidePrefix = "libobs.hide_scene_item.";

	OBSSourceAutoRelease source = obs_get_source_by_uuid(QT_TO_UTF8(ref.context));
	if (!source) {
		return {};
	}
	QString name = QT_UTF8(obs_source_get_name(source));

	if (ref.name == "libobs.mute" || ref.name == "libobs.unmute") {
		const char *lookupKey = obs_source_muted(source) ? "Accessibility.Announce.SourceMuted"
								 : "Accessibility.Announce.SourceUnmuted";
		return QTStr(lookupKey).arg(name);
	}
	if (ref.name == "libobs.monitor-on" || ref.name == "libobs.monitor-off") {
		bool monitoring = obs_source_get_monitoring_enabled(source);
		const char *lookupKey = monitoring ? "Accessibility.Announce.MonitoringOn"
						   : "Accessibility.Announce.MonitoringOff";
		return QTStr(lookupKey).arg(name);
	}
	if (ref.name.startsWith(showPrefix)) {
		return SceneItemFeedback(source, ref.name, showPrefix);
	}
	if (ref.name.startsWith(hidePrefix)) {
		return SceneItemFeedback(source, ref.name, hidePrefix);
	}
	return {};
}

const char *PositionKey(Row row, Column column)
{
	static const std::map<std::pair<Row, Column>, const char *> keys = {
		{{Row::Top, Column::Left}, "Accessibility.StreamDeck.Position.TopLeft"},
		{{Row::Top, Column::Center}, "Accessibility.StreamDeck.Position.Top"},
		{{Row::Top, Column::Right}, "Accessibility.StreamDeck.Position.TopRight"},
		{{Row::Middle, Column::Left}, "Accessibility.StreamDeck.Position.Left"},
		{{Row::Middle, Column::Center}, "Accessibility.StreamDeck.Position.Center"},
		{{Row::Middle, Column::Right}, "Accessibility.StreamDeck.Position.Right"},
		{{Row::Bottom, Column::Left}, "Accessibility.StreamDeck.Position.BottomLeft"},
		{{Row::Bottom, Column::Center}, "Accessibility.StreamDeck.Position.Bottom"},
		{{Row::Bottom, Column::Right}, "Accessibility.StreamDeck.Position.BottomRight"},
	};

	auto found = keys.find({row, column});
	return found != keys.end() ? found->second : nullptr;
}
} // namespace

std::vector<ActionInfo> ListActions()
{
	std::vector<ActionInfo> actions;
	for (Entry &entry : ListEntries()) {
		actions.push_back(std::move(entry.info));
	}

	std::stable_sort(actions.begin(), actions.end(), [](const ActionInfo &a, const ActionInfo &b) {
		return static_cast<int>(a.category) < static_cast<int>(b.category);
	});
	return actions;
}

QString CategoryName(ActionCategory category)
{
	switch (category) {
	case ActionCategory::General:
		return QTStr("Accessibility.StreamDeck.Category.General");
	case ActionCategory::Scenes:
		return QTStr("Accessibility.StreamDeck.Category.Scenes");
	case ActionCategory::Sources:
		return QTStr("Accessibility.StreamDeck.Category.Sources");
	case ActionCategory::Outputs:
		return QTStr("Accessibility.StreamDeck.Category.Outputs");
	case ActionCategory::Encoders:
		return QTStr("Accessibility.StreamDeck.Category.Encoders");
	case ActionCategory::Services:
		return QTStr("Accessibility.StreamDeck.Category.Services");
	case ActionCategory::Invalid:
		break;
	}
	return {};
}

ResolvedAction Resolve(const HotkeyRef &ref)
{
	ResolvedAction resolved;

	std::vector<Entry> entries = ListEntries();
	const Entry *entry = FindEntry(entries, ref);
	if (!entry) {
		return resolved;
	}

	resolved.id = entry->id;
	if (ref.toggle) {
		resolved.partner = entry->partner;
	}
	return resolved;
}

QString Describe(const HotkeyRef &ref)
{
	std::vector<Entry> entries = ListEntries();
	const Entry *entry = FindEntry(entries, ref);
	if (!entry) {
		QString missing = ref.contextName.isEmpty() ? ref.name
							    : Prefixed(QT_TO_UTF8(ref.contextName)) + ref.name;
		return QTStr("Accessibility.StreamDeck.Missing").arg(missing);
	}

	if (ref.toggle && !entry->info.partnerDescription.isEmpty()) {
		return QTStr("Accessibility.StreamDeck.Toggle")
			.arg(entry->prefix + entry->ownDescription, entry->info.partnerDescription);
	}
	return entry->info.description;
}

QString Feedback(const HotkeyRef &ref, const QString &label)
{
	if (IsAnnouncedElsewhere(ref)) {
		return {};
	}

	if (ref.owner == HotkeyOwner::Source) {
		/* Push-to-talk lasts while the user speaks. */
		if (ref.name == "libobs.push-to-talk" || ref.name == "libobs.push-to-mute") {
			return {};
		}

		QString state = SourceFeedback(ref);
		if (!state.isEmpty()) {
			return state;
		}
	}

	return label.isEmpty() ? Describe(ref) : label;
}

void Trigger(obs_hotkey_id id, bool pressed)
{
	obs_hotkey_trigger_routed_callback(id, pressed);
}

void Toggle(const ResolvedAction &action)
{
	if (action.partner == OBS_INVALID_HOTKEY_ID) {
		Trigger(action.id, true);
		Trigger(action.id, false);
		return;
	}

	/* The half pressed first runs if it applies and keeps the other from
	 * running; otherwise the other one runs. */
	Trigger(action.id, true);
	Trigger(action.partner, true);
	Trigger(action.id, false);
	Trigger(action.partner, false);
}

QString KeyName(const Model &model, int key)
{
	KeyPosition position = PositionOf(model, key);

	const char *positionKey = PositionKey(position.rowName, position.columnName);
	QString where = positionKey ? QTStr(positionKey)
				    : QTStr("Accessibility.StreamDeck.Position.Grid")
					      .arg(position.row)
					      .arg(position.column);

	return QTStr("Accessibility.StreamDeck.Key").arg(key + 1).arg(where);
}

} // namespace OBS::Accessibility::StreamDeck
