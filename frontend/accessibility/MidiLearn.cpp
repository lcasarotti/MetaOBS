#include "MidiLearn.hpp"
#include "Announcer.hpp"
#include "MidiManager.hpp"
#include "MidiTargets.hpp"

#include <OBSApp.hpp>

#include <qt-wrappers.hpp>

#include <QKeyEvent>
#include <QTimer>
#include <QWidget>

#include <algorithm>

#include "moc_MidiLearn.cpp"

namespace OBS::Accessibility {

namespace {
/* How long the first messages of a control are collected to tell a button
 * from a knob. */
constexpr int gestureMilliseconds = 500;
constexpr int timeoutMilliseconds = 20000;

void Speak(const QString &text)
{
	if (Announcer *announcer = Announcer::Get()) {
		announcer->AnnounceAtOnce(text);
	}
}

bool IsLearnShortcut(const QKeyEvent *event)
{
	Qt::KeyboardModifiers modifiers = event->modifiers() & ~Qt::KeypadModifier;
	return event->key() == Qt::Key_M && modifiers == (Qt::ControlModifier | Qt::ShiftModifier);
}

bool IsRelease(const Midi::Message &message)
{
	return message.type == Midi::MessageType::NoteOff ||
	       (message.type == Midi::MessageType::NoteOn && message.value == 0);
}
} // namespace

MidiLearn::MidiLearn(MidiManager *manager_, QObject *parent) : QObject(parent), manager(manager_)
{
	gestureTimer = new QTimer(this);
	gestureTimer->setSingleShot(true);
	gestureTimer->setInterval(gestureMilliseconds);
	connect(gestureTimer, &QTimer::timeout, this, &MidiLearn::Finish);

	timeoutTimer = new QTimer(this);
	timeoutTimer->setSingleShot(true);
	timeoutTimer->setInterval(timeoutMilliseconds);
	connect(timeoutTimer, &QTimer::timeout, this, [this]() {
		Stop();
		Speak(QTStr("Accessibility.Midi.Learn.TimedOut"));
	});
}

bool MidiLearn::eventFilter(QObject *watched, QEvent *event)
{
	if (event->type() != QEvent::KeyPress && event->type() != QEvent::ShortcutOverride) {
		return QObject::eventFilter(watched, event);
	}
	if (!watched->isWidgetType()) {
		return QObject::eventFilter(watched, event);
	}

	auto keyEvent = static_cast<QKeyEvent *>(event);
	bool shortcut = IsLearnShortcut(keyEvent);
	Qt::KeyboardModifiers modifiers = keyEvent->modifiers() & ~Qt::KeypadModifier;
	bool escape = learning && keyEvent->key() == Qt::Key_Escape && modifiers == Qt::NoModifier;
	bool remove = learning && keyEvent->key() == Qt::Key_Delete && modifiers == Qt::NoModifier;
	if (!shortcut && !escape && !remove) {
		return QObject::eventFilter(watched, event);
	}

	/* Claims the keys before any shortcut or dialog sees them. */
	if (event->type() == QEvent::ShortcutOverride) {
		event->accept();
		return true;
	}
	if (keyEvent->isAutoRepeat()) {
		return true;
	}

	if (escape || (shortcut && learning)) {
		Cancel();
	} else if (remove) {
		RemoveAssignments();
	} else {
		/* Keys go to the focused widget of their window. */
		Start(static_cast<QWidget *>(watched));
	}
	return true;
}

void MidiLearn::Start(QWidget *widget)
{
	if (!manager || !widget) {
		return;
	}

	if (!manager->IsEnabled()) {
		Speak(QTStr("Accessibility.Midi.Learn.Disabled"));
		return;
	}

	candidates = Midi::TargetsForWidget(widget);
	if (candidates.empty()) {
		Speak(QTStr("Accessibility.Midi.Learn.NotMappable"));
		return;
	}

	bool anyOpen = std::any_of(manager->InputNames().begin(), manager->InputNames().end(),
				   [this](const QString &input) { return manager->IsInputOpen(input); });
	if (!anyOpen) {
		candidates.clear();
		Speak(QTStr("Accessibility.Midi.Learn.NoController"));
		return;
	}

	learning = true;
	control = {};
	classifier.Clear();
	manager->Capture();
	connect(manager, &MidiManager::MessageReceived, this, &MidiLearn::HandleMessage);
	timeoutTimer->start();

	QString prompt;
	const Midi::Target *continuous = nullptr;
	const Midi::Target *button = nullptr;
	for (const Midi::Target &target : candidates) {
		(target.IsContinuous() ? continuous : button) = &target;
	}
	if (continuous && button) {
		prompt = QTStr("Accessibility.Midi.Learn.StartBoth")
				 .arg(Midi::DescribeTarget(*continuous), Midi::DescribeTarget(*button));
	} else if (continuous) {
		prompt = QTStr("Accessibility.Midi.Learn.StartContinuous").arg(Midi::DescribeTarget(*continuous));
	} else {
		prompt = QTStr("Accessibility.Midi.Learn.StartButton").arg(Midi::DescribeTarget(*button));
	}

	QString current = CurrentAssignments();
	if (!current.isEmpty()) {
		prompt += " " + current;
	}
	prompt += " " + QTStr("Accessibility.Midi.Learn.Keys");
	Speak(prompt);
}

void MidiLearn::Stop()
{
	if (!learning) {
		return;
	}

	learning = false;
	candidates.clear();
	control = {};
	classifier.Clear();
	gestureTimer->stop();
	timeoutTimer->stop();
	if (manager) {
		disconnect(manager, &MidiManager::MessageReceived, this, &MidiLearn::HandleMessage);
		manager->Release();
	}
}

void MidiLearn::Cancel()
{
	Stop();
	Speak(QTStr("Accessibility.Midi.Learn.Cancelled"));
}

void MidiLearn::RemoveAssignments()
{
	if (!manager) {
		return;
	}

	Midi::Mapping mapping = manager->GetMapping();
	QStringList removed;
	for (const Midi::Target &target : candidates) {
		int index = Midi::FindTarget(mapping, target);
		while (index >= 0) {
			removed << Midi::DescribeControl(mapping[static_cast<size_t>(index)].control);
			mapping.erase(mapping.begin() + index);
			index = Midi::FindTarget(mapping, target);
		}
	}

	Stop();
	if (removed.isEmpty()) {
		Speak(QTStr("Accessibility.Midi.Learn.NothingToRemove"));
		return;
	}
	manager->SetMapping(mapping);
	QString text = QTStr("Accessibility.Midi.Learn.Removed").arg(removed.join(QStringLiteral("; ")));
	blog(LOG_INFO, "[MIDI] %s", QT_TO_UTF8(text));
	Speak(text);
}

void MidiLearn::HandleMessage(const QString &port, const Midi::Message &message)
{
	Midi::Control incoming = Midi::ControlOf(port, message);
	if (!incoming.IsValid()) {
		return;
	}

	if (!control.IsValid()) {
		/* The release of a key pressed before learning started. */
		if (IsRelease(message)) {
			return;
		}
		control = incoming;
		classifier.Clear();
		gestureTimer->start();
	}

	if (incoming == control) {
		classifier.Add(message);
	}
}

void MidiLearn::Finish()
{
	if (!learning || !manager || !control.IsValid()) {
		return;
	}

	Midi::Gesture gesture = classifier.Result();
	bool continuous = gesture == Midi::Gesture::Continuous;
	auto chosen = std::find_if(candidates.begin(), candidates.end(), [continuous](const Midi::Target &target) {
		return target.IsContinuous() == continuous;
	});

	if (chosen == candidates.end()) {
		Speak(QTStr(continuous ? "Accessibility.Midi.Learn.NeedButton" : "Accessibility.Midi.Learn.NeedKnob"));
		control = {};
		classifier.Clear();
		timeoutTimer->start();
		return;
	}

	Midi::Assignment assignment;
	assignment.control = control;
	assignment.target = *chosen;
	assignment.options.encoder = classifier.Encoder();
	assignment.options.takeover = assignment.options.encoder == Midi::EncoderMode::Absolute;

	Midi::Mapping mapping = manager->GetMapping();
	QString replaced;
	int index = Midi::FindControl(mapping, control);
	if (index >= 0) {
		const Midi::Target &old = mapping[static_cast<size_t>(index)].target;
		if (!old.SameAs(assignment.target)) {
			replaced = Midi::DescribeTarget(old);
		}
		mapping.erase(mapping.begin() + index);
	}
	/* A control learned for a target replaces the one it had. */
	index = Midi::FindTarget(mapping, assignment.target);
	while (index >= 0) {
		mapping.erase(mapping.begin() + index);
		index = Midi::FindTarget(mapping, assignment.target);
	}
	mapping.push_back(assignment);

	Stop();
	manager->SetMapping(mapping);

	blog(LOG_INFO, "[MIDI] Learned %s for %s", QT_TO_UTF8(Midi::DescribeControl(assignment.control, true)),
	     QT_TO_UTF8(Midi::DescribeTarget(assignment.target)));

	QString text = QTStr("Accessibility.Midi.Learn.Assigned")
			       .arg(Midi::DescribeControl(assignment.control), Midi::DescribeTarget(assignment.target));
	if (assignment.options.encoder != Midi::EncoderMode::Absolute) {
		text += " " + QTStr("Accessibility.Midi.Learn.Encoder");
	}
	if (!replaced.isEmpty()) {
		text += " " + QTStr("Accessibility.Midi.Learn.Replaced").arg(replaced);
	}
	Speak(text);
}

QString MidiLearn::CurrentAssignments() const
{
	if (!manager) {
		return {};
	}

	QStringList controls;
	for (const Midi::Target &target : candidates) {
		for (const Midi::Assignment &assignment : manager->GetMapping()) {
			if (assignment.target.SameAs(target)) {
				controls << Midi::DescribeControl(assignment.control);
			}
		}
	}
	if (controls.isEmpty()) {
		return {};
	}
	return QTStr("Accessibility.Midi.Learn.Current").arg(controls.join(QStringLiteral("; ")));
}

} // namespace OBS::Accessibility
