#include "StreamDeckMapping.hpp"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace OBS::Accessibility::StreamDeck {

namespace {
constexpr int formatVersion = 1;
/* Larger than any Stream Deck, to reject nonsense. */
constexpr int maximumKeys = 64;

struct OwnerName {
	HotkeyOwner owner;
	const char *name;
};

const OwnerName ownerNames[] = {
	{HotkeyOwner::Frontend, "frontend"}, {HotkeyOwner::Source, "source"},   {HotkeyOwner::Output, "output"},
	{HotkeyOwner::Encoder, "encoder"},   {HotkeyOwner::Service, "service"},
};

QString NameOf(HotkeyOwner owner)
{
	for (const OwnerName &entry : ownerNames) {
		if (entry.owner == owner) {
			return entry.name;
		}
	}
	return {};
}

HotkeyOwner OwnerNamed(const QString &name)
{
	for (const OwnerName &entry : ownerNames) {
		if (name == entry.name) {
			return entry.owner;
		}
	}
	return HotkeyOwner::Invalid;
}
} // namespace

QString MappingToJson(const Mapping &mapping)
{
	QJsonArray keys;
	for (const auto &[key, assignment] : mapping) {
		if (!assignment.action.IsValid()) {
			continue;
		}

		QJsonObject entry;
		entry["key"] = key;
		entry["owner"] = NameOf(assignment.action.owner);
		entry["context"] = assignment.action.context;
		entry["contextName"] = assignment.action.contextName;
		entry["name"] = assignment.action.name;
		entry["toggle"] = assignment.action.toggle;
		entry["label"] = assignment.label;
		keys.append(entry);
	}

	QJsonObject root;
	root["version"] = formatVersion;
	root["keys"] = keys;
	return QString::fromUtf8(QJsonDocument(root).toJson(QJsonDocument::Compact));
}

Mapping MappingFromJson(const QString &json)
{
	Mapping mapping;

	QJsonDocument document = QJsonDocument::fromJson(json.toUtf8());
	if (!document.isObject()) {
		return mapping;
	}

	const QJsonArray keys = document.object()["keys"].toArray();
	for (const QJsonValue &value : keys) {
		QJsonObject entry = value.toObject();

		int key = entry["key"].toInt(-1);
		if (key < 0 || key >= maximumKeys) {
			continue;
		}

		KeyAssignment assignment;
		assignment.action.owner = OwnerNamed(entry["owner"].toString());
		assignment.action.context = entry["context"].toString();
		assignment.action.contextName = entry["contextName"].toString();
		assignment.action.name = entry["name"].toString();
		assignment.action.toggle = entry["toggle"].toBool();
		assignment.label = entry["label"].toString();

		if (assignment.action.IsValid()) {
			mapping[key] = assignment;
		}
	}

	return mapping;
}

} // namespace OBS::Accessibility::StreamDeck
