/*
 *  SDMUSBmuxListener.c
 *  SDMMobileDevice
 *
 *  Copyright (c) 2013, Sam Marshall
 *  All rights reserved.
 * 
 *  Redistribution and use in source and binary forms, with or without modification, are permitted provided that the following conditions are met:
 *  1. Redistributions of source code must retain the above copyright notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright notice, this list of conditions and the following disclaimer in the documentation and/or other materials provided with the distribution.
 *  3. All advertising materials mentioning features or use of this software must display the following acknowledgement:
 *  	This product includes software developed by the Sam Marshall.
 *  4. Neither the name of the Sam Marshall nor the names of its contributors may be used to endorse or promote products derived from this software without specific prior written permission.
 * 
 *  THIS SOFTWARE IS PROVIDED BY Sam Marshall ''AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL Sam Marshall BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 */

#ifndef _SDM_MD_USBMUXLISTENER_C_
#define _SDM_MD_USBMUXLISTENER_C_

#include "SDMMD_USBMuxListener.h"
#include "SDMMD_MCP.h"
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <sys/un.h>

typedef struct USBMuxResponseCode {
	uint32_t code;
	CFStringRef string;
} __attribute__ ((packed)) USBMuxResponseCode;

static uint32_t transactionId = 0x0; 

void SDMMD_USBMuxSend(uint32_t sock, struct USBMuxPacket *packet);
void SDMMD_USBMuxReceive(uint32_t sock, struct USBMuxPacket *packet);

void SDMMD_USBMuxResponseCallback(void *context, struct USBMuxPacket *packet);
void SDMMD_USBMuxAttachedCallback(void *context, struct USBMuxPacket *packet);
void SDMMD_USBMuxDetachedCallback(void *context, struct USBMuxPacket *packet);
void SDMMD_USBMuxLogsCallback(void *context, struct USBMuxPacket *packet);
void SDMMD_USBMuxDeviceListCallback(void *context, struct USBMuxPacket *packet);
void SDMMD_USBMuxListenerListCallback(void *context, struct USBMuxPacket *packet);

struct USBMuxResponseCode SDMMD_USBMuxParseReponseCode(CFDictionaryRef dict) {
	uint32_t code = 0x0;
	CFNumberRef resultCode = NULL;
	CFStringRef resultString = NULL;
	if (CFDictionaryContainsKey(dict, CFSTR("Number")))
		resultCode = CFDictionaryGetValue(dict, CFSTR("Number"));
	if (CFDictionaryContainsKey(dict, CFSTR("String")))
		resultString = CFDictionaryGetValue(dict, CFSTR("String"));
		
	if (resultCode) {
		CFNumberGetValue(resultCode, CFNumberGetType(resultCode), &code);
		switch (code) {
			case SDMMD_USBMuxResult_OK: {
				code = 0x0;
					resultString = CFSTR("OK");
				break;
			};
			case SDMMD_USBMuxResult_BadCommand: {
				code = 0x2d;
				if (!resultString)
					resultString = CFSTR("Bad Command");
				break;
			};
			case SDMMD_USBMuxResult_BadDevice: {
				code = 0x6;
				if (!resultString)
					resultString = CFSTR("Bad Device");
				break;
			};
			case SDMMD_USBMuxResult_ConnectionRefused: {
				code = 0x3d;
				if (!resultString)
					resultString = CFSTR("Connection Refused by Device");
				break;
			};
			case SDMMD_USBMuxResult_Unknown0: {
				code = 0xffffffff;
				break;
			};
			case SDMMD_USBMuxResult_Unknown1: {
				code = 0x16;
				break;
			};
			case SDMMD_USBMuxResult_BadVersion: {
				code = 0x49;
				if (!resultString)
					resultString = CFSTR("Bad Protocol Version");
				break;
			};
			case SDMMD_USBMuxResult_Unknown2: {
				code = 0x4b;
				break;
			};
			default: {
				break;
			};
		}
	}	
	return (struct USBMuxResponseCode){code, resultString};
}

void SDMMD_USBMuxResponseCallback(void *context, struct USBMuxPacket *packet) {
	if (packet->payload) {
		struct USBMuxResponseCode response = SDMMD_USBMuxParseReponseCode(packet->payload);
		dispatch_async(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0x0), ^{
			printf("usbmuxd returned%s: %d - %s.\n", (response.code ? " error" : ""), response.code, (response.string ? CFStringGetCStringPtr(response.string, CFStringGetFastestEncoding(response.string)) : "Unknown Error Description"));
		});
		dispatch_semaphore_signal(((SDMMD_USBMuxListenerRef)context)->semaphore);
	}
}

void SDMMD_USBMuxAttachedCallback(void *context, struct USBMuxPacket *packet) {
	SDMMD_AMDeviceRef newDevice = SDMMD_AMDeviceCreateFromProperties(packet->payload);
	if (newDevice && !CFArrayContainsValue(SDMMobileDevice->deviceList, CFRangeMake(0x0, CFArrayGetCount(SDMMobileDevice->deviceList)), newDevice)) {
		CFMutableArrayRef updateWithNew = CFArrayCreateMutableCopy(kCFAllocatorDefault, 0x0, SDMMobileDevice->deviceList);
		if (newDevice->ivars.connection_type == 0) {
			// give priority to usb over wifi
		}
		CFArrayAppendValue(updateWithNew, newDevice);
		CFNotificationCenterPostNotification(CFNotificationCenterGetLocalCenter(), CFSTR("SDMMD_USBMuxListenerDeviceAttachedNotification"), newDevice, NULL, true);
		CFRelease(SDMMobileDevice->deviceList);
		SDMMobileDevice->deviceList = CFArrayCreateCopy(kCFAllocatorDefault, updateWithNew);
		CFRelease(updateWithNew);
	}
	CFNotificationCenterPostNotification(CFNotificationCenterGetLocalCenter(), CFSTR("SDMMD_USBMuxListenerDeviceAttachedNotificationFinished"), newDevice, NULL, true);
}

void SDMMD_USBMuxDetachedCallback(void *context, struct USBMuxPacket *packet) {
	uint32_t detachedId;
	CFNumberRef deviceId = CFDictionaryGetValue(packet->payload, CFSTR("DeviceID"));
	CFNumberGetValue(deviceId, kCFNumberSInt64Type, &detachedId);
	CFMutableArrayRef updateWithRemove = CFArrayCreateMutableCopy(kCFAllocatorDefault, 0x0, SDMMobileDevice->deviceList);
	uint32_t removeCounter = 0x0;
	for (uint32_t i = 0x0; i < CFArrayGetCount(SDMMobileDevice->deviceList); i++) {
		SDMMD_AMDeviceRef device = (SDMMD_AMDeviceRef)CFArrayGetValueAtIndex(SDMMobileDevice->deviceList, i);
		// add something for then updating to use wifi if available.
		if (detachedId == SDMMD_AMDeviceGetConnectionID(device)) {
			CFArrayRemoveValueAtIndex(updateWithRemove, i-removeCounter);
			removeCounter++;
			CFNotificationCenterPostNotification(CFNotificationCenterGetLocalCenter(), CFSTR("SDMMD_USBMuxListenerDeviceDetachedNotification"), device, NULL, true);
		}
	}
	CFRelease(SDMMobileDevice->deviceList);
	SDMMobileDevice->deviceList = CFArrayCreateCopy(kCFAllocatorDefault, updateWithRemove);
	CFRelease(updateWithRemove);
	CFNotificationCenterPostNotification(CFNotificationCenterGetLocalCenter(), CFSTR("SDMMD_USBMuxListenerDeviceDetachedNotificationFinished"), NULL, NULL, true);
}

void SDMMD_USBMuxLogsCallback(void *context, struct USBMuxPacket *packet) {
	dispatch_semaphore_signal(((SDMMD_USBMuxListenerRef)context)->semaphore);
}

void SDMMD_USBMuxDeviceListCallback(void *context, struct USBMuxPacket *packet) {
	CFArrayRef devices = CFDictionaryGetValue(packet->payload, CFSTR("DeviceList"));
	for (uint32_t i = 0x0; i < CFArrayGetCount(devices); i++) {
		SDMMD_AMDeviceRef deviceFromList = SDMMD_AMDeviceCreateFromProperties(CFArrayGetValueAtIndex(devices, i));
		if (deviceFromList && !CFArrayContainsValue(SDMMobileDevice->deviceList, CFRangeMake(0x0, CFArrayGetCount(SDMMobileDevice->deviceList)), deviceFromList)) {
			struct USBMuxPacket *devicePacket = calloc(1, sizeof(struct USBMuxPacket));
			memcpy(devicePacket, packet, sizeof(struct USBMuxPacket));
			devicePacket->payload = CFArrayGetValueAtIndex(devices, i);
			((SDMMD_USBMuxListenerRef)context)->attachedCallback(context, devicePacket);
		}
	}
	dispatch_semaphore_signal(((SDMMD_USBMuxListenerRef)context)->semaphore);
}

void SDMMD_USBMuxListenerListCallback(void *context, struct USBMuxPacket *packet) {
	dispatch_semaphore_signal(((SDMMD_USBMuxListenerRef)context)->semaphore);
}

void SDMMD_USBMuxUnknownCallback(void *context, struct USBMuxPacket *packet) {
	printf("Unknown response from usbmuxd!\n");
	if (packet->payload)
		CFShow(packet->payload);
	dispatch_semaphore_signal(((SDMMD_USBMuxListenerRef)context)->semaphore);
}

SDMMD_USBMuxListenerRef SDMMD_USBMuxCreate() {
	SDMMD_USBMuxListenerRef listener = (SDMMD_USBMuxListenerRef)calloc(1, sizeof(struct USBMuxListenerClass));
	listener->socket = 0x0;
	listener->isActive = false;
	listener->socketQueue = dispatch_queue_create("com.samdmarshall.sdmmobiledevice.socketQueue", NULL);
	listener->responseCallback = SDMMD_USBMuxResponseCallback;
	listener->attachedCallback = SDMMD_USBMuxAttachedCallback;
	listener->detachedCallback = SDMMD_USBMuxDetachedCallback;
	listener->logsCallback = SDMMD_USBMuxLogsCallback;
	listener->deviceListCallback = SDMMD_USBMuxDeviceListCallback;
	listener->listenerListCallback = SDMMD_USBMuxListenerListCallback;
	listener->unknownCallback = SDMMD_USBMuxUnknownCallback;
	listener->responses = CFArrayCreateMutable(kCFAllocatorDefault, 0x0, NULL);
	return listener;
}

void SDMMD_USBMuxClose(SDMMD_USBMuxListenerRef listener) {
	listener->isActive = false;
	if (listener->responses)
		CFRelease(listener->responses);
	if (listener->socket)
		close(listener->socket);
	if (listener->socketQueue)
		dispatch_release(listener->socketQueue);
	if (listener->responseCallback)
		listener->responseCallback = NULL;
	if (listener->attachedCallback)
		listener->attachedCallback = NULL;
	if (listener->detachedCallback)
		listener->detachedCallback = NULL;
	if (listener->logsCallback)
		listener->logsCallback = NULL;
	if (listener->deviceListCallback)
		listener->deviceListCallback = NULL;
	if (listener->listenerListCallback)
		listener->listenerListCallback = NULL;
	if (listener->unknownCallback)
		listener->unknownCallback = NULL;
	CFNotificationCenterPostNotification(CFNotificationCenterGetLocalCenter(), CFSTR("SDMMD_USBMuxListenerStoppedListenerNotification"), NULL, NULL, true);
	free(listener);
}


/*
 debugging traffic:
 sudo mv /var/run/usbmuxd /var/run/usbmuxx
 sudo socat -t100 -x -v UNIX-LISTEN:/var/run/usbmuxd,mode=777,reuseaddr,fork UNIX-CONNECT:/var/run/usbmuxx
 */

uint32_t SDMMD_ConnectToUSBMux() {
	sdmmd_return_t result = 0x0;
	uint32_t sock = socket(AF_UNIX, SOCK_STREAM, 0x0);
	uint32_t mask = 0x00010400;
	if (setsockopt(sock, 0xffff, 0x1001, &mask, 0x4)) {
		result = 0x1;
		printf("SDMMD_USBMuxConnectByPort: setsockopt SO_SNDBUF failed: %d - %s\\n", errno, strerror(errno));
	}
	mask = 0x00010400;
	if (setsockopt(sock, 0xffff, 0x1002, &mask, 0x4)) {
		result = 0x2;
		printf("SDMMD_USBMuxConnectByPort: setsockopt SO_RCVBUF failed: %d - %s\\n", errno, strerror(errno));
	}
	mask = 0x1;
	if (setsockopt(sock, 0xffff, 0x1022, &mask, 0x4)) {
		result = 0x3;
		printf("SDMMD_USBMuxConnectByPort: setsockopt SO_NOSIGPIPE failed: %d - %s\\n", errno, strerror(errno));
	}
	if (!result) {
		char *mux = "/var/run/usbmuxd";
		struct sockaddr_un address;
		address.sun_family = AF_UNIX;
		strncpy(address.sun_path, mux, 0x68);
        address.sun_len = SUN_LEN(&address);

		result = connect(sock, &address, sizeof(struct sockaddr_un));
		ioctl(sock, 0x8004667e/*, nope */); // _USBMuxSetSocketBlockingMode
	}
	return sock;
}

sdmmd_return_t SDMMD_USBMuxConnectByPort(SDMMD_AMDeviceRef device, uint32_t port, uint32_t *socketConn) {
	sdmmd_return_t result = 0x0;
	*socketConn = SDMMD_ConnectToUSBMux();
	if (*socketConn) {
		CFMutableDictionaryRef dict = CFDictionaryCreateMutable(kCFAllocatorDefault, 0x0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
		CFNumberRef deviceNum = CFNumberCreate(kCFAllocatorDefault, 0x3, &device->ivars.device_id);
		CFDictionarySetValue(dict, CFSTR("DeviceID"), deviceNum);
		CFRelease(deviceNum);
		struct USBMuxPacket *connect = SDMMD_USBMuxCreatePacketType(kSDMMD_USBMuxPacketConnectType, dict);
		if (port != 0x7ef2) {
			uint16_t newPort = htons(port);
			CFNumberRef portNumber = CFNumberCreate(kCFAllocatorDefault, 0x2, &newPort);
			CFDictionarySetValue((CFMutableDictionaryRef)connect->payload, CFSTR("PortNumber"), portNumber);
			CFRelease(portNumber);
		}
		SDMMD_USBMuxSend(*socketConn, connect);
		SDMMD_USBMuxReceive(*socketConn, connect);
		CFRelease(dict);
	} else {
		result = kAMDMuxConnectError;
	}
	return result;
}

void SDMMD_USBMuxStartListener(SDMMD_USBMuxListenerRef *listener) {
	dispatch_sync(dispatch_get_global_queue(DISPATCH_QUEUE_PRIORITY_DEFAULT, 0x0), ^{
		(*listener)->socket = SDMMD_ConnectToUSBMux();
		(*listener)->socketSource = dispatch_source_create(DISPATCH_SOURCE_TYPE_READ, (*listener)->socket, 0x0, (*listener)->socketQueue);
		dispatch_source_set_event_handler((*listener)->socketSource, ^{
            printf("socketSourceEventHandler: fired\n");
			struct USBMuxPacket *packet = (struct USBMuxPacket *)calloc(0x1, sizeof(struct USBMuxPacket));
			SDMMD_USBMuxReceive((*listener)->socket, packet);
			if (CFPropertyListIsValid(packet->payload, kCFPropertyListXMLFormat_v1_0)) {
				if (CFDictionaryContainsKey(packet->payload, CFSTR("MessageType"))) {
					CFStringRef type = CFDictionaryGetValue(packet->payload, CFSTR("MessageType"));
					if (CFStringCompare(type, SDMMD_USBMuxPacketMessage[kSDMMD_USBMuxPacketResultType], 0x0) == 0x0) {
						(*listener)->responseCallback((*listener), packet);
						CFArrayAppendValue((*listener)->responses, packet);
					} else if (CFStringCompare(type, SDMMD_USBMuxPacketMessage[kSDMMD_USBMuxPacketAttachType], 0x0) == 0x0) {
						(*listener)->attachedCallback((*listener), packet);
					} else if (CFStringCompare(type, SDMMD_USBMuxPacketMessage[kSDMMD_USBMuxPacketDetachType], 0x0) == 0x0) {
						(*listener)->detachedCallback((*listener), packet);
					}
				} else {
					if (CFDictionaryContainsKey(packet->payload, CFSTR("Logs"))) {
						(*listener)->logsCallback((*listener), packet);
					} else if (CFDictionaryContainsKey(packet->payload, CFSTR("DeviceList"))) {
						(*listener)->deviceListCallback((*listener), packet);
					} else if (CFDictionaryContainsKey(packet->payload, CFSTR("ListenerList"))) {
						(*listener)->listenerListCallback((*listener), packet);
					} else {
						(*listener)->unknownCallback((*listener), packet);
					}
					CFArrayAppendValue((*listener)->responses, packet);
				}
			} else {
                printf("socketSourceEventHandler: failed to decodeCFPropertyList from packet payload\n");
            }
		});
        dispatch_source_set_cancel_handler((*listener)->socketSource, ^{
            printf("socketSourceEventCancelHandler: source canceled\n");
        });
		dispatch_resume((*listener)->socketSource);
				
		while (!(*listener)->isActive) {
			struct USBMuxPacket *startListen = SDMMD_USBMuxCreatePacketType(kSDMMD_USBMuxPacketListenType, NULL);
			SDMMD_USBMuxListenerSend(*listener, startListen);
			if (startListen->payload) {
				struct USBMuxResponseCode response = SDMMD_USBMuxParseReponseCode(startListen->payload);
				if (response.code == 0x0){
					(*listener)->isActive = true;
                } else {
                    printf("SDMMD_USBMuxStartListener: non zero response code. trying again. code:%i string:%s\n", response.code, response.string ? CFStringGetCStringPtr(response.string, kCFStringEncodingUTF8):"");
                }
			} else {
                printf("SDMMD_USBMuxStartListener: no response payload. trying again.\n");
            }
			USBMuxPacketRelease(startListen);
		}
	});
}

void SDMMD_USBMuxListenerSend(SDMMD_USBMuxListenerRef listener, struct USBMuxPacket *packet) {
	listener->semaphore = dispatch_semaphore_create(0x0);
	SDMMD_USBMuxSend(listener->socket, packet);
	dispatch_semaphore_wait(listener->semaphore, packet->timeout);
	
	CFMutableArrayRef updateWithRemove = CFArrayCreateMutableCopy(kCFAllocatorDefault, 0x0, listener->responses);
	struct USBMuxPacket *responsePacket = (struct USBMuxPacket *)calloc(0x1, sizeof(struct USBMuxPacket));
	uint32_t removeCounter = 0x0;
	for (uint32_t i = 0x0; i < CFArrayGetCount(listener->responses); i++) {
		struct USBMuxPacket *response = (struct USBMuxPacket *)CFArrayGetValueAtIndex(listener->responses, i);
		if (packet->body.tag == response->body.tag) {
			responsePacket = response;
			CFArrayRemoveValueAtIndex(updateWithRemove, i-removeCounter);
			removeCounter++;
		}
	}
	CFRelease(listener->responses);
	listener->responses = CFArrayCreateMutableCopy(kCFAllocatorDefault, 0x0, updateWithRemove);
	CFRelease(updateWithRemove);
	*packet = *responsePacket;
	dispatch_release(listener->semaphore);
}

void SDMMD_USBMuxSend(uint32_t sock, struct USBMuxPacket *packet) {	
	CFDataRef xmlData = CFPropertyListCreateXMLData(kCFAllocatorDefault, packet->payload);
	char *buffer = (char *)CFDataGetBytePtr(xmlData);
	uint32_t result = send(sock, &packet->body, sizeof(struct USBMuxPacketBody), 0x0);
	if (result == sizeof(struct USBMuxPacketBody)) {
		if (packet->body.length > result) {
			uint32_t payloadSize = packet->body.length - result;
			uint32_t remainder = payloadSize;
			while (remainder) {
				result = send(sock, &buffer[payloadSize-remainder], sizeof(char), 0x0);
				if (result != sizeof(char))
					break;
				remainder -= result;
			}
		}
	}
	CFRelease(xmlData);
}

void SDMMD_USBMuxListenerReceive(SDMMD_USBMuxListenerRef listener, struct USBMuxPacket *packet) {
	SDMMD_USBMuxReceive(listener->socket, packet);
}

void SDMMD_USBMuxReceive(uint32_t sock, struct USBMuxPacket *packet) {
	uint32_t result = recv(sock, &packet->body, sizeof(struct USBMuxPacketBody), 0x0);
	if (result == sizeof(struct USBMuxPacketBody)) {
		uint32_t payloadSize = packet->body.length - result;
		if (payloadSize) {
			char *buffer = calloc(0x1, payloadSize);
			uint32_t remainder = payloadSize;
			while (remainder) {
				result = recv(sock, &buffer[payloadSize-remainder], sizeof(char), 0x0);
				if (result != sizeof(char))
					break;
				remainder -= result;
			}
			CFDataRef xmlData = CFDataCreate(kCFAllocatorDefault, (UInt8 *)buffer, payloadSize);
			packet->payload = CFPropertyListCreateFromXMLData(kCFAllocatorDefault, xmlData, kCFPropertyListImmutable, NULL);
			free(buffer);
			CFRelease(xmlData);
		}
	}
}

struct USBMuxPacket * SDMMD_USBMuxCreatePacketType(SDMMD_USBMuxPacketMessageType type, CFDictionaryRef dict) {
	struct USBMuxPacket *packet = (struct USBMuxPacket *)calloc(1, sizeof(struct USBMuxPacket));
	if (type == kSDMMD_USBMuxPacketListenType || type == kSDMMD_USBMuxPacketConnectType) {
		packet->timeout = dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC*0x1e);
	} else {
		packet->timeout = dispatch_time(DISPATCH_TIME_NOW, NSEC_PER_SEC*0x5);
	}
	packet->body = (struct USBMuxPacketBody){0x10, 0x1, 0x8, transactionId};
	transactionId++;
	packet->payload = CFDictionaryCreateMutable(kCFAllocatorDefault, 0x0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
	CFDictionarySetValue((CFMutableDictionaryRef)packet->payload, CFSTR("BundleID"), CFSTR("com.samdmarshall.sdmmobiledevice"));
	CFDictionarySetValue((CFMutableDictionaryRef)packet->payload, CFSTR("ClientVersionString"), CFSTR("usbmuxd-323"));
	CFDictionarySetValue((CFMutableDictionaryRef)packet->payload, CFSTR("ProgName"), CFSTR("SDMMobileDevice"));
    uint32_t version = 3;
    CFNumberRef versionNumber = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &version);
	CFDictionarySetValue((CFMutableDictionaryRef)packet->payload, CFSTR("kLibUSBMuxVersion"), versionNumber);
    if (versionNumber) CFRelease(versionNumber);
    
	if (dict) {
		CFIndex count = CFDictionaryGetCount(dict);
		void *keys[count];
		void *values[count];
		CFDictionaryGetKeysAndValues(dict, (const void **)keys, (const void **)values);
		for (uint32_t i = 0x0; i < count; i++) {
			CFDictionarySetValue((CFMutableDictionaryRef)packet->payload, keys[i], values[i]);
		}
	}
	CFDictionarySetValue((CFMutableDictionaryRef)packet->payload, CFSTR("MessageType"), SDMMD_USBMuxPacketMessage[type]);
	if (type == kSDMMD_USBMuxPacketConnectType) {
		uint16_t port = 0x7ef2; //htons(0x7ef2);
		CFNumberRef portNumber = CFNumberCreate(kCFAllocatorDefault, kCFNumberSInt16Type, &port);
		CFDictionarySetValue((CFMutableDictionaryRef)packet->payload, CFSTR("PortNumber"), portNumber);
		CFRelease(portNumber);
	}
	if (type == kSDMMD_USBMuxPacketListenType) {
		uint32_t connection = 0x0;
		CFNumberRef connectionType = CFNumberCreate(kCFAllocatorDefault, kCFNumberIntType, &connection);
		CFDictionarySetValue((CFMutableDictionaryRef)packet->payload, CFSTR("ConnType"), connectionType);
		CFRelease(connectionType);
	}

	CFDataRef xmlData = CFPropertyListCreateXMLData(kCFAllocatorDefault, packet->payload);
	packet->body.length = 0x10 + CFDataGetLength(xmlData);
	CFRelease(xmlData);
	return packet;
}

void USBMuxPacketRelease(struct USBMuxPacket *packet) {
	if (CFPropertyListIsValid(packet->payload, kCFPropertyListXMLFormat_v1_0))
		CFRelease(packet->payload);
	free(packet);
}

#endif