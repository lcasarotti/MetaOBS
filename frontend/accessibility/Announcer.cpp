#include "Announcer.hpp"
#include "AccessibleSettings.hpp"
#include "NvdaController.hpp"

#include <widgets/OBSBasic.hpp>

#include <qt-wrappers.hpp>

#include <QAccessible>
#include <QApplication>
#include <QTextDocumentFragment>

#include "moc_Announcer.cpp"

namespace OBS::Accessibility {

namespace {
Announcer *instance = nullptr;

constexpr qint64 duplicateWindowMs = 500;

QString ToPlainText(const QString &text)
{
	QString plainText = Qt::mightBeRichText(text) ? QTextDocumentFragment::fromHtml(text).toPlainText() : text;
	return plainText.simplified();
}

QWidget *FindAnnouncementTarget()
{
	if (QWidget *window = QApplication::activeWindow()) {
		return window;
	}

	if (QWidget *focusWidget = QApplication::focusWidget()) {
		return focusWidget->window();
	}

	OBSBasic *main = OBSBasic::Get();
	if (main && main->isVisible()) {
		return main;
	}

	for (QWidget *window : QApplication::topLevelWidgets()) {
		if (window->isVisible()) {
			return window;
		}
	}

	return nullptr;
}
} // namespace

Announcer::Announcer(QObject *parent) : QObject(parent), nvdaController(std::make_unique<NvdaController>())
{
	instance = this;
}

Announcer::~Announcer()
{
	if (instance == this) {
		instance = nullptr;
	}
}

Announcer *Announcer::Get()
{
	return instance;
}

void Announcer::Announce(const QString &text, Priority priority, bool force)
{
	QString plainText = ToPlainText(text);
	if (plainText.isEmpty() || (!force && !IsWanted(priority)) || IsDuplicate(plainText)) {
		return;
	}

	bool spoken = Speak(plainText, priority, Settings::GetSpeechBackend());

	blog(LOG_DEBUG, "[Accessibility] Announcement %s: %s", spoken ? "sent" : "not sent", QT_TO_UTF8(plainText));
}

bool Announcer::TestBackend(const QString &text, SpeechBackend backend)
{
	return Speak(ToPlainText(text), Priority::High, backend);
}

bool Announcer::Speak(const QString &text, Priority priority, SpeechBackend backend) const
{
	bool spoken = false;
	switch (backend) {
	case SpeechBackend::Automatic:
		/* UI Automation notifications are usually ignored by screen
		 * readers while OBS is in the background, so prefer NVDA's own
		 * library when OBS has no active window. */
		if (!QApplication::activeWindow()) {
			spoken = SpeakWithNvdaController(text, priority);
		}
		if (!spoken) {
			spoken = SpeakWithUIAutomation(text, priority);
		}
		break;
	case SpeechBackend::NvdaController:
		spoken = SpeakWithNvdaController(text, priority);
		if (!spoken) {
			spoken = SpeakWithUIAutomation(text, priority);
		}
		break;
	case SpeechBackend::UIAutomation:
		spoken = SpeakWithUIAutomation(text, priority);
		break;
	}

	return spoken;
}

void Announcer::AnnounceFromAnyThread(const QString &text, Priority priority)
{
	QMetaObject::invokeMethod(
		qApp,
		[text, priority]() {
			if (Announcer *announcer = Announcer::Get()) {
				announcer->Announce(text, priority);
			}
		},
		Qt::QueuedConnection);
}

bool Announcer::IsNvdaControllerLoaded() const
{
	return nvdaController->IsLibraryLoaded();
}

bool Announcer::IsNvdaControllerRunning() const
{
	return nvdaController->IsRunning();
}

bool Announcer::IsWanted(Priority priority) const
{
	switch (Settings::GetAnnounceLevel()) {
	case AnnounceLevel::Off:
		return false;
	case AnnounceLevel::Essential:
		return priority == Priority::High;
	case AnnounceLevel::Normal:
		return priority != Priority::Low;
	case AnnounceLevel::Verbose:
		return true;
	}

	return true;
}

bool Announcer::IsDuplicate(const QString &text)
{
	bool duplicate = text == lastText && lastTextTimer.isValid() &&
			 lastTextTimer.elapsed() < duplicateWindowMs;

	lastText = text;
	lastTextTimer.start();

	return duplicate;
}

bool Announcer::SpeakWithUIAutomation(const QString &text, Priority priority) const
{
	if (!QAccessible::isActive()) {
		return false;
	}

	QWidget *target = FindAnnouncementTarget();
	if (!target) {
		return false;
	}

	QAccessibleAnnouncementEvent event(target, text);
	event.setPoliteness(priority == Priority::High ? QAccessible::AnnouncementPoliteness::Assertive
						       : QAccessible::AnnouncementPoliteness::Polite);
	QAccessible::updateAccessibility(&event);

	return true;
}

bool Announcer::SpeakWithNvdaController(const QString &text, Priority priority) const
{
	return nvdaController->Speak(text, priority == Priority::High);
}

} // namespace OBS::Accessibility
