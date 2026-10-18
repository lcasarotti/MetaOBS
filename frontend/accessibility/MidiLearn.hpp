#pragma once

#include "MidiMapping.hpp"

#include <QObject>
#include <QPointer>

#include <vector>

class QTimer;

namespace OBS::Accessibility {

class MidiManager;

/* MIDI learn, as in music programs: Ctrl+Shift+M on a control of MetaOBS
 * (a row of the audio mixer, the T-bar, a setting of a filter...) waits for a
 * knob, fader or button of the controller, and assigns the first one moved to
 * that control. Escape cancels, Delete removes the control's assignment.
 * Installed as an application event filter, so that it works in every
 * window. */
class MidiLearn : public QObject {
	Q_OBJECT

public:
	MidiLearn(MidiManager *manager, QObject *parent);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	void Start(QWidget *widget);
	void Stop();
	void Cancel();
	void RemoveAssignments();
	void HandleMessage(const QString &port, const Midi::Message &message);
	void Finish();
	QString CurrentAssignments() const;

	QPointer<MidiManager> manager;
	bool learning = false;
	std::vector<Midi::Target> candidates;

	/* The control being moved, and what it sent so far. */
	Midi::Control control;
	Midi::GestureClassifier classifier;
	QTimer *gestureTimer;
	QTimer *timeoutTimer;
};

} // namespace OBS::Accessibility
