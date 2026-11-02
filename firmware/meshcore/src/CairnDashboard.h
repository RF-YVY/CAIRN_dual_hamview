#pragma once

#include <lvgl.h>

enum class CairnAction { Home, Chats, Nodes, Radio, Position, Settings, Network, HamView, Files, Tools, Clock };

class CairnShell
{
  public:
    using Navigate = void (*)(CairnAction);
    void create(lv_obj_t *root, Navigate navigate);
    void select(CairnAction action);
    lv_obj_t *home = nullptr;
    lv_obj_t *content = nullptr;
    lv_obj_t *header = nullptr;

  private:
    Navigate navigate = nullptr;
    lv_obj_t *title = nullptr;
    lv_obj_t *tabs[6] = {};
    static void clicked(lv_event_t *event);
};

struct CairnDashboardState {
    char clock[8] = "--:--";
    char protocol[16] = "MESHCORE";
    char radioProfile[32] = {};
    char clockZone[12] = "UTC";
    char date[48] = "Time not synchronized";
    char messageFrom[32] = "Quiet";
    char messageText[80] = "Listening";
    char network[40] = "Wi-Fi off";
    char hamStatus[48] = "DX alerts";
    char scope[31] = "us-ms";
    char position[24] = "Not set";
    char positionDetail[32] = "GPS / saved";
    uint32_t messages = 0;
    uint32_t nodes = 0;
    uint32_t spots = 0;
    float frequency = 910.525f;
    float bandwidth = 62.5f;
    uint8_t sf = 7;
    int16_t noiseFloor = -127;
    int16_t signal[120] = {};
    uint16_t samples = 0;
    bool radioReady = false;
    bool wifiConnected = false;
};

class CairnDashboard
{
  public:
    using Navigate = void (*)(CairnAction);
    void create(lv_obj_t *parent, Navigate navigate);
    void update(const CairnDashboardState &state);
    void setNetworkDetail(const char *text) { lv_label_set_text(networkDetail, text); }

  private:
    Navigate navigate = nullptr;
    lv_obj_t *clock = nullptr;
    lv_obj_t *date = nullptr;
    lv_obj_t *protocol = nullptr;
    lv_obj_t *chart = nullptr;
    lv_chart_series_t *signalSeries = nullptr;
    lv_obj_t *chatValue = nullptr;
    lv_obj_t *chatDetail = nullptr;
    lv_obj_t *nodeValue = nullptr;
    lv_obj_t *positionValue = nullptr;
    lv_obj_t *positionDetail = nullptr;
    lv_obj_t *frequency = nullptr;
    lv_obj_t *radioDetail = nullptr;
    lv_obj_t *networkValue = nullptr;
    lv_obj_t *networkDetail = nullptr;
    lv_obj_t *hamValue = nullptr;
    static void clicked(lv_event_t *event);
    lv_obj_t *card(lv_obj_t *parent, const char *name, const char *symbol, uint32_t accent, CairnAction action);
};
