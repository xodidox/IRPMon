
#include <windows.h>
#include "debug.h"
#include "ioctls.h"
#include "kernel-shared.h"
#include "irpmondll-types.h"
#include "driver-com.h"

/************************************************************************/
/*                           GLOBAL VARIABLES                           */
/************************************************************************/

static HANDLE _deviceHandle = INVALID_HANDLE_VALUE;
static BOOLEAN _initialized = FALSE;
static BOOLEAN _connected = FALSE;

/************************************************************************/
/*                          HELPER ROUTINES                             */
/************************************************************************/

static DWORD _SynchronousNoIOIOCTL(DWORD Code)
{
	DWORD dummy = 0;
	DWORD ret = ERROR_GEN_FAILURE;
	DEBUG_ENTER_FUNCTION("Code=0x%x", Code);

	if (DeviceIoControl(_deviceHandle, Code, NULL, 0, NULL, 0, &dummy, NULL))
		ret = ERROR_SUCCESS;
	else ret = GetLastError();

	DEBUG_EXIT_FUNCTION("%u", ret);
	return ret;
}

static DWORD _SynchronousWriteIOCTL(DWORD Code, PVOID InputBuffer, ULONG InputBufferLength)
{
	DWORD dummy = 0;
	DWORD ret = ERROR_GEN_FAILURE;
	DEBUG_ENTER_FUNCTION("Code=0x%x; InputBuffer=0x%p; InputBufferLength=%u", Code, InputBuffer, InputBufferLength);

	if (DeviceIoControl(_deviceHandle, Code, InputBuffer, InputBufferLength, NULL, 0, &dummy, NULL))
		ret = ERROR_SUCCESS;
	else ret = GetLastError();

	DEBUG_EXIT_FUNCTION("%u", ret);
	return ret;
}

static DWORD _SynchronousReadIOCTL(DWORD Code, PVOID OutputBuffer, ULONG OutputBufferLength)
{
	DWORD dummy = 0;
	DWORD ret = ERROR_GEN_FAILURE;
	DEBUG_ENTER_FUNCTION("Code=0x%x; OutputBuffer=0x%p; OutputBufferLength=%u", Code, OutputBuffer, OutputBufferLength);

	if (DeviceIoControl(_deviceHandle, Code, NULL, 0, OutputBuffer, OutputBufferLength, &dummy, NULL))
		ret = ERROR_SUCCESS;
	else ret = GetLastError();

	DEBUG_EXIT_FUNCTION("%u", ret);
	return ret;
}

static DWORD _SynchronousOtherIOCTL(DWORD Code, PVOID InputBuffer, ULONG InputBufferLength, PVOID OutputBuffer, ULONG OutputBufferLength)
{
	DWORD dummy = 0;
	DWORD ret = ERROR_GEN_FAILURE;
	DEBUG_ENTER_FUNCTION("Code=0x%x; InputBuffer=0x%p; InputBufferLength=%u; OutputBuffer=0x%p; OutputBufferLength=%u", Code, InputBuffer, InputBufferLength, OutputBuffer, OutputBufferLength);

	if (DeviceIoControl(_deviceHandle, Code, InputBuffer, InputBufferLength, OutputBuffer, OutputBufferLength, &dummy, NULL))
		ret = ERROR_SUCCESS;
	else ret = GetLastError();

	DEBUG_EXIT_FUNCTION("%u", ret);
	return ret;
}

static PIRPMON_DRIVER_INFO _DriverInfoAlloc(PVOID DriverObject, PWCHAR Drivername, ULONG DriverNameLen, ULONG DeviceCount)
{
	PIRPMON_DRIVER_INFO ret = NULL;
	SIZE_T size = sizeof(IRPMON_DRIVER_INFO);
	DEBUG_ENTER_FUNCTION("DriverObject=0x%p; DriverName=0x%p; DriverNameLen=%u; DeviceCount=%u", DriverObject, Drivername, DriverNameLen, DeviceCount);

	size += DriverNameLen + sizeof(WCHAR) + DeviceCount*sizeof(PIRPMON_DEVICE_INFO);
	ret = (PIRPMON_DRIVER_INFO)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
	if (ret != NULL) {
		ret->DriverObject = DriverObject;
		ret->DeviceCount = DeviceCount;
		ret->DriverName = (PWCHAR)(ret + 1);
		memcpy(ret->DriverName, Drivername, DriverNameLen);
		ret->DriverName[DriverNameLen / sizeof(WCHAR)] = L'\0';
		ret->Devices = (PIRPMON_DEVICE_INFO *)((PUCHAR)ret->DriverName + DriverNameLen + sizeof(WCHAR));
	}

	DEBUG_EXIT_FUNCTION("0x%p", ret);
	return ret;
}

static VOID _DriverInfoFree(PIRPMON_DRIVER_INFO Info)
{
	DEBUG_ENTER_FUNCTION("Info=0x%p", Info);

	HeapFree(GetProcessHeap(), 0, Info);

	DEBUG_EXIT_FUNCTION_VOID();
	return;
}

static PIRPMON_DEVICE_INFO _DeviceInfoAlloc(PVOID DeviceObject, PVOID AttachedDevice, PWCHAR DeviceName, ULONG DeviceNameLen)
{
	PIRPMON_DEVICE_INFO ret = NULL;
	SIZE_T size = sizeof(IRPMON_DEVICE_INFO);
	DEBUG_ENTER_FUNCTION("DeviceObject=0x%p; AttachedDevice=0x%p; DeviceName=0x%p; DeviceNameLen=%u", DeviceObject, AttachedDevice, DeviceName, DeviceNameLen);

	size += DeviceNameLen + sizeof(WCHAR);
	ret = (PIRPMON_DEVICE_INFO)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, size);
	if (ret != NULL) {
		ret->DeviceObject = DeviceObject;
		ret->AttachedDevice = AttachedDevice;
		ret->Name = (PWCHAR)(ret + 1);
		memcpy(ret->Name, DeviceName, DeviceNameLen);
		ret->Name[DeviceNameLen / sizeof(WCHAR)] = L'\0';
	}

	DEBUG_EXIT_FUNCTION("0x%x", ret);
	return ret;
}

static VOID _DeviceInfoFree(PIRPMON_DEVICE_INFO Info)
{
	DEBUG_ENTER_FUNCTION("Info=0x%p", Info);

	HeapFree(GetProcessHeap(), 0, Info);

	DEBUG_EXIT_FUNCTION_VOID();
	return;
}

static PWCHAR _CopyString(PWCHAR Str)
{
	PWCHAR ret = NULL;
	SIZE_T len = (Str != NULL ? wcslen(Str)*sizeof(WCHAR) : 0);

	ret = (PWCHAR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, len + sizeof(WCHAR));
	if (ret != NULL) {
		memcpy(ret, Str, len);
		ret[len / sizeof(WCHAR)] = L'\0';
	}

	return ret;
}

static DWORD _ObjectOpen(EHandletype ObjectType, PVOID ID, PHANDLE Handle)
{
	DWORD ret = ERROR_GEN_FAILURE;
	IOCTL_IRPMONDRV_HOOK_OPEN_INPUT input;
	IOCTL_IRPMONDRV_HOOK_OPEN_OUTPUT output;
	DEBUG_ENTER_FUNCTION("ObjectType=%u; ID=0x%p; Handle=0x%p", ObjectType, ID, Handle);

	input.ObjectId = ID;
	input.ObjectType = ObjectType;
	ret = _SynchronousOtherIOCTL(IOCTL_IRPMONDRV_HOOK_OPEN, &input, sizeof(input), &output, sizeof(output));
	if (ret == ERROR_SUCCESS)
		*Handle = output.Handle;

	DEBUG_EXIT_FUNCTION("%u", ret);
	return ret;
}

static DWORD _ObjectClose(EHandletype ObjectType, HANDLE Handle)
{
	DWORD ret = ERROR_SUCCESS;
	IOCTL_IRPMONDRV_HOOK_CLOSE_INPUT input;
	DEBUG_ENTER_FUNCTION("ObjectType=%u; Handle=0x%p", ObjectType, Handle);

	input.Handle = Handle;
	input.ObjectType = ObjectType;
	ret = _SynchronousWriteIOCTL(IOCTL_IRPMONDRV_HOOK_CLOSE, &input, sizeof(input));

	DEBUG_EXIT_FUNCTION("%u", ret);
	return ret;
}

/************************************************************************/
/*                          PUBLIC ROUTINES                             */
/************************************************************************/

DWORD DriverComSnapshotRetrieve(PIRPMON_DRIVER_INFO **DriverInfo, PULONG InfoCount)
{
	DWORD outputBufferLength = 512;
	PVOID outputBuffer = NULL;
	ULONG tmpInfoArrayCount = 0;
	PIRPMON_DRIVER_INFO *tmpInfoArray = NULL;
	DWORD ret = ERROR_GEN_FAILURE;
	DEBUG_ENTER_FUNCTION("DriverInfo=0x%p; InfoCount=0x%p", DriverInfo, InfoCount);

	do {
		outputBuffer = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, outputBufferLength);
		if (outputBuffer != NULL) {
			ret = _SynchronousReadIOCTL(IOCTL_IRPMNDRV_GET_DRIVER_DEVICE_INFO, outputBuffer, outputBufferLength);
			if (ret != ERROR_SUCCESS) {
				HeapFree(GetProcessHeap(), 0, outputBuffer);
				if (ret == ERROR_INSUFFICIENT_BUFFER)
					outputBufferLength *= 2;
			}
		} else ret = GetLastError();
	} while (ret == ERROR_INSUFFICIENT_BUFFER);

	if (ret == ERROR_SUCCESS) {
		PUCHAR tmpBuffer = (PUCHAR)outputBuffer;

		tmpInfoArrayCount = *(PULONG)tmpBuffer;
		tmpBuffer += sizeof(ULONG);
		tmpInfoArray = (PIRPMON_DRIVER_INFO *)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(PIRPMON_DRIVER_INFO)*tmpInfoArrayCount);
		if (tmpInfoArray != NULL) {
			ULONG i = 0;

			for (i = 0; i < tmpInfoArrayCount; ++i) {
				PVOID driverObject = NULL;
				ULONG deviceCount = 0;
				ULONG driverNameLen = 0;
				
				driverObject = *(PVOID *)tmpBuffer;
				tmpBuffer += sizeof(driverObject);
				deviceCount = *(PULONG)tmpBuffer;
				tmpBuffer += sizeof(deviceCount);
				driverNameLen = *(PULONG)tmpBuffer;
				tmpBuffer += sizeof(driverNameLen);
				tmpInfoArray[i] = _DriverInfoAlloc(driverObject, (PWCHAR)tmpBuffer, driverNameLen, deviceCount);
				if (tmpInfoArray[i] != NULL) {
					ULONG j = 0;
					PIRPMON_DRIVER_INFO driverInfo = tmpInfoArray[i];
					
					tmpBuffer += driverNameLen;
					for (j = 0; j < deviceCount; ++j) {
						PVOID deviceObject = NULL;
						PVOID attachedDevice = NULL;
						ULONG deviceNameLen = 0;
						
						deviceObject = *(PVOID *)tmpBuffer;
						tmpBuffer += sizeof(deviceObject);
						attachedDevice = *(PVOID *)tmpBuffer;
						tmpBuffer += sizeof(attachedDevice);
						deviceNameLen = *(PULONG)tmpBuffer;
						tmpBuffer += sizeof(deviceNameLen);
						driverInfo->Devices[j] = _DeviceInfoAlloc(deviceObject, attachedDevice, (PWCHAR)tmpBuffer, deviceNameLen);
						if (driverInfo->Devices[j] == NULL) {
							ULONG k = 0;

							ret = ERROR_NOT_ENOUGH_MEMORY;
							for (k = 0; k < j; ++k)
								_DeviceInfoFree(driverInfo->Devices[k]);

							break;
						}

						tmpBuffer += deviceNameLen;
					}
				} else ret = ERROR_NOT_ENOUGH_MEMORY;
			
				if (ret != ERROR_SUCCESS) {
					ULONG k = 0;

					for (k = 0; k < i; ++k) {
						ULONG l = 0;
						PIRPMON_DRIVER_INFO driverInfo = tmpInfoArray[k];
					
						for (l = 0; l < driverInfo->DeviceCount; ++l)
							_DeviceInfoFree(driverInfo->Devices[l]);

						_DriverInfoFree(driverInfo);
					}

					break;
				}
			}
		
			if (ret == ERROR_SUCCESS) {
				*DriverInfo = tmpInfoArray;
				*InfoCount = tmpInfoArrayCount;
			}

			if (ret != ERROR_SUCCESS)
				HeapFree(GetProcessHeap(), 0, tmpInfoArray);
		} else ret = GetLastError();

		HeapFree(GetProcessHeap(), 0, outputBuffer);
	}

	DEBUG_EXIT_FUNCTION("%u, *DriverInfo=0x%p, *InfoCount=%u", ret, *DriverInfo, *InfoCount);
	return ret;
}

VOID DriverComSnapshotFree(PIRPMON_DRIVER_INFO *DriverInfo, ULONG Count)
{
	ULONG i = 0;
	DEBUG_ENTER_FUNCTION("DriverInfo=0x%p; Count=%u", DriverInfo, Count);

	for (i = 0; i < Count; ++i) {
		ULONG j = 0;
		PIRPMON_DRIVER_INFO drvInfo = DriverInfo[i];
	
		for (j = 0; j < drvInfo->DeviceCount; ++j)
			_DeviceInfoFree(drvInfo->Devices[j]);

		_DriverInfoFree(drvInfo);
	}

	HeapFree(GetProcessHeap(), 0, DriverInfo);

	DEBUG_EXIT_FUNCTION_VOID();
	return;
}

DWORD DriverComHookDriver(PWCHAR DriverName, PDRIVER_MONITOR_SETTINGS MonitorSettings, PHANDLE HookHandle)
{
	DWORD ret = ERROR_GEN_FAILURE;
	IOCTL_IRPMNDRV_HOOK_DRIVER_INPUT input;
	IOCTL_IRPMNDRV_HOOK_DRIVER_OUTPUT output;
	DEBUG_ENTER_FUNCTION("DriverName=\"%S\"; MonitorSettings=0x%p; HookHandle=0x%p", DriverName, MonitorSettings, HookHandle);

	input.DriverName = _CopyString(DriverName);
	if (input.DriverName != NULL) {
		input.DriverNameLength = (ULONG)wcslen(input.DriverName)*sizeof(WCHAR);
		input.MonitorSettings = *MonitorSettings;
		ret = _SynchronousOtherIOCTL(IOCTL_IRPMNDRV_HOOK_DRIVER, &input, sizeof(input), &output, sizeof(output));
		if (ret == ERROR_SUCCESS)
			*HookHandle = output.HookHandle;

		HeapFree(GetProcessHeap(), 0, input.DriverName);
	} else ret = ERROR_NOT_ENOUGH_MEMORY;

	DEBUG_EXIT_FUNCTION("0x%x, *Hookandle=0x%p", ret, *HookHandle);
	return ret;
}

DWORD DriverComHookedDriverSetInfo(HANDLE Driverhandle, PDRIVER_MONITOR_SETTINGS Settings)
{
	IOCTL_IRPMNDRV_HOOK_DRIVER_SET_INFO_INPUT input;
	DWORD ret = ERROR_GEN_FAILURE;
	DEBUG_ENTER_FUNCTION("Driverhandle=0x%p; Settings=0x%p", Driverhandle, Settings);

	input.DriverHandle = Driverhandle;
	input.MonitorAddDevice = Settings->MonitorAddDevice;
	input.MonitorNewDevices = Settings->MonitorNewDevices;
	input.MonitorStartIo = Settings->MonitorStartIo;
	input.MonitorUnload = Settings->MonitorUnload;
	ret = _SynchronousWriteIOCTL(IOCTL_IRPMNDRV_HOOK_DRIVER_SET_INFO, &input, sizeof(input));

	DEBUG_EXIT_FUNCTION("%u", ret);
	return ret;
}

DWORD DriverComHookedDriverGetInfo(HANDLE Driverhandle, PDRIVER_MONITOR_SETTINGS Settings, PBOOLEAN MonitoringEnabled)
{
	DWORD ret = ERROR_GEN_FAILURE;
	IOCTL_IRPMNDRV_HOOK_DRIVER_GET_INFO_INPUT input;
	IOCTL_IRPMNDRV_HOOK_DRIVER_GET_INFO_OUTPUT output;
	DEBUG_ENTER_FUNCTION("DriverHandle=0x%p; Settings=0x%p; MonitoringEnabled=0x%p", Driverhandle, Settings, MonitoringEnabled);

	input.DriverHandle = Driverhandle;
	ret = _SynchronousOtherIOCTL(IOCTL_IRPMNDRV_HOOK_DRIVER_GET_INFO, &input, sizeof(input), &output, sizeof(output));
	if (ret == ERROR_SUCCESS) {
		*Settings = output.Settings;
		*MonitoringEnabled = output.MonitoringEnabled;
	}

	DEBUG_EXIT_FUNCTION("%u, *MonitoringEnabled=%u", ret, *MonitoringEnabled);
	return ret;
}


DWORD DriverComHookedDriverActivate(HANDLE DriverHandle, BOOLEAN Activate)
{
	DWORD ret = ERROR_GEN_FAILURE;
	IOCTL_IRPMNDRV_HOOK_DRIVER_MONITORING_CHANGE_INPUT input;
	DEBUG_ENTER_FUNCTION("DriverHandle=0x%p; Activate=%u", DriverHandle, Activate);

	input.DriverHandle = DriverHandle;
	input.EnableMonitoring = Activate;
	ret = _SynchronousWriteIOCTL(IOCTL_IRPMNDRV_HOOK_DRIVER_MONITORING_CHANGE, &input, sizeof(input));

	DEBUG_EXIT_FUNCTION("%u", ret);
	return ret;
}

DWORD DriverComUnhookDriver(HANDLE HookHandle)
{
	DWORD ret = ERROR_GEN_FAILURE;
	IOCTL_IRPMNDRV_UNHOOK_DRIVER_INPUT input;
	DEBUG_ENTER_FUNCTION("HookHandle=0x%p", HookHandle);

	input.HookHandle = HookHandle;
	ret = _SynchronousWriteIOCTL(IOCTL_IRPMNDRV_UNHOOK_DRIVER, &input, sizeof(input));

	DEBUG_EXIT_FUNCTION("%u", ret);
	return ret;
}

DWORD DriverComConnect(HANDLE hSemaphore)
{
	DWORD ret = ERROR_GEN_FAILURE;
	IOCTL_IRPMNDRV_CONNECT_INPUT input;
	DEBUG_ENTER_FUNCTION("hSemaphore=0x%p", hSemaphore);

	input.SemaphoreHandle = hSemaphore;
	ret = _SynchronousWriteIOCTL(IOCTL_IRPMNDRV_CONNECT, &input, sizeof(input));

	DEBUG_EXIT_FUNCTION("%u", ret);
	return ret;
}

DWORD DriverComDisconnect(VOID)
{
	DWORD ret = ERROR_GEN_FAILURE;
	DEBUG_ENTER_FUNCTION_NO_ARGS();

	ret = _SynchronousNoIOIOCTL(IOCTL_IRPMNDRV_DISCONNECT);

	DEBUG_EXIT_FUNCTION("%u", ret);
	return ret;
}

DWORD DriverComGetRequest(PREQUEST_HEADER Request, DWORD Size)
{
	DWORD ret = ERROR_GEN_FAILURE;
	DEBUG_ENTER_FUNCTION("Request=0x%p; Size=%u", Request, Size);

	ret = _SynchronousReadIOCTL(IOCTL_IRPMNDRV_GET_RECORD, Request, Size);

	DEBUG_EXIT_FUNCTION("%u", ret);
	return ret;
}

DWORD DriverComHookDeviceByName(PWCHAR DeviceName, PHANDLE HookHandle)
{
	DWORD ret = ERROR_GEN_FAILURE;
	IOCTL_IRPMNDRV_HOOK_ADD_DEVICE_INPUT input;
	IOCTL_IRPMNDRV_HOOK_ADD_DEVICE_OUTPUT output;
	DEBUG_ENTER_FUNCTION("DeviceName=\"%S\"; HookHandle=0x%p", DeviceName, HookHandle);

	input.HookByName = TRUE;
	input.IRPSettings = NULL;
	input.FastIoSettings = NULL;
	input.DeviceAddress = NULL;
	input.DeviceName = _CopyString(DeviceName);
	if (input.DeviceName != NULL) {
		input.DeviceNameLength = (ULONG)wcslen(input.DeviceName)*sizeof(WCHAR);
		ret = _SynchronousOtherIOCTL(IOCTL_IRPMNDRV_HOOK_ADD_DEVICE, &input, sizeof(input), &output, sizeof(output));
		if (ret == ERROR_SUCCESS)
			*HookHandle = output.DeviceHandle;

		HeapFree(GetProcessHeap(), 0, input.DeviceName);
	} else ret = ERROR_NOT_ENOUGH_MEMORY;

	DEBUG_EXIT_FUNCTION("%u, *HookHandle=0x%p", ret, *HookHandle);
	return ret;
}

DWORD DriverComHookDeviceByAddress(PVOID DeviceObject, PHANDLE HookHandle)
{
	DWORD ret = ERROR_GEN_FAILURE;
	IOCTL_IRPMNDRV_HOOK_ADD_DEVICE_INPUT input;
	IOCTL_IRPMNDRV_HOOK_ADD_DEVICE_OUTPUT output;
	DEBUG_ENTER_FUNCTION("DeviceObject=0x%p; HookHandle=0x%p", DeviceObject, HookHandle);

	input.HookByName = FALSE;
	input.DeviceName = NULL;
	input.DeviceNameLength = 0;
	input.DeviceAddress = DeviceObject;
	input.FastIoSettings = NULL;
	input.IRPSettings = NULL;
	ret = _SynchronousOtherIOCTL(IOCTL_IRPMNDRV_HOOK_ADD_DEVICE, &input, sizeof(input), &output, sizeof(output));
	if (ret == ERROR_SUCCESS)
		*HookHandle = output.DeviceHandle;

	DEBUG_EXIT_FUNCTION("%u, *HookHandle=0x%p", ret, *HookHandle);
	return ret;
}

DWORD DriverComDeviceGetInfo(HANDLE DeviceHandle, PUCHAR IRPSettings, PUCHAR FastIoSettings, PBOOLEAN MonitoringEnabled)
{
	IOCTL_IRPMNDRV_HOOK_DEVICE_GET_INFO_INPUT input;
	IOCTL_IRPMNDRV_HOOK_DEVICE_GET_INFO_OUTPUT output;
	DWORD ret = ERROR_GEN_FAILURE;
	DEBUG_ENTER_FUNCTION("DeviceHandle=0x%p; IRPSettings=0x%p; FastIoSettings=0x%p; MonitoringEnabled=0x%p", DeviceHandle, IRPSettings, FastIoSettings, MonitoringEnabled);

	input.DeviceHandle = DeviceHandle;
	ret = _SynchronousOtherIOCTL(IOCTL_IRPMNDRV_HOOK_DEVICE_GET_INFO, &input, sizeof(input), &output, sizeof(output));
	if (ret == ERROR_SUCCESS) {
		*MonitoringEnabled = output.MonitoringEnabled;
		if (IRPSettings != NULL)
			memcpy(IRPSettings, output.IRPSettings, sizeof(output.IRPSettings));

		if (FastIoSettings != NULL)
			memcpy(FastIoSettings, output.FastIoSettings, sizeof(output.FastIoSettings));
	}

	DEBUG_EXIT_FUNCTION("%u, *MonitoringEnabled=%u", ret, *MonitoringEnabled);
	return ret;
}

DWORD DriverComDeviceSetInfo(HANDLE DeviceHandle, PUCHAR IRPSettings, PUCHAR FastIoSettings, BOOLEAN MonitoringEnabled)
{
	DWORD ret = ERROR_GEN_FAILURE;
	IOCTL_IRPMNDRV_HOOK_DEVICE_SET_INFO_INPUT input;
	DEBUG_ENTER_FUNCTION("DeviceHandle=0x%p; IRPSettings=0x%p; FastIoSettings=0x%p; MonitoringEnabled=%u", DeviceHandle, IRPSettings, FastIoSettings, MonitoringEnabled);

	input.DeviceHandle = DeviceHandle;
	input.IRPSettings = IRPSettings;
	input.FastIoSettings = FastIoSettings;
	input.MonitoringEnabled = MonitoringEnabled;
	ret = _SynchronousWriteIOCTL(IOCTL_IRPMNDRV_HOOK_DEVICE_SET_INFO, &input, sizeof(input));

	DEBUG_EXIT_FUNCTION("%u", ret);
	return ret;
}

DWORD DriverComUnhookDevice(HANDLE HookHandle)
{
	DWORD ret = ERROR_GEN_FAILURE;
	IOCTL_IRPMNDRV_HOOK_REMOVE_DEVICE_INPUT input;
	DEBUG_ENTER_FUNCTION("HookHandle=0x%p", HookHandle);

	input.DeviceHandle = HookHandle;
	ret = _SynchronousWriteIOCTL(IOCTL_IRPMNDRV_HOOK_REMOVE_DEVICE, &input, sizeof(input));

	DEBUG_EXIT_FUNCTION("%u", ret);
	return ret;
}

DWORD DriverComHookedObjectsEnumerate(PHOOKED_DRIVER_UMINFO *Info, PULONG Count)
{
	ULONG hoLen = 512;
	PHOOKED_OBJECTS_INFO hookedObjects = NULL;
	DWORD ret = ERROR_GEN_FAILURE;
	DEBUG_ENTER_FUNCTION("Info=0x%p; Count=0x%p", Info, Count);

	do {
		hoLen *= 2;
		hookedObjects = (PHOOKED_OBJECTS_INFO)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, hoLen);
		if (hookedObjects != NULL) {
			ret = _SynchronousReadIOCTL(IOCTL_IRPMONDRV_HOOK_GET_INFO, hookedObjects, hoLen);
			if (ret != ERROR_SUCCESS)
				HeapFree(GetProcessHeap(), 0, hookedObjects);
		} else ret = GetLastError();
	} while (ret == ERROR_INSUFFICIENT_BUFFER);

	if (ret == ERROR_SUCCESS) {
		PHOOKED_DRIVER_UMINFO tmpInfo = NULL;

		if (hookedObjects->NumberOfHookedDrivers > 0) {
			tmpInfo = (PHOOKED_DRIVER_UMINFO)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(HOOKED_DRIVER_UMINFO)*hookedObjects->NumberOfHookedDrivers);
			if (tmpInfo != NULL) {
				ULONG i = 0;
				PHOOKED_DRIVER_INFO driverEntry = (PHOOKED_DRIVER_INFO)(hookedObjects + 1);
				PHOOKED_DRIVER_UMINFO umDriverEntry = tmpInfo;

				ret = ERROR_SUCCESS;
				for (i = 0; i < hookedObjects->NumberOfHookedDrivers; ++i) {
					umDriverEntry->ObjectId = driverEntry->ObjectId;
					umDriverEntry->DriverObject = driverEntry->DriverObject;
					umDriverEntry->MonitoringEnabled = driverEntry->MonitoringEnabled;
					umDriverEntry->MonitorSettings = driverEntry->MonitorSettings;
					umDriverEntry->DriverNameLen = driverEntry->DriverNameLen - sizeof(WCHAR);
					umDriverEntry->DriverName = (PWCHAR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, driverEntry->DriverNameLen);
					if (umDriverEntry->DriverName != NULL) {
						memcpy(umDriverEntry->DriverName, &driverEntry->DriverName, driverEntry->DriverNameLen);
						umDriverEntry->NumberOfHookedDevices = driverEntry->NumberOfHookedDevices;
						umDriverEntry->HookedDevices = (umDriverEntry->NumberOfHookedDevices > 0) ?
							(PHOOKED_DEVICE_UMINFO)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, umDriverEntry->NumberOfHookedDevices*sizeof(HOOKED_DEVICE_UMINFO)) :
							NULL;

						if (umDriverEntry->NumberOfHookedDevices == 0 || umDriverEntry->HookedDevices != NULL) {
							ULONG j = 0;
							PHOOKED_DEVICE_INFO deviceEntry = (PHOOKED_DEVICE_INFO)((PUCHAR)driverEntry + driverEntry->EntrySize);
							PHOOKED_DEVICE_UMINFO umDeviceEntry = umDriverEntry->HookedDevices;

							for (j = 0; j < umDriverEntry->NumberOfHookedDevices; ++j) {
								umDeviceEntry->ObjectId = deviceEntry->ObjectId;
								umDeviceEntry->DeviceObject = deviceEntry->DeviceObject;
								memcpy(&umDeviceEntry->FastIoSettings, &deviceEntry->FastIoSettings, sizeof(umDeviceEntry->FastIoSettings));
								memcpy(&umDeviceEntry->IRPSettings, &deviceEntry->IRPSettings, sizeof(umDeviceEntry->IRPSettings));
								umDeviceEntry->MonitoringEnabled = deviceEntry->MonitoringEnabled;
								umDeviceEntry->DeviceNameLen = deviceEntry->DeviceNameLen - sizeof(WCHAR);
								umDeviceEntry->DeviceName = (PWCHAR)HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, deviceEntry->DeviceNameLen);
								if (umDeviceEntry->DeviceName != NULL)
									memcpy(umDeviceEntry->DeviceName, &deviceEntry->DeviceName, deviceEntry->DeviceNameLen);
								else ret = GetLastError();

								if (ret != ERROR_SUCCESS) {
									ULONG k = 0;

									--umDeviceEntry;
									for (k = 0; k < j; ++k) {
										HeapFree(GetProcessHeap(), 0, umDeviceEntry->DeviceName);
										--umDeviceEntry;
									}

									break;
								}

								++umDeviceEntry;
								deviceEntry = (PHOOKED_DEVICE_INFO)((PUCHAR)deviceEntry + deviceEntry->EntrySize);
							}

							driverEntry = (PHOOKED_DRIVER_INFO)deviceEntry;
							if (ret != ERROR_SUCCESS) {
								if (umDriverEntry->HookedDevices != NULL)
									HeapFree(GetProcessHeap(), 0, umDriverEntry->HookedDevices);
							}
						} else ret = GetLastError();

						if (ret != ERROR_SUCCESS)
							HeapFree(GetProcessHeap(), 0, umDriverEntry->DriverName);
					} else ret = GetLastError();

					if (ret != ERROR_SUCCESS) {
						ULONG j = 0;
						
						--umDriverEntry;
						for (j = 0; j < i; ++j) {
							if (umDriverEntry->NumberOfHookedDevices > 0) {
								ULONG k = 0;
								PHOOKED_DEVICE_UMINFO umDeviceEntry = umDriverEntry->HookedDevices;

								for (k = 0; k < umDriverEntry->NumberOfHookedDevices; ++k) {
									HeapFree(GetProcessHeap(), 0, umDeviceEntry->DeviceName);
									++umDeviceEntry;
								}
							
								HeapFree(GetProcessHeap(), 0, umDriverEntry->HookedDevices);
							}

							HeapFree(GetProcessHeap(), 0, umDriverEntry->DriverName);
							--umDriverEntry;
						}

						break;
					}

					++umDriverEntry;
				}

				if (ret == ERROR_SUCCESS) {
					*Info = tmpInfo;
					*Count = hookedObjects->NumberOfHookedDrivers;
				}

				if (ret != ERROR_SUCCESS)
					HeapFree(GetProcessHeap(), 0, tmpInfo);
			} else ret = GetLastError();
		} else {
			*Info = NULL;
			*Count = 0;
		}

		HeapFree(GetProcessHeap(), 0, hookedObjects);
	}

	DEBUG_EXIT_FUNCTION("%u, *Info=0x%p", ret, *Info);
	return ret;
}

VOID DriverComHookedObjectsFree(PHOOKED_DRIVER_UMINFO Info, ULONG Count)
{
	ULONG i = 0;
	PHOOKED_DRIVER_UMINFO driverInfo = Info;
	DEBUG_ENTER_FUNCTION("Info=0x%p; Count=%u", Info, Count);

	for (i = 0; i < Count; ++i) {
		if (driverInfo->NumberOfHookedDevices > 0) {
			ULONG j = 0;
			PHOOKED_DEVICE_UMINFO deviceInfo = driverInfo->HookedDevices;

			for (i = 0; i < driverInfo->NumberOfHookedDevices; ++i) {
				HeapFree(GetProcessHeap(), 0, deviceInfo->DeviceName);
				++deviceInfo;
			}

			HeapFree(GetProcessHeap(), 0, driverInfo->HookedDevices);
		}

		HeapFree(GetProcessHeap(), 0, driverInfo->DriverName);

		++driverInfo;
	}

	HeapFree(GetProcessHeap(), 0, Info);

	DEBUG_EXIT_FUNCTION_VOID();
	return;
}

DWORD DriverComDriverOpen(PVOID ID, PHANDLE Handle)
{
	DWORD ret = ERROR_GEN_FAILURE;
	DEBUG_ENTER_FUNCTION("ID=0x%p; Handle=0x%p", ID, Handle);

	ret = _ObjectOpen(ehtDriver, ID, Handle);

	DEBUG_EXIT_FUNCTION("%u, *Handle=0x%p", ret, *Handle);
	return ret;
}

DWORD DriverComDriverHandleClose(HANDLE Handle)
{
	DWORD ret = ERROR_GEN_FAILURE;
	DEBUG_ENTER_FUNCTION("Handle=0x%p", Handle);

	ret = _ObjectClose(ehtDriver, Handle);

	DEBUG_EXIT_FUNCTION("%u", ret);
	return ret;
}

DWORD DriverComDeviceOpen(PVOID ID, PHANDLE Handle)
{
	DWORD ret = ERROR_GEN_FAILURE;
	DEBUG_ENTER_FUNCTION("ID=0x%p; Handle=0x%p", ID, Handle);

	ret = _ObjectOpen(ehtDevice, ID, Handle);

	DEBUG_EXIT_FUNCTION("%u, *Handle=0x%p", ret, *Handle);
	return ret;
}

DWORD DriverComDeviceHandleClose(HANDLE Handle)
{
	DWORD ret = ERROR_GEN_FAILURE;
	DEBUG_ENTER_FUNCTION("Handle=0x%p", Handle);

	ret = _ObjectClose(ehtDevice, Handle);

	DEBUG_EXIT_FUNCTION("%u", ret);
	return ret;
}


/************************************************************************/
/*                   INITIALIZATION AND FINALIZATION                    */
/************************************************************************/

DWORD DriverComModuleInit(VOID)
{
	DWORD ret = ERROR_GEN_FAILURE;
	DEBUG_ENTER_FUNCTION_NO_ARGS();

	_deviceHandle = CreateFileW(IRPMNDRV_USER_DEVICE_NAME, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
	_initialized = (_deviceHandle != INVALID_HANDLE_VALUE);
	if (_initialized)
		ret = ERROR_SUCCESS;

	if (!_initialized)
		ret = GetLastError();

	DEBUG_EXIT_FUNCTION("%u", ret);
	return ret;
}

VOID DriverComModuleFinit(VOID)
{
	DEBUG_ENTER_FUNCTION_NO_ARGS();

	_connected = FALSE;
	_initialized = FALSE;
	CloseHandle(_deviceHandle);
	_deviceHandle = INVALID_HANDLE_VALUE;

	DEBUG_EXIT_FUNCTION_VOID();
	return;
}