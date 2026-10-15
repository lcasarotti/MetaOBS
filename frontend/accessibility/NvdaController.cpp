#include "NvdaController.hpp"

#include <util/base.h>

#include <string>

#ifdef _WIN32
#include <windows.h>
#endif

namespace OBS::Accessibility {

#ifdef _WIN32
struct NvdaController::Library {
	using TestIfRunning = unsigned long(__stdcall *)();
	using SpeakText = unsigned long(__stdcall *)(const wchar_t *);
	using CancelSpeech = unsigned long(__stdcall *)();

	HMODULE module = nullptr;
	TestIfRunning testIfRunning = nullptr;
	SpeakText speakText = nullptr;
	CancelSpeech cancelSpeech = nullptr;
};

NvdaController::NvdaController()
{
	const wchar_t *libraryNames[] = {L"nvdaControllerClient.dll", L"nvdaControllerClient64.dll"};

	for (const wchar_t *name : libraryNames) {
		HMODULE module = LoadLibraryExW(name, nullptr, LOAD_LIBRARY_SEARCH_APPLICATION_DIR);
		if (!module) {
			continue;
		}

		auto candidate = new Library;
		candidate->module = module;
		candidate->testIfRunning = reinterpret_cast<Library::TestIfRunning>(
			GetProcAddress(module, "nvdaController_testIfRunning"));
		candidate->speakText =
			reinterpret_cast<Library::SpeakText>(GetProcAddress(module, "nvdaController_speakText"));
		candidate->cancelSpeech =
			reinterpret_cast<Library::CancelSpeech>(GetProcAddress(module, "nvdaController_cancelSpeech"));

		if (candidate->testIfRunning && candidate->speakText && candidate->cancelSpeech) {
			library = candidate;
			blog(LOG_INFO, "[Accessibility] Loaded NVDA controller client");
			return;
		}

		FreeLibrary(module);
		delete candidate;
	}
}

NvdaController::~NvdaController()
{
	if (library) {
		FreeLibrary(library->module);
		delete library;
	}
}

bool NvdaController::IsLibraryLoaded() const
{
	return library != nullptr;
}

bool NvdaController::IsRunning() const
{
	return library && library->testIfRunning() == 0;
}

bool NvdaController::Speak(const QString &text, bool interrupt) const
{
	if (!IsRunning()) {
		return false;
	}

	if (interrupt) {
		library->cancelSpeech();
	}

	std::wstring wideText = text.toStdWString();
	return library->speakText(wideText.c_str()) == 0;
}

void NvdaController::Cancel() const
{
	if (IsRunning()) {
		library->cancelSpeech();
	}
}
#else
struct NvdaController::Library {};

NvdaController::NvdaController() = default;
NvdaController::~NvdaController() = default;

bool NvdaController::IsLibraryLoaded() const
{
	return false;
}

bool NvdaController::IsRunning() const
{
	return false;
}

bool NvdaController::Speak(const QString &, bool) const
{
	return false;
}

void NvdaController::Cancel() const {}
#endif

} // namespace OBS::Accessibility
