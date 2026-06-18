#include "esim.h"
#include "web_handlers.h"

static struct euicc_ctx s_euiccCtx;
static struct euicc_apdu_interface s_apduInterface;
static struct euicc_http_interface s_httpInterface;
static bool s_initialized = false;
static bool s_ready = false;
static char s_lastError[128] = {0};

static SPISettings *s_spiSettings = nullptr;
static int s_csPin = -1;

static void setError(const char *msg) {
    strncpy(s_lastError, msg, sizeof(s_lastError) - 1);
    s_lastError[sizeof(s_lastError) - 1] = '\0';
}

static int apduConnect(struct euicc_ctx *ctx) {
    SPI.beginTransaction(*s_spiSettings);
    digitalWrite(s_csPin, LOW);
    delay(10);
    return 0;
}

static void apduDisconnect(struct euicc_ctx *ctx) {
    digitalWrite(s_csPin, HIGH);
    SPI.endTransaction();
}

static int apduLogicChannelOpen(struct euicc_ctx *ctx, const uint8_t *aid, uint8_t aidLen) {
    uint8_t cmd[] = {0x00, 0xA4, 0x04, 0x00, aidLen};
    uint8_t response[256] = {0};
    
    digitalWrite(s_csPin, LOW);
    
    for (uint8_t i = 0; i < sizeof(cmd); i++) {
        SPI.transfer(cmd[i]);
    }
    
    for (uint8_t i = 0; i < aidLen; i++) {
        response[i] = SPI.transfer(aid[i]);
    }
    
    for (int i = 0; i < 2; i++) {
        response[aidLen + i] = SPI.transfer(0x00);
    }
    
    digitalWrite(s_csPin, HIGH);
    
    if (response[aidLen] != 0x90 || response[aidLen + 1] != 0x00) {
        return -1;
    }
    
    return 0;
}

static void apduLogicChannelClose(struct euicc_ctx *ctx, uint8_t channel) {
}

static int apduTransmit(struct euicc_ctx *ctx, uint8_t **rx, uint32_t *rxLen, const uint8_t *tx, uint32_t txLen) {
    uint8_t *response = (uint8_t *)malloc(256);
    if (!response) {
        return -1;
    }
    
    memset(response, 0, 256);
    
    digitalWrite(s_csPin, LOW);
    
    for (uint32_t i = 0; i < txLen; i++) {
        response[i] = SPI.transfer(tx[i]);
    }
    
    for (int i = 0; i < 2; i++) {
        response[txLen + i] = SPI.transfer(0x00);
    }
    
    digitalWrite(s_csPin, HIGH);
    
    *rx = response;
    *rxLen = txLen + 2;
    
    return 0;
}

static int httpTransmit(struct euicc_ctx *ctx, const char *url, uint32_t *rcode, uint8_t **rx, uint32_t *rxLen, const uint8_t *tx, uint32_t txLen, const char **headers) {
    if (!WiFi.isConnected()) {
        return -1;
    }
    
    WiFiClient client;
    HTTPClient http;
    
    if (!http.begin(client, url)) {
        return -1;
    }
    
    if (headers) {
        for (int i = 0; headers[i] != NULL; i += 2) {
            if (headers[i + 1]) {
                http.addHeader(headers[i], headers[i + 1]);
            }
        }
    }
    
    int httpResponseCode;
    if (tx && txLen > 0) {
        httpResponseCode = http.POST((const char *)tx, txLen);
    } else {
        httpResponseCode = http.GET();
    }
    
    if (httpResponseCode > 0) {
        *rcode = httpResponseCode;
        String payload = http.getString();
        *rxLen = payload.length();
        *rx = (uint8_t *)malloc(*rxLen + 1);
        if (*rx) {
            memcpy(*rx, payload.c_str(), *rxLen);
            (*rx)[*rxLen] = '\0';
        }
        http.end();
        return *rx ? 0 : -1;
    }
    
    http.end();
    return -1;
}

bool esimInit(int csPin) {
    if (s_initialized) {
        return true;
    }
    
    s_csPin = csPin;
    static SPISettings spiSettings(ESIM_SPI_FREQUENCY, MSBFIRST, SPI_MODE0);
    s_spiSettings = &spiSettings;
    
    SPI.begin();
    pinMode(s_csPin, OUTPUT);
    digitalWrite(s_csPin, HIGH);
    
    s_apduInterface.connect = apduConnect;
    s_apduInterface.disconnect = apduDisconnect;
    s_apduInterface.logic_channel_open = apduLogicChannelOpen;
    s_apduInterface.logic_channel_close = apduLogicChannelClose;
    s_apduInterface.transmit = apduTransmit;
    s_apduInterface.userdata = nullptr;
    
    s_httpInterface.transmit = httpTransmit;
    s_httpInterface.userdata = nullptr;
    
    memset(&s_euiccCtx, 0, sizeof(s_euiccCtx));
    s_euiccCtx.apdu.interface = &s_apduInterface;
    s_euiccCtx.http.interface = &s_httpInterface;
    
    s_initialized = true;
    
    if (euicc_init(&s_euiccCtx) == 0) {
        s_ready = true;
        euicc_fini(&s_euiccCtx);
        logCaptureLn("eSIM初始化成功");
        return true;
    } else {
        setError("eSIM初始化失败");
        logCaptureLn("eSIM初始化失败");
        return false;
    }
}

bool esimIsReady() {
    return s_ready;
}

bool esimGetEID(char *eid, size_t bufferSize) {
    if (!s_initialized || !s_ready) {
        setError("eSIM未就绪");
        return false;
    }
    
    if (euicc_init(&s_euiccCtx) != 0) {
        setError("初始化euicc失败");
        return false;
    }
    
    char *eidValue = NULL;
    int result = es10c_get_eid(&s_euiccCtx, &eidValue);
    
    if (result == 0 && eidValue) {
        strncpy(eid, eidValue, bufferSize - 1);
        eid[bufferSize - 1] = '\0';
        free(eidValue);
        euicc_fini(&s_euiccCtx);
        return true;
    } else {
        setError("获取EID失败");
        euicc_fini(&s_euiccCtx);
        return false;
    }
}

int esimGetProfiles(ESimProfile *profiles, int maxProfiles) {
    if (!s_initialized || !s_ready || !profiles || maxProfiles <= 0) {
        return 0;
    }
    
    if (euicc_init(&s_euiccCtx) != 0) {
        return 0;
    }
    
    struct es10c_profile_info_list *profileList = NULL;
    int result = es10c_get_profiles_info(&s_euiccCtx, &profileList);
    
    if (result != 0) {
        euicc_fini(&s_euiccCtx);
        return 0;
    }
    
    int count = 0;
    struct es10c_profile_info_list *current = profileList;
    
    while (current && count < maxProfiles) {
        strncpy(profiles[count].iccid, current->iccid, sizeof(profiles[count].iccid) - 1);
        profiles[count].iccid[sizeof(profiles[count].iccid) - 1] = '\0';
        
        if (current->profileNickname) {
            strncpy(profiles[count].nickname, current->profileNickname, sizeof(profiles[count].nickname) - 1);
            profiles[count].nickname[sizeof(profiles[count].nickname) - 1] = '\0';
        } else {
            profiles[count].nickname[0] = '\0';
        }
        
        profiles[count].state = current->profileState;
        profiles[count].profileClass = current->profileClass;
        
        if (current->serviceProviderName) {
            strncpy(profiles[count].serviceProviderName, current->serviceProviderName, sizeof(profiles[count].serviceProviderName) - 1);
            profiles[count].serviceProviderName[sizeof(profiles[count].serviceProviderName) - 1] = '\0';
        } else {
            profiles[count].serviceProviderName[0] = '\0';
        }
        
        if (current->profileName) {
            strncpy(profiles[count].profileName, current->profileName, sizeof(profiles[count].profileName) - 1);
            profiles[count].profileName[sizeof(profiles[count].profileName) - 1] = '\0';
        } else {
            profiles[count].profileName[0] = '\0';
        }
        
        count++;
        current = current->next;
    }
    
    es10c_profile_info_list_free_all(profileList);
    euicc_fini(&s_euiccCtx);
    
    return count;
}

bool esimEnableProfile(const char *iccid) {
    if (!s_initialized || !s_ready || !iccid) {
        setError("参数无效");
        return false;
    }
    
    if (euicc_init(&s_euiccCtx) != 0) {
        setError("初始化euicc失败");
        return false;
    }
    
    int result = es10c_enable_profile(&s_euiccCtx, iccid, 1);
    euicc_fini(&s_euiccCtx);
    
    if (result != 0) {
        setError("启用配置文件失败");
        return false;
    }
    
    logCapture(String("eSIM配置文件已启用: ") + iccid);
    return true;
}

bool esimDisableProfile(const char *iccid) {
    if (!s_initialized || !s_ready || !iccid) {
        setError("参数无效");
        return false;
    }
    
    if (euicc_init(&s_euiccCtx) != 0) {
        setError("初始化euicc失败");
        return false;
    }
    
    int result = es10c_disable_profile(&s_euiccCtx, iccid, 1);
    euicc_fini(&s_euiccCtx);
    
    if (result != 0) {
        setError("禁用配置文件失败");
        return false;
    }
    
    logCapture(String("eSIM配置文件已禁用: ") + iccid);
    return true;
}

bool esimDeleteProfile(const char *iccid) {
    if (!s_initialized || !s_ready || !iccid) {
        setError("参数无效");
        return false;
    }
    
    if (euicc_init(&s_euiccCtx) != 0) {
        setError("初始化euicc失败");
        return false;
    }
    
    int result = es10c_delete_profile(&s_euiccCtx, iccid);
    euicc_fini(&s_euiccCtx);
    
    if (result != 0) {
        setError("删除配置文件失败");
        return false;
    }
    
    logCapture(String("eSIM配置文件已删除: ") + iccid);
    return true;
}

bool esimSetNickname(const char *iccid, const char *nickname) {
    if (!s_initialized || !s_ready || !iccid || !nickname) {
        setError("参数无效");
        return false;
    }
    
    if (euicc_init(&s_euiccCtx) != 0) {
        setError("初始化euicc失败");
        return false;
    }
    
    int result = es10c_set_nickname(&s_euiccCtx, iccid, nickname);
    euicc_fini(&s_euiccCtx);
    
    if (result != 0) {
        setError("设置昵称失败");
        return false;
    }
    
    return true;
}

bool esimGetNotificationCount(int *count) {
    if (!s_initialized || !s_ready || !count) {
        return false;
    }
    
    if (euicc_init(&s_euiccCtx) != 0) {
        return false;
    }
    
    struct es10b_notification_metadata_list *notifList = NULL;
    int result = es10b_list_notification(&s_euiccCtx, &notifList);
    
    if (result != 0) {
        euicc_fini(&s_euiccCtx);
        return false;
    }
    
    *count = 0;
    struct es10b_notification_metadata_list *current = notifList;
    while (current) {
        (*count)++;
        current = current->next;
    }
    
    es10b_notification_metadata_list_free_all(notifList);
    euicc_fini(&s_euiccCtx);
    
    return true;
}

bool esimRetrieveNotification(unsigned long seq) {
    if (!s_initialized || !s_ready) {
        return false;
    }
    
    if (euicc_init(&s_euiccCtx) != 0) {
        return false;
    }
    
    struct es10b_pending_notification notif;
    int result = es10b_retrieve_notifications_list(&s_euiccCtx, &notif, seq);
    
    if (result == 0) {
        es10b_pending_notification_free(&notif);
        euicc_fini(&s_euiccCtx);
        return true;
    }
    
    euicc_fini(&s_euiccCtx);
    return false;
}

bool esimRemoveNotification(unsigned long seq) {
    if (!s_initialized || !s_ready) {
        return false;
    }
    
    if (euicc_init(&s_euiccCtx) != 0) {
        return false;
    }
    
    int result = es10b_remove_notification_from_list(&s_euiccCtx, seq);
    euicc_fini(&s_euiccCtx);
    
    return result == 0;
}

const char* esimGetLastError() {
    return s_lastError;
}