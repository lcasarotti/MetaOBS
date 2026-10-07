#pragma once

#include <QString>

namespace OBS::Accessibility {

/* Optional speech output through NVDA's controller client library
 * (nvdaControllerClient.dll, LGPL), loaded at runtime if it is placed next to
 * the OBS executable. Unlike UI Automation notifications it also speaks while
 * OBS is in the background. Available on Windows only. */
class NvdaController {
public:
	NvdaController();
	~NvdaController();

	NvdaController(const NvdaController &) = delete;
	NvdaController &operator=(const NvdaController &) = delete;

	bool IsLibraryLoaded() const;
	bool IsRunning() const;
	bool Speak(const QString &text, bool interrupt) const;

private:
	struct Library;
	Library *library = nullptr;
};

} // namespace OBS::Accessibility
