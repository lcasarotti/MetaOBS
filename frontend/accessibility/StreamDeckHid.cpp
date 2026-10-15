#include "StreamDeckHid.hpp"

#include <util/base.h>

#include <algorithm>
#include <mutex>

#include <windows.h>
#include <hidsdi.h>
#include <setupapi.h>
#include <tlhelp32.h>

namespace OBS::Accessibility::StreamDeck {

namespace {
constexpr DWORD writeTimeoutMilliseconds = 1000;

class Handle {
public:
	explicit Handle(HANDLE handle_ = INVALID_HANDLE_VALUE) : handle(handle_) {}
	~Handle()
	{
		if (IsValid()) {
			CloseHandle(handle);
		}
	}

	Handle(const Handle &) = delete;
	Handle &operator=(const Handle &) = delete;

	void Reset(HANDLE newHandle)
	{
		if (IsValid()) {
			CloseHandle(handle);
		}
		handle = newHandle;
	}

	bool IsValid() const { return handle != INVALID_HANDLE_VALUE && handle != nullptr; }
	HANDLE Get() const { return handle; }

private:
	HANDLE handle;
};

const Model *ModelAt(const wchar_t *path)
{
	/* Opening without read or write access is enough to ask for the IDs,
	 * and works for devices that other programs hold. */
	Handle device(CreateFileW(path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr));
	if (!device.IsValid()) {
		return nullptr;
	}

	HIDD_ATTRIBUTES attributes = {};
	attributes.Size = sizeof(attributes);
	if (!HidD_GetAttributes(device.Get(), &attributes)) {
		return nullptr;
	}

	return FindModel(attributes.VendorID, attributes.ProductID);
}

std::vector<uint8_t> Padded(const std::vector<uint8_t> &report, size_t length)
{
	std::vector<uint8_t> padded(report);
	padded.resize(std::max(length, report.size()), 0);
	return padded;
}
} // namespace

struct HidDevice::Handles {
	Handle file;
	Handle readEvent;
	Handle writeEvent;
	Handle cancelEvent;
	size_t inputLength = 0;
	size_t outputLength = 0;
	size_t featureLength = 0;
	std::mutex writeMutex;
};

std::vector<DeviceInfo> EnumerateDevices()
{
	std::vector<DeviceInfo> devices;

	GUID hidGuid = {};
	HidD_GetHidGuid(&hidGuid);

	HDEVINFO deviceSet = SetupDiGetClassDevsW(&hidGuid, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
	if (deviceSet == INVALID_HANDLE_VALUE) {
		return devices;
	}

	SP_DEVICE_INTERFACE_DATA interfaceData = {};
	interfaceData.cbSize = sizeof(interfaceData);

	for (DWORD index = 0; SetupDiEnumDeviceInterfaces(deviceSet, nullptr, &hidGuid, index, &interfaceData);
	     index++) {
		DWORD size = 0;
		SetupDiGetDeviceInterfaceDetailW(deviceSet, &interfaceData, nullptr, 0, &size, nullptr);
		if (size == 0) {
			continue;
		}

		std::vector<uint8_t> buffer(size, 0);
		auto detail = reinterpret_cast<SP_DEVICE_INTERFACE_DETAIL_DATA_W *>(buffer.data());
		detail->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
		if (!SetupDiGetDeviceInterfaceDetailW(deviceSet, &interfaceData, detail, size, nullptr, nullptr)) {
			continue;
		}

		const Model *model = ModelAt(detail->DevicePath);
		if (model) {
			devices.push_back({detail->DevicePath, model});
		}
	}

	SetupDiDestroyDeviceInfoList(deviceSet);
	return devices;
}

std::unique_ptr<HidDevice> HidDevice::Open(const DeviceInfo &info)
{
	auto handles = std::make_unique<Handles>();

	HANDLE file = CreateFileW(info.path.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
				  nullptr, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, nullptr);
	handles->file.Reset(file);
	if (!handles->file.IsValid()) {
		blog(LOG_WARNING, "[StreamDeck] Could not open %s: error %lu", info.model->name, GetLastError());
		return nullptr;
	}

	PHIDP_PREPARSED_DATA preparsed = nullptr;
	if (!HidD_GetPreparsedData(file, &preparsed)) {
		return nullptr;
	}

	HIDP_CAPS caps = {};
	NTSTATUS status = HidP_GetCaps(preparsed, &caps);
	HidD_FreePreparsedData(preparsed);
	if (status != HIDP_STATUS_SUCCESS) {
		return nullptr;
	}

	handles->inputLength = caps.InputReportByteLength;
	handles->outputLength = caps.OutputReportByteLength;
	handles->featureLength = caps.FeatureReportByteLength;
	if (handles->inputLength == 0) {
		return nullptr;
	}

	handles->readEvent.Reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
	handles->writeEvent.Reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
	handles->cancelEvent.Reset(CreateEventW(nullptr, TRUE, FALSE, nullptr));
	if (!handles->readEvent.IsValid() || !handles->writeEvent.IsValid() || !handles->cancelEvent.IsValid()) {
		return nullptr;
	}

	blog(LOG_INFO, "[StreamDeck] Opened %s (input %zu, output %zu, feature %zu bytes)", info.model->name,
	     handles->inputLength, handles->outputLength, handles->featureLength);

	return std::unique_ptr<HidDevice>(new HidDevice(info.model, std::move(handles)));
}

HidDevice::HidDevice(const Model *model_, std::unique_ptr<Handles> handles_)
	: model(model_),
	  handles(std::move(handles_))
{
}

HidDevice::~HidDevice() = default;

bool HidDevice::Read(std::vector<uint8_t> &report)
{
	report.assign(handles->inputLength, 0);

	OVERLAPPED overlapped = {};
	overlapped.hEvent = handles->readEvent.Get();
	ResetEvent(overlapped.hEvent);

	HANDLE file = handles->file.Get();
	if (!ReadFile(file, report.data(), static_cast<DWORD>(report.size()), nullptr, &overlapped) &&
	    GetLastError() != ERROR_IO_PENDING) {
		return false;
	}

	HANDLE events[] = {overlapped.hEvent, handles->cancelEvent.Get()};
	DWORD wait = WaitForMultipleObjects(2, events, FALSE, INFINITE);

	DWORD transferred = 0;
	if (wait != WAIT_OBJECT_0) {
		CancelIoEx(file, &overlapped);
		GetOverlappedResult(file, &overlapped, &transferred, TRUE);
		return false;
	}

	if (!GetOverlappedResult(file, &overlapped, &transferred, FALSE)) {
		return false;
	}

	report.resize(transferred);
	return true;
}

void HidDevice::Cancel()
{
	SetEvent(handles->cancelEvent.Get());
}

bool HidDevice::SetFeature(const std::vector<uint8_t> &report)
{
	std::lock_guard lock(handles->writeMutex);

	std::vector<uint8_t> padded = Padded(report, handles->featureLength);
	return HidD_SetFeature(handles->file.Get(), padded.data(), static_cast<ULONG>(padded.size()));
}

bool HidDevice::Write(const std::vector<uint8_t> &report)
{
	std::lock_guard lock(handles->writeMutex);

	std::vector<uint8_t> padded = Padded(report, handles->outputLength);

	OVERLAPPED overlapped = {};
	overlapped.hEvent = handles->writeEvent.Get();
	ResetEvent(overlapped.hEvent);

	HANDLE file = handles->file.Get();
	if (!WriteFile(file, padded.data(), static_cast<DWORD>(padded.size()), nullptr, &overlapped) &&
	    GetLastError() != ERROR_IO_PENDING) {
		return false;
	}

	DWORD transferred = 0;
	if (WaitForSingleObject(overlapped.hEvent, writeTimeoutMilliseconds) != WAIT_OBJECT_0) {
		CancelIoEx(file, &overlapped);
		GetOverlappedResult(file, &overlapped, &transferred, TRUE);
		return false;
	}

	return GetOverlappedResult(file, &overlapped, &transferred, FALSE) && transferred == padded.size();
}

bool IsElgatoAppRunning()
{
	Handle snapshot(CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0));
	if (!snapshot.IsValid()) {
		return false;
	}

	PROCESSENTRY32W entry = {};
	entry.dwSize = sizeof(entry);
	for (BOOL found = Process32FirstW(snapshot.Get(), &entry); found;
	     found = Process32NextW(snapshot.Get(), &entry)) {
		if (_wcsicmp(entry.szExeFile, L"StreamDeck.exe") == 0) {
			return true;
		}
	}
	return false;
}

} // namespace OBS::Accessibility::StreamDeck
