#ifndef ESIM_H
#define ESIM_H

#include <Arduino.h>
#include <SPI.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "euicc/euicc.h"
#include "euicc/interface.h"
#include "euicc/es10c.h"
#include "euicc/es10b.h"
#include "euicc/es10a.h"

#ifdef __cplusplus
}
#endif

#define ESIM_CS_PIN 5
#define ESIM_SPI_FREQUENCY 1000000

struct ESimProfile {
    char iccid[24];
    char nickname[64];
    int state;
    int profileClass;
    char serviceProviderName[64];
    char profileName[64];
};

bool esimInit(int csPin = ESIM_CS_PIN);
bool esimIsReady();
bool esimGetEID(char *eid, size_t bufferSize);
int esimGetProfiles(ESimProfile *profiles, int maxProfiles);
bool esimEnableProfile(const char *iccid);
bool esimDisableProfile(const char *iccid);
bool esimDeleteProfile(const char *iccid);
bool esimSetNickname(const char *iccid, const char *nickname);
bool esimGetNotificationCount(int *count);
bool esimRetrieveNotification(unsigned long seq);
bool esimRemoveNotification(unsigned long seq);
const char* esimGetLastError();

#endif