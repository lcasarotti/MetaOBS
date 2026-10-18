#include "MidiTargets.hpp"
#include "AccessibleBridge.hpp"
#include "AccessibleFormat.hpp"
#include "AudioActions.hpp"
#include "StreamDeckActions.hpp"

#include <OBSApp.hpp>
#include <widgets/OBSBasic.hpp>

#include <obs-frontend-api.h>
#include <properties-view.hpp>
#include <qt-wrappers.hpp>

#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QApplication>
#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QSpinBox>

#include <algorithm>
#include <cmath>

namespace OBS::Accessibility::Midi {

namespace {
/* The range of the Controls panel's duration box. */
constexpr int minimumDurationMs = 50;
constexpr int maximumDurationMs = 20000;
constexpr int durationStepMs = 50;
/* One notch of an encoder on a volume, in fader positions. */
constexpr double volumeEncoderStep = 0.01;
/* Encoders on targets without steps of their own. */
constexpr double defaultEncoderStep = 1.0 / 127.0;
constexpr int maximumDecimals = 3;

QString SourceName(const Target &target)
{
	OBSSource source = FindSource(target);
	if (source) {
		return QT_UTF8(obs_source_get_name(source));
	}
	return target.name;
}

/* The name of a filter's source, or empty for other sources. */
QString ParentName(const Target &target)
{
	OBSSource source = FindSource(target);
	if (!source) {
		return target.parentName;
	}
	obs_source_t *parent = obs_filter_get_parent(source);
	return parent ? QT_UTF8(obs_source_get_name(parent)) : QString();
}

/* The duration takes a curve, so that the short durations used most get
 * most of a fader's travel. */
int DurationFromValue(double value)
{
	double ms = minimumDurationMs + (maximumDurationMs - minimumDurationMs) * value * value;
	int rounded = static_cast<int>(std::lround(ms / durationStepMs)) * durationStepMs;
	return std::clamp(rounded, minimumDurationMs, maximumDurationMs);
}

double ValueFromDuration(int ms)
{
	double fraction = static_cast<double>(ms - minimumDurationMs) / (maximumDurationMs - minimumDurationMs);
	return std::sqrt(std::clamp(fraction, 0.0, 1.0));
}

double PropertyRawValue(const Target &target, double value)
{
	double raw = target.minimum + std::clamp(value, 0.0, 1.0) * (target.maximum - target.minimum);
	if (target.step > 0.0) {
		raw = target.minimum + std::round((raw - target.minimum) / target.step) * target.step;
	}
	raw = std::clamp(raw, target.minimum, target.maximum);
	if (target.propertyType == PropertyType::Int) {
		raw = std::round(raw);
	}
	return raw;
}

int Decimals(const Target &target)
{
	if (target.propertyType != PropertyType::Float || target.step <= 0.0 || target.step >= 1.0) {
		return target.propertyType == PropertyType::Float && target.step <= 0.0 ? 2 : 0;
	}
	return std::min(static_cast<int>(std::ceil(-std::log10(target.step) - 1e-9)), maximumDecimals);
}

QString FormatNumber(const Target &target, double raw)
{
	return QString::number(raw, 'f', Decimals(target)) + target.suffix;
}

/* The widget that shows a setting in an open Properties or Filters window,
 * so that MIDI changes go through it like the user's own: the window
 * previews them and can still cancel them. */
QWidget *FindOpenSettingWidget(obs_source_t *source, const QString &setting)
{
	for (QWidget *window : QApplication::topLevelWidgets()) {
		if (!window->isVisible()) {
			continue;
		}

		for (OBSPropertiesView *view : window->findChildren<OBSPropertiesView *>()) {
			if (!view->IsObject(source)) {
				continue;
			}

			QWidget *slider = nullptr;
			for (QWidget *widget : view->findChildren<QWidget *>()) {
				if (widget->property(settingProperty).toString() != setting) {
					continue;
				}
				/* The spin box rather than the slider beside it:
				 * it holds the exact value. */
				if (qobject_cast<QAbstractSpinBox *>(widget) || qobject_cast<QCheckBox *>(widget)) {
					return widget;
				}
				if (qobject_cast<QAbstractSlider *>(widget)) {
					slider = widget;
				}
			}
			if (slider) {
				return slider;
			}
		}
	}
	return nullptr;
}

void ApplyProperty(const Target &target, double raw)
{
	OBSSource source = FindSource(target);
	if (!source) {
		return;
	}

	QByteArray setting = target.setting.toUtf8();
	QWidget *widget = FindOpenSettingWidget(source, target.setting);
	if (auto spin = qobject_cast<QSpinBox *>(widget)) {
		spin->setValue(static_cast<int>(raw));
		return;
	}
	if (auto spin = qobject_cast<QDoubleSpinBox *>(widget)) {
		spin->setValue(raw);
		return;
	}
	if (auto checkBox = qobject_cast<QCheckBox *>(widget)) {
		checkBox->setChecked(raw != 0.0);
		return;
	}

	OBSDataAutoRelease settings = obs_source_get_settings(source);
	switch (target.propertyType) {
	case PropertyType::Bool:
		obs_data_set_bool(settings, setting.constData(), raw != 0.0);
		break;
	case PropertyType::Int:
		obs_data_set_int(settings, setting.constData(), static_cast<long long>(raw));
		break;
	case PropertyType::Float:
		obs_data_set_double(settings, setting.constData(), raw);
		break;
	case PropertyType::Invalid:
		return;
	}
	obs_source_update(source, settings);
}

/* The raw value of a setting, or NaN when it cannot be read. */
double ReadProperty(const Target &target)
{
	OBSSource source = FindSource(target);
	if (!source) {
		return std::nan("");
	}

	QByteArray setting = target.setting.toUtf8();
	OBSDataAutoRelease settings = obs_source_get_settings(source);
	switch (target.propertyType) {
	case PropertyType::Bool:
		return obs_data_get_bool(settings, setting.constData()) ? 1.0 : 0.0;
	case PropertyType::Int:
		return static_cast<double>(obs_data_get_int(settings, setting.constData()));
	case PropertyType::Float:
		return obs_data_get_double(settings, setting.constData());
	case PropertyType::Invalid:
		break;
	}
	return std::nan("");
}

QString BalanceText(float balance)
{
	int percent = static_cast<int>(std::lround(std::abs(balance - 0.5f) * 200.0f));
	if (percent == 0) {
		return QTStr("Accessibility.Midi.Value.BalanceCenter");
	}
	return QTStr(balance < 0.5f ? "Accessibility.Midi.Value.BalanceLeft" : "Accessibility.Midi.Value.BalanceRight")
		.arg(percent);
}

/* Whether a scene item named in a Show or Hide hotkey is visible, or -1. */
double SceneItemState(obs_source_t *sceneSource, const QString &hotkeyName, const QString &prefix)
{
	bool ok = false;
	int64_t id = hotkeyName.mid(prefix.size()).toLongLong(&ok);
	obs_scene_t *scene = obs_scene_from_source(sceneSource);
	if (!scene) {
		scene = obs_group_from_source(sceneSource);
	}
	obs_sceneitem_t *item = ok && scene ? obs_scene_find_sceneitem_by_id(scene, id) : nullptr;
	if (!item) {
		return -1.0;
	}
	return obs_sceneitem_visible(item) ? 1.0 : 0.0;
}

/* The state behind the hotkeys that turn something on and off, for the
 * controller's lights. -1 for hotkeys without a state. */
double HotkeyState(const StreamDeck::HotkeyRef &ref)
{
	static const QString showPrefix = "libobs.show_scene_item.";
	static const QString hidePrefix = "libobs.hide_scene_item.";

	auto is = [&ref](std::initializer_list<const char *> names) {
		return std::any_of(names.begin(), names.end(), [&ref](const char *name) { return ref.name == name; });
	};
	auto state = [](bool on) {
		return on ? 1.0 : 0.0;
	};

	if (ref.owner == StreamDeck::HotkeyOwner::Frontend) {
		if (is({"OBSBasic.StartStreaming", "OBSBasic.StopStreaming", "OBSBasic.ForceStopStreaming"})) {
			return state(obs_frontend_streaming_active());
		}
		if (is({"OBSBasic.StartRecording", "OBSBasic.StopRecording"})) {
			return state(obs_frontend_recording_active());
		}
		if (is({"OBSBasic.PauseRecording", "OBSBasic.UnpauseRecording"})) {
			return state(obs_frontend_recording_paused());
		}
		if (is({"OBSBasic.StartReplayBuffer", "OBSBasic.StopReplayBuffer"})) {
			return state(obs_frontend_replay_buffer_active());
		}
		if (is({"OBSBasic.StartVirtualCam", "OBSBasic.StopVirtualCam"})) {
			return state(obs_frontend_virtualcam_active());
		}
		if (is({"OBSBasic.EnablePreviewProgram", "OBSBasic.DisablePreviewProgram"})) {
			return state(obs_frontend_preview_program_mode_active());
		}
		if (is({"OBSBasic.EnablePreview", "OBSBasic.DisablePreview"})) {
			return state(obs_frontend_preview_enabled());
		}
		return -1.0;
	}

	if (ref.owner != StreamDeck::HotkeyOwner::Source) {
		return -1.0;
	}

	OBSSourceAutoRelease source = obs_get_source_by_uuid(QT_TO_UTF8(ref.context));
	if (!source) {
		return -1.0;
	}
	if (is({"libobs.mute", "libobs.unmute"})) {
		return state(obs_source_muted(source));
	}
	if (is({"libobs.monitor-on", "libobs.monitor-off"})) {
		return state(obs_source_get_monitoring_enabled(source));
	}
	if (ref.name == "OBSBasic.SelectScene") {
		OBSSourceAutoRelease program = obs_frontend_get_current_scene();
		return state(program.Get() == source.Get());
	}
	if (ref.name.startsWith(showPrefix)) {
		return SceneItemState(source, ref.name, showPrefix);
	}
	if (ref.name.startsWith(hidePrefix)) {
		return SceneItemState(source, ref.name, hidePrefix);
	}
	return -1.0;
}

std::vector<Target> AudioSourceTargets(QAbstractItemView *view)
{
	if (!view) {
		return {};
	}

	QString uuid = view->currentIndex().data(Qt::UserRole).toString();
	OBSSourceAutoRelease source = obs_get_source_by_uuid(QT_TO_UTF8(uuid));
	if (!source) {
		return {};
	}

	Target volume;
	volume.kind = TargetKind::SourceVolume;
	volume.uuid = uuid;
	volume.name = QT_UTF8(obs_source_get_name(source));

	Target mute = volume;
	mute.kind = TargetKind::SourceMute;
	return {volume, mute};
}

/* Activating a scene is its Select Scene hotkey, which also lights the key of
 * the scene on the air. */
std::vector<Target> SceneTargets(QAbstractItemView *view)
{
	if (!view) {
		return {};
	}

	QString uuid = view->currentIndex().data(Qt::UserRole).toString();
	OBSSourceAutoRelease source = obs_get_source_by_uuid(QT_TO_UTF8(uuid));
	if (!source) {
		return {};
	}

	Target target;
	target.kind = TargetKind::Hotkey;
	target.hotkey.owner = StreamDeck::HotkeyOwner::Source;
	target.hotkey.context = uuid;
	target.hotkey.contextName = QT_UTF8(obs_source_get_name(source));
	target.hotkey.name = "OBSBasic.SelectScene";
	if (!StreamDeck::Resolve(target.hotkey).IsValid()) {
		return {};
	}
	return {target};
}

std::vector<Target> SourceSettingTargets(QWidget *widget, const QString &kind)
{
	static const std::pair<const char *, TargetKind> kinds[] = {
		{"volume", TargetKind::SourceVolume},
		{"mute", TargetKind::SourceMute},
		{"monitoring", TargetKind::SourceMonitoring},
		{"balance", TargetKind::SourceBalance},
	};

	QString uuid = widget->property(sourceProperty).toString();
	OBSSourceAutoRelease source = obs_get_source_by_uuid(QT_TO_UTF8(uuid));
	if (!source) {
		return {};
	}

	for (const auto &[name, targetKind] : kinds) {
		if (kind == name) {
			Target target;
			target.kind = targetKind;
			target.uuid = uuid;
			target.name = QT_UTF8(obs_source_get_name(source));
			return {target};
		}
	}
	return {};
}

std::vector<Target> HotkeyTargets(const QString &name)
{
	Target target;
	target.kind = TargetKind::Hotkey;
	target.hotkey.owner = StreamDeck::HotkeyOwner::Frontend;
	target.hotkey.name = name;
	target.hotkey.toggle = true;

	/* Only a hotkey with a partner toggles. */
	StreamDeck::ResolvedAction resolved = StreamDeck::Resolve(target.hotkey);
	if (!resolved.IsValid()) {
		return {};
	}
	target.hotkey.toggle = resolved.partner != OBS_INVALID_HOTKEY_ID;
	return {target};
}

std::vector<Target> PropertyTargets(QWidget *widget, const QString &setting)
{
	OBSPropertiesView *view = nullptr;
	for (QObject *parent = widget->parent(); parent && !view; parent = parent->parent()) {
		view = qobject_cast<OBSPropertiesView *>(parent);
	}
	if (!view) {
		return {};
	}

	OBSObject object = view->GetObject();
	if (!object || obs_obj_get_type(object) != OBS_OBJ_TYPE_SOURCE) {
		return {};
	}
	auto source = reinterpret_cast<obs_source_t *>(object.Get());

	obs_properties_t *properties = obs_source_properties(source);
	obs_property_t *property = obs_properties_get(properties, QT_TO_UTF8(setting));

	Target target;
	target.kind = TargetKind::Property;
	target.uuid = QT_UTF8(obs_source_get_uuid(source));
	target.name = QT_UTF8(obs_source_get_name(source));
	if (obs_source_t *parent = obs_filter_get_parent(source)) {
		target.parentName = QT_UTF8(obs_source_get_name(parent));
	}
	target.setting = setting;

	switch (property ? obs_property_get_type(property) : OBS_PROPERTY_INVALID) {
	case OBS_PROPERTY_BOOL:
		target.propertyType = PropertyType::Bool;
		break;
	case OBS_PROPERTY_INT:
		target.propertyType = PropertyType::Int;
		target.minimum = obs_property_int_min(property);
		target.maximum = obs_property_int_max(property);
		target.step = obs_property_int_step(property);
		target.suffix = QT_UTF8(obs_property_int_suffix(property));
		break;
	case OBS_PROPERTY_FLOAT:
		target.propertyType = PropertyType::Float;
		target.minimum = obs_property_float_min(property);
		target.maximum = obs_property_float_max(property);
		target.step = obs_property_float_step(property);
		target.suffix = QT_UTF8(obs_property_float_suffix(property));
		break;
	default:
		break;
	}
	if (property) {
		target.settingLabel = QT_UTF8(obs_property_description(property));
	}
	obs_properties_destroy(properties);

	if (!target.IsValid()) {
		return {};
	}
	return {target};
}
} // namespace

QString DescribeControl(const Control &control, bool withPort)
{
	QString text;
	switch (control.type) {
	case ControlType::Note: {
		QStringList names = QTStr("Accessibility.Midi.NoteNames").split(',');
		text = QTStr("Accessibility.Midi.Control.Note")
			       .arg(NoteName(control.number, names))
			       .arg(control.channel + 1);
		break;
	}
	case ControlType::ControlChange:
		text = QTStr("Accessibility.Midi.Control.CC").arg(control.number).arg(control.channel + 1);
		break;
	case ControlType::PitchBend:
		text = QTStr("Accessibility.Midi.Control.PitchBend").arg(control.channel + 1);
		break;
	case ControlType::Invalid:
		return {};
	}

	if (withPort) {
		text = QTStr("Accessibility.Midi.Control.WithPort").arg(text, control.port);
	}
	return text;
}

QString DescribeTarget(const Target &target)
{
	QString text;
	switch (target.kind) {
	case TargetKind::Hotkey:
		return StreamDeck::Describe(target.hotkey);
	case TargetKind::SourceVolume:
		text = QTStr("Accessibility.Midi.Target.Volume").arg(SourceName(target));
		break;
	case TargetKind::SourceMute:
		text = QTStr("Accessibility.Midi.Target.Mute").arg(SourceName(target));
		break;
	case TargetKind::SourceMonitoring:
		text = QTStr("Accessibility.Midi.Target.Monitoring").arg(SourceName(target));
		break;
	case TargetKind::SourceBalance:
		text = QTStr("Accessibility.Midi.Target.Balance").arg(SourceName(target));
		break;
	case TargetKind::TBar:
		return QTStr("Accessibility.Controls.TBar");
	case TargetKind::TransitionDuration:
		return QTStr("Accessibility.Midi.Target.TransitionDuration");
	case TargetKind::Property: {
		QString parent = ParentName(target);
		text = parent.isEmpty() ? QTStr("Accessibility.Midi.Target.Property")
						  .arg(SourceName(target), target.settingLabel)
					: QTStr("Accessibility.Midi.Target.FilterProperty")
						  .arg(parent, SourceName(target), target.settingLabel);
		break;
	}
	case TargetKind::Invalid:
		return {};
	}

	if (!IsAvailable(target)) {
		text = QTStr("Accessibility.Midi.Target.Missing").arg(text);
	}
	return text;
}

QString DescribeValue(const Target &target)
{
	OBSSource source = FindSource(target);

	switch (target.kind) {
	case TargetKind::Hotkey:
		return StreamDeck::Feedback(target.hotkey, QString());
	case TargetKind::SourceVolume:
		if (!source) {
			break;
		}
		return QTStr("Accessibility.Midi.Value.Named").arg(SourceName(target), FormatDb(GetVolumeDb(source)));
	case TargetKind::SourceMute:
		if (!source) {
			break;
		}
		return QTStr(obs_source_muted(source) ? "Accessibility.Announce.SourceMuted"
						      : "Accessibility.Announce.SourceUnmuted")
			.arg(SourceName(target));
	case TargetKind::SourceMonitoring:
		if (!source) {
			break;
		}
		return QTStr(obs_source_get_monitoring_enabled(source) ? "Accessibility.Announce.MonitoringOn"
								       : "Accessibility.Announce.MonitoringOff")
			.arg(SourceName(target));
	case TargetKind::SourceBalance:
		if (!source) {
			break;
		}
		return QTStr("Accessibility.Midi.Value.Balance")
			.arg(SourceName(target), BalanceText(obs_source_get_balance_value(source)));
	case TargetKind::TBar: {
		int percent = static_cast<int>(std::lround(ReadValue(target) * 100.0));
		return QTStr("Accessibility.Midi.Value.TBar").arg(percent);
	}
	case TargetKind::TransitionDuration:
		return QTStr("Accessibility.Midi.Value.TransitionDuration").arg(obs_frontend_get_transition_duration());
	case TargetKind::Property: {
		double raw = ReadProperty(target);
		if (std::isnan(raw)) {
			break;
		}
		const char *stateKey = raw != 0.0 ? "Accessibility.Midi.Value.On" : "Accessibility.Midi.Value.Off";
		QString value = target.propertyType == PropertyType::Bool ? QTStr(stateKey) : FormatNumber(target, raw);
		return QTStr("Accessibility.Midi.Value.Named").arg(target.settingLabel, value);
	}
	case TargetKind::Invalid:
		break;
	}
	return DescribeTarget(target);
}

bool IsAvailable(const Target &target)
{
	switch (target.kind) {
	case TargetKind::Hotkey:
		return StreamDeck::Resolve(target.hotkey).IsValid();
	case TargetKind::SourceVolume:
	case TargetKind::SourceMute:
	case TargetKind::SourceMonitoring:
	case TargetKind::SourceBalance:
	case TargetKind::Property:
		return FindSource(target) != nullptr;
	case TargetKind::TBar:
		return obs_frontend_preview_program_mode_active();
	case TargetKind::TransitionDuration:
		return true;
	case TargetKind::Invalid:
		break;
	}
	return false;
}

double ReadValue(const Target &target)
{
	OBSSource source = FindSource(target);

	switch (target.kind) {
	case TargetKind::SourceVolume:
		return source ? VolumeDbToPosition(obs_mul_to_db(obs_source_get_volume(source))) : -1.0;
	case TargetKind::SourceMute:
		return source ? (obs_source_muted(source) ? 1.0 : 0.0) : -1.0;
	case TargetKind::SourceMonitoring:
		return source ? (obs_source_get_monitoring_enabled(source) ? 1.0 : 0.0) : -1.0;
	case TargetKind::SourceBalance:
		return source ? obs_source_get_balance_value(source) : -1.0;
	case TargetKind::TBar: {
		if (!obs_frontend_preview_program_mode_active()) {
			return -1.0;
		}
		int maximum = OBSAccessibleBridge::TBarMaximum();
		return maximum > 0 ? static_cast<double>(obs_frontend_get_tbar_position()) / maximum : -1.0;
	}
	case TargetKind::TransitionDuration:
		return ValueFromDuration(obs_frontend_get_transition_duration());
	case TargetKind::Property: {
		double raw = ReadProperty(target);
		if (std::isnan(raw)) {
			return -1.0;
		}
		if (target.propertyType == PropertyType::Bool) {
			return raw;
		}
		return std::clamp((raw - target.minimum) / (target.maximum - target.minimum), 0.0, 1.0);
	}
	case TargetKind::Hotkey:
		return HotkeyState(target.hotkey);
	case TargetKind::Invalid:
		break;
	}
	return -1.0;
}

void ApplyValue(const Target &target, double value)
{
	value = std::clamp(value, 0.0, 1.0);
	OBSSource source = FindSource(target);

	switch (target.kind) {
	case TargetKind::SourceVolume:
		if (source) {
			obs_source_set_volume(source, obs_db_to_mul(static_cast<float>(PositionToVolumeDb(value))));
		}
		break;
	case TargetKind::SourceBalance:
		if (source) {
			obs_source_set_balance_value(source, static_cast<float>(value));
		}
		break;
	case TargetKind::TransitionDuration:
		obs_frontend_set_transition_duration(DurationFromValue(value));
		break;
	case TargetKind::Property:
		if (target.propertyType != PropertyType::Bool) {
			ApplyProperty(target, PropertyRawValue(target, value));
		}
		break;
	default:
		break;
	}
}

void ToggleTarget(const Target &target)
{
	OBSSource source = FindSource(target);
	OBSBasic *main = OBSBasic::Get();

	switch (target.kind) {
	case TargetKind::SourceMute:
		if (source && main) {
			SetMutedWithUndo(main, source, !obs_source_muted(source));
		}
		break;
	case TargetKind::SourceMonitoring:
		if (source && main) {
			SetMonitoringWithUndo(main, source, !obs_source_get_monitoring_enabled(source));
		}
		break;
	case TargetKind::Property:
		if (target.propertyType == PropertyType::Bool) {
			ApplyProperty(target, ReadProperty(target) != 0.0 ? 0.0 : 1.0);
		}
		break;
	default:
		break;
	}
}

double EncoderStep(const Target &target)
{
	switch (target.kind) {
	case TargetKind::SourceVolume:
		return volumeEncoderStep;
	case TargetKind::TransitionDuration:
		return 1.0 / 100.0;
	case TargetKind::Property:
		if (target.step > 0.0 && target.maximum > target.minimum) {
			return std::max(target.step / (target.maximum - target.minimum), defaultEncoderStep / 4.0);
		}
		break;
	default:
		break;
	}
	return defaultEncoderStep;
}

OBSSource FindSource(const Target &target)
{
	if (target.uuid.isEmpty()) {
		return nullptr;
	}
	OBSSourceAutoRelease source = obs_get_source_by_uuid(QT_TO_UTF8(target.uuid));
	return source.Get();
}

std::vector<Target> TargetsForWidget(QWidget *widget)
{
	for (QWidget *current = widget; current; current = current->parentWidget()) {
		QString kind = current->property(targetProperty).toString();
		if (kind == "audioSources") {
			return AudioSourceTargets(qobject_cast<QAbstractItemView *>(current));
		}
		if (kind == "scenes") {
			return SceneTargets(qobject_cast<QAbstractItemView *>(current));
		}
		if (!kind.isEmpty() && !current->property(sourceProperty).toString().isEmpty()) {
			return SourceSettingTargets(current, kind);
		}
		if (kind == "tbar" || kind == "transitionDuration") {
			Target target;
			target.kind = kind == "tbar" ? TargetKind::TBar : TargetKind::TransitionDuration;
			return {target};
		}

		QString hotkey = current->property(hotkeyProperty).toString();
		if (!hotkey.isEmpty()) {
			return HotkeyTargets(hotkey);
		}

		QString setting = current->property(settingProperty).toString();
		if (!setting.isEmpty()) {
			return PropertyTargets(current, setting);
		}

		if (current->isWindow()) {
			break;
		}
	}
	return {};
}

} // namespace OBS::Accessibility::Midi
