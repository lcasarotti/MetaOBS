#pragma once

#include "AccessibleSettings.hpp"

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <memory>

namespace OBS::Accessibility {

class NvdaController;

/* How important an announcement is. The user's announcement level decides
 * which priorities are spoken: Essential speaks only High, Normal speaks High
 * and Normal, Verbose speaks everything. */
enum class Priority {
	Low,
	Normal,
	High,
};

class Announcer : public QObject {
	Q_OBJECT

public:
	explicit Announcer(QObject *parent = nullptr);
	~Announcer();

	static Announcer *Get();

	/* Must be called on the UI thread. Set force to speak regardless of the
	 * announcement level (used for explicit user requests). */
	void Announce(const QString &text, Priority priority = Priority::Normal, bool force = false);

	/* Stops the announcement being spoken. Only NVDA allows it. */
	void Interrupt();

	/* Stops what is being spoken and speaks the text at once, whatever the
	 * announcement level and even if it repeats the last announcement: for
	 * the result of a key the user has just pressed. */
	void AnnounceAtOnce(const QString &text);

	/* Can be called from any thread. */
	static void AnnounceFromAnyThread(const QString &text, Priority priority = Priority::Normal);

	/* Speaks through the given backend regardless of the user's settings.
	 * Returns whether a backend accepted the text. */
	bool TestBackend(const QString &text, SpeechBackend backend);

	bool IsNvdaControllerLoaded() const;
	bool IsNvdaControllerRunning() const;

private:
	bool Speak(const QString &text, Priority priority, SpeechBackend backend) const;
	bool IsWanted(Priority priority) const;
	bool IsDuplicate(const QString &text);
	bool SpeakWithUIAutomation(const QString &text, Priority priority) const;
	bool SpeakWithNvdaController(const QString &text, Priority priority) const;

	std::unique_ptr<NvdaController> nvdaController;
	QString lastText;
	QElapsedTimer lastTextTimer;
};

} // namespace OBS::Accessibility
