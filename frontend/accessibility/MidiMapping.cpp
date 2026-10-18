#include "MidiMapping.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>

#include <algorithm>

namespace OBS::Accessibility::Midi {

namespace {
constexpr int formatVersion = 1;

template<typename Enum> struct EnumName {
	Enum value;
	const char *name;
};

const EnumName<ControlType> controlTypeNames[] = {
	{ControlType::Note, "note"},
	{ControlType::ControlChange, "cc"},
	{ControlType::PitchBend, "pitchbend"},
};

const EnumName<TargetKind> targetKindNames[] = {
	{TargetKind::Hotkey, "hotkey"},
	{TargetKind::SourceVolume, "volume"},
	{TargetKind::SourceMute, "mute"},
	{TargetKind::SourceMonitoring, "monitoring"},
	{TargetKind::SourceBalance, "balance"},
	{TargetKind::TBar, "tbar"},
	{TargetKind::TransitionDuration, "transitionDuration"},
	{TargetKind::Property, "property"},
};

const EnumName<PropertyType> propertyTypeNames[] = {
	{PropertyType::Bool, "bool"},
	{PropertyType::Int, "int"},
	{PropertyType::Float, "float"},
};

const EnumName<EncoderMode> encoderModeNames[] = {
	{EncoderMode::Absolute, "absolute"},
	{EncoderMode::TwosComplement, "twosComplement"},
	{EncoderMode::BinaryOffset, "binaryOffset"},
	{EncoderMode::SignMagnitude, "signMagnitude"},
};

template<typename Enum, size_t Size> QString NameOf(const EnumName<Enum> (&names)[Size], Enum value)
{
	for (const EnumName<Enum> &entry : names) {
		if (entry.value == value) {
			return entry.name;
		}
	}
	return {};
}

template<typename Enum, size_t Size> Enum ValueNamed(const EnumName<Enum> (&names)[Size], const QString &name)
{
	for (const EnumName<Enum> &entry : names) {
		if (name == entry.name) {
			return entry.value;
		}
	}
	return Enum::Invalid;
}

bool IsSourceKind(TargetKind kind)
{
	switch (kind) {
	case TargetKind::SourceVolume:
	case TargetKind::SourceMute:
	case TargetKind::SourceMonitoring:
	case TargetKind::SourceBalance:
		return true;
	default:
		return false;
	}
}

QJsonObject TargetToJson(const Target &target)
{
	QJsonObject json;
	json["kind"] = NameOf(targetKindNames, target.kind);

	if (target.kind == TargetKind::Hotkey) {
		json["owner"] = StreamDeck::HotkeyOwnerName(target.hotkey.owner);
		json["context"] = target.hotkey.context;
		json["contextName"] = target.hotkey.contextName;
		json["name"] = target.hotkey.name;
		json["toggle"] = target.hotkey.toggle;
		return json;
	}

	if (IsSourceKind(target.kind) || target.kind == TargetKind::Property) {
		json["uuid"] = target.uuid;
		json["sourceName"] = target.name;
		if (!target.parentName.isEmpty()) {
			json["parentName"] = target.parentName;
		}
	}

	if (target.kind == TargetKind::Property) {
		json["setting"] = target.setting;
		json["settingLabel"] = target.settingLabel;
		json["propertyType"] = NameOf(propertyTypeNames, target.propertyType);
		json["minimum"] = target.minimum;
		json["maximum"] = target.maximum;
		json["step"] = target.step;
		json["suffix"] = target.suffix;
	}
	return json;
}

Target TargetFromJson(const QJsonObject &json)
{
	Target target;
	target.kind = ValueNamed(targetKindNames, json["kind"].toString());

	if (target.kind == TargetKind::Hotkey) {
		target.hotkey.owner = StreamDeck::HotkeyOwnerNamed(json["owner"].toString());
		target.hotkey.context = json["context"].toString();
		target.hotkey.contextName = json["contextName"].toString();
		target.hotkey.name = json["name"].toString();
		target.hotkey.toggle = json["toggle"].toBool();
		return target;
	}

	target.uuid = json["uuid"].toString();
	target.name = json["sourceName"].toString();
	target.parentName = json["parentName"].toString();

	if (target.kind == TargetKind::Property) {
		target.setting = json["setting"].toString();
		target.settingLabel = json["settingLabel"].toString();
		target.propertyType = ValueNamed(propertyTypeNames, json["propertyType"].toString());
		target.minimum = json["minimum"].toDouble(0.0);
		target.maximum = json["maximum"].toDouble(1.0);
		target.step = json["step"].toDouble(0.0);
		target.suffix = json["suffix"].toString();
	}
	return target;
}

/* "MIDIIN2 (Device)" gives "MIDIOUT2 (Device)". */
QString OutputNameFor(const QString &input)
{
	static const QRegularExpression pattern(QStringLiteral("^MIDIIN(\\d*) \\((.*)\\)$"));
	QRegularExpressionMatch match = pattern.match(input);
	if (!match.hasMatch()) {
		return input;
	}
	return QStringLiteral("MIDIOUT%1 (%2)").arg(match.captured(1), match.captured(2));
}
} // namespace

bool Target::IsValid() const
{
	switch (kind) {
	case TargetKind::Hotkey:
		return hotkey.IsValid();
	case TargetKind::SourceVolume:
	case TargetKind::SourceMute:
	case TargetKind::SourceMonitoring:
	case TargetKind::SourceBalance:
		return !uuid.isEmpty();
	case TargetKind::TBar:
	case TargetKind::TransitionDuration:
		return true;
	case TargetKind::Property:
		return !uuid.isEmpty() && !setting.isEmpty() && propertyType != PropertyType::Invalid &&
		       (propertyType == PropertyType::Bool || maximum > minimum);
	case TargetKind::Invalid:
		break;
	}
	return false;
}

bool Target::IsContinuous() const
{
	switch (kind) {
	case TargetKind::SourceVolume:
	case TargetKind::SourceBalance:
	case TargetKind::TBar:
	case TargetKind::TransitionDuration:
		return true;
	case TargetKind::Property:
		return propertyType == PropertyType::Int || propertyType == PropertyType::Float;
	default:
		return false;
	}
}

bool Target::SameAs(const Target &other) const
{
	if (kind != other.kind) {
		return false;
	}

	switch (kind) {
	case TargetKind::Hotkey:
		return hotkey.owner == other.hotkey.owner && hotkey.context == other.hotkey.context &&
		       hotkey.name == other.hotkey.name;
	case TargetKind::Property:
		return uuid == other.uuid && setting == other.setting;
	case TargetKind::TBar:
	case TargetKind::TransitionDuration:
		return true;
	default:
		return uuid == other.uuid;
	}
}

int FindControl(const Mapping &mapping, const Control &control)
{
	auto found = std::find_if(mapping.begin(), mapping.end(),
				  [&control](const Assignment &assignment) { return assignment.control == control; });
	return found != mapping.end() ? static_cast<int>(found - mapping.begin()) : -1;
}

int FindTarget(const Mapping &mapping, const Target &target)
{
	auto found = std::find_if(mapping.begin(), mapping.end(),
				  [&target](const Assignment &assignment) { return assignment.target.SameAs(target); });
	return found != mapping.end() ? static_cast<int>(found - mapping.begin()) : -1;
}

QString MappingToJson(const Mapping &mapping)
{
	QJsonArray assignments;
	for (const Assignment &assignment : mapping) {
		if (!assignment.control.IsValid() || !assignment.target.IsValid()) {
			continue;
		}

		const Control &control = assignment.control;
		const Options &options = assignment.options;

		QJsonObject entry;
		entry["port"] = control.port;
		entry["type"] = NameOf(controlTypeNames, control.type);
		entry["channel"] = control.channel;
		entry["number"] = control.number;
		entry["target"] = TargetToJson(assignment.target);
		entry["encoder"] = NameOf(encoderModeNames, options.encoder);
		entry["takeover"] = options.takeover;
		entry["invert"] = options.invert;
		entry["low"] = options.low;
		entry["high"] = options.high;
		entry["feedback"] = options.feedback;
		entry["onValue"] = options.onValue;
		entry["offValue"] = options.offValue;
		assignments.append(entry);
	}

	QJsonObject root;
	root["version"] = formatVersion;
	root["assignments"] = assignments;
	return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

Mapping MappingFromJson(const QString &json)
{
	Mapping mapping;

	QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
	if (!document.isObject()) {
		return mapping;
	}

	const QJsonArray assignments = document.object()["assignments"].toArray();
	for (const QJsonValue &value : assignments) {
		QJsonObject entry = value.toObject();

		Assignment assignment;
		Control &control = assignment.control;
		control.port = entry["port"].toString();
		control.type = ValueNamed(controlTypeNames, entry["type"].toString());
		control.channel = entry["channel"].toInt(-1);
		control.number = entry["number"].toInt(-1);
		if (!control.IsValid() || control.channel < 0 || control.channel > 15 || control.number < 0 ||
		    control.number > 127) {
			continue;
		}

		assignment.target = TargetFromJson(entry["target"].toObject());
		if (!assignment.target.IsValid() || FindControl(mapping, control) >= 0) {
			continue;
		}

		Options &options = assignment.options;
		EncoderMode encoder = ValueNamed(encoderModeNames, entry["encoder"].toString());
		options.encoder = encoder != EncoderMode::Invalid ? encoder : EncoderMode::Absolute;
		options.takeover = entry["takeover"].toBool(true);
		options.invert = entry["invert"].toBool(false);
		options.low = std::clamp(entry["low"].toDouble(0.0), 0.0, 1.0);
		options.high = std::clamp(entry["high"].toDouble(1.0), 0.0, 1.0);
		options.feedback = entry["feedback"].toBool(true);
		options.onValue = std::clamp(entry["onValue"].toInt(127), 0, 127);
		options.offValue = std::clamp(entry["offValue"].toInt(0), 0, 127);

		mapping.push_back(assignment);
	}

	return mapping;
}

bool PortSettings::IsInputEnabled(const QString &input) const
{
	return std::find(disabledInputs.begin(), disabledInputs.end(), input) == disabledInputs.end();
}

void PortSettings::SetInputEnabled(const QString &input, bool enabled)
{
	disabledInputs.erase(std::remove(disabledInputs.begin(), disabledInputs.end(), input), disabledInputs.end());
	if (!enabled) {
		disabledInputs.push_back(input);
	}
}

QString PortSettingsToJson(const PortSettings &settings)
{
	QJsonArray disabled;
	for (const QString &input : settings.disabledInputs) {
		disabled.append(input);
	}

	QJsonObject outputs;
	for (const auto &[input, output] : settings.outputs) {
		outputs[input] = output;
	}

	QJsonObject root;
	root["version"] = formatVersion;
	root["disabledInputs"] = disabled;
	root["outputs"] = outputs;
	return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

PortSettings PortSettingsFromJson(const QString &json)
{
	PortSettings settings;

	QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
	if (!document.isObject()) {
		return settings;
	}

	QJsonObject root = document.object();
	for (const QJsonValue &value : root["disabledInputs"].toArray()) {
		if (value.isString()) {
			settings.disabledInputs.push_back(value.toString());
		}
	}

	QJsonObject outputs = root["outputs"].toObject();
	for (auto it = outputs.begin(); it != outputs.end(); ++it) {
		if (it.value().isString()) {
			settings.outputs[it.key()] = it.value().toString();
		}
	}
	return settings;
}

QString PairedOutput(const QString &input, const std::vector<QString> &outputs)
{
	for (const QString &candidate : {input, OutputNameFor(input)}) {
		if (std::find(outputs.begin(), outputs.end(), candidate) != outputs.end()) {
			return candidate;
		}
	}
	return {};
}

QString OutputFor(const PortSettings &settings, const QString &input, const std::vector<QString> &outputs)
{
	auto chosen = settings.outputs.find(input);
	if (chosen == settings.outputs.end()) {
		return PairedOutput(input, outputs);
	}

	const QString &output = chosen->second;
	bool present = std::find(outputs.begin(), outputs.end(), output) != outputs.end();
	return present ? output : QString();
}

} // namespace OBS::Accessibility::Midi
