#include "CairnDashboard.h"
#include <cstdio>

LV_FONT_DECLARE(cairn_clock_72);

namespace
{
constexpr uint32_t BLACK = 0x030106;
constexpr uint32_t PANEL = 0x10071D;
constexpr uint32_t TEXT = 0xF4F0FF;
constexpr uint32_t MUTED = 0xB6A9D2;
constexpr uint32_t CYAN = 0x00D7E6;
constexpr uint32_t GREEN = 0x20FF60;
constexpr uint32_t BLUE = 0x3294FF;
constexpr uint32_t PURPLE = 0xC15CFA;
constexpr uint32_t YELLOW = 0xFFE500;

void box(lv_obj_t *obj, int x, int y, int w, int h, uint32_t color, int radius = 0)
{
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_size(obj, w, h);
    lv_obj_set_style_bg_color(obj, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(obj, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(obj, radius, 0);
    lv_obj_remove_flag(obj, LV_OBJ_FLAG_SCROLLABLE);
}

lv_obj_t *text(lv_obj_t *parent, const char *value, int x, int y, int width, uint32_t color,
               const lv_font_t *font = &lv_font_montserrat_16)
{
    auto *obj = lv_label_create(parent);
    lv_obj_remove_style_all(obj);
    lv_obj_set_pos(obj, x, y);
    lv_obj_set_width(obj, width);
    lv_obj_set_height(obj, lv_font_get_line_height(font));
    lv_obj_set_style_text_font(obj, font, 0);
    lv_obj_set_style_text_color(obj, lv_color_hex(color), 0);
    lv_label_set_long_mode(obj, LV_LABEL_LONG_DOT);
    lv_label_set_text(obj, value);
    return obj;
}

void stroke(lv_obj_t *parent, const lv_point_precise_t *points, uint32_t count, uint32_t color)
{
    auto *line = lv_line_create(parent);
    lv_obj_remove_style_all(line);
    lv_line_set_points(line, points, count);
    lv_obj_set_style_line_color(line, lv_color_hex(color), 0);
    lv_obj_set_style_line_width(line, 3, 0);
    lv_obj_set_style_line_rounded(line, true, 0);
    lv_obj_remove_flag(line, LV_OBJ_FLAG_CLICKABLE);
}

void navIcon(lv_obj_t *parent, unsigned icon, uint32_t color)
{
    static const lv_point_precise_t roof[] = {{12, 28}, {29, 13}, {46, 28}};
    static const lv_point_precise_t house[] = {{17, 27}, {17, 44}, {26, 44}, {26, 34}, {33, 34}, {33, 44}, {41, 44}, {41, 27}};
    static const lv_point_precise_t tail[] = {{17, 36}, {13, 45}, {27, 36}};
    static const lv_point_precise_t network[] = {{15, 16}, {41, 20}, {28, 42}, {15, 16}};
    static const lv_point_precise_t wave[] = {{9, 31}, {18, 31}, {23, 13}, {29, 44}, {35, 23}, {40, 31}, {49, 31}};
    static const lv_point_precise_t map[] = {{11, 17}, {23, 12}, {37, 18}, {49, 12}, {49, 42},
                                             {37, 47}, {23, 41}, {11, 47}, {11, 17}};
    static const lv_point_precise_t fold1[] = {{23, 12}, {23, 41}};
    static const lv_point_precise_t fold2[] = {{37, 18}, {37, 47}};
    if (icon == 0) {
        stroke(parent, roof, 3, color);
        stroke(parent, house, 8, color);
    } else if (icon == 1) {
        auto *bubble = lv_obj_create(parent);
        box(bubble, 12, 14, 34, 23, BLACK, 5);
        lv_obj_set_style_bg_opa(bubble, LV_OPA_TRANSP, 0);
        lv_obj_set_style_border_width(bubble, 3, 0);
        lv_obj_set_style_border_color(bubble, lv_color_hex(color), 0);
        lv_obj_remove_flag(bubble, LV_OBJ_FLAG_CLICKABLE);
        stroke(parent, tail, 3, color);
    } else if (icon == 2) {
        stroke(parent, network, 4, color);
        const int centers[][2] = {{15, 16}, {41, 20}, {28, 42}};
        for (const auto &center : centers) {
            auto *dot = lv_obj_create(parent);
            box(dot, center[0] - 6, center[1] - 6, 12, 12, color, LV_RADIUS_CIRCLE);
            lv_obj_remove_flag(dot, LV_OBJ_FLAG_CLICKABLE);
        }
    } else if (icon == 3) {
        stroke(parent, wave, 7, color);
    } else if (icon == 4) {
        stroke(parent, map, 9, color);
        stroke(parent, fold1, 2, color);
        stroke(parent, fold2, 2, color);
    } else {
        auto *glyph = text(parent, LV_SYMBOL_SETTINGS, 0, 12, 58, color, &lv_font_montserrat_28);
        lv_obj_set_style_text_align(glyph, LV_TEXT_ALIGN_CENTER, 0);
    }
}

void tint(lv_obj_t *parent, uint32_t color)
{
    for (uint32_t i = 0; i < lv_obj_get_child_count(parent); ++i) {
        auto *child = lv_obj_get_child(parent, i);
        lv_obj_set_style_line_color(child, lv_color_hex(color), 0);
        lv_obj_set_style_border_color(child, lv_color_hex(color), 0);
        lv_obj_set_style_text_color(child, lv_color_hex(color), 0);
        lv_obj_set_style_bg_color(child, lv_color_hex(color), 0);
    }
}
} // namespace

void CairnShell::clicked(lv_event_t *event)
{
    auto *self = static_cast<CairnShell *>(lv_event_get_user_data(event));
    const auto action =
        static_cast<CairnAction>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event))));
    if (self->navigate)
        self->navigate(action);
}

void CairnShell::create(lv_obj_t *root, Navigate callback)
{
    navigate = callback;
    box(root, 0, 0, 480, 480, BLACK);
    header = lv_obj_create(root);
    box(header, 0, 0, 480, 44, BLACK);
    title = text(header, "SENSECAP", 39, 10, 275, GREEN, &lv_font_montserrat_20);
    text(header, LV_SYMBOL_BLUETOOTH, 363, 11, 24, MUTED, &lv_font_montserrat_20);
    auto *power = lv_obj_create(header);
    box(power, 414, 10, 48, 25, PANEL, 5);
    lv_obj_set_style_border_color(power, lv_color_hex(GREEN), 0);
    lv_obj_set_style_border_width(power, 1, 0);
    text(power, "USB", 5, 4, 39, GREEN, &lv_font_montserrat_14);
    auto *rail = lv_obj_create(root);
    box(rail, 0, 44, 64, 436, BLACK);
    const CairnAction actions[] = {CairnAction::Home,  CairnAction::Chats,    CairnAction::Nodes,
                                   CairnAction::Radio, CairnAction::Position, CairnAction::Settings};
    const uint32_t colors[] = {CYAN, BLUE, PURPLE, YELLOW, 0xFF3577, MUTED};
    for (unsigned i = 0; i < 6; ++i) {
        tabs[i] = lv_button_create(rail);
        box(tabs[i], 3, 9 + i * 66, 58, 58, BLACK, 15);
        lv_obj_set_user_data(tabs[i], reinterpret_cast<void *>(static_cast<uintptr_t>(actions[i])));
        lv_obj_add_event_cb(tabs[i], clicked, LV_EVENT_CLICKED, this);
        navIcon(tabs[i], i, colors[i]);
    }
    home = lv_obj_create(root);
    box(home, 64, 44, 416, 436, BLACK);
    content = lv_obj_create(root);
    box(content, 64, 44, 416, 436, BLACK);
    lv_obj_add_flag(content, LV_OBJ_FLAG_HIDDEN);
    select(CairnAction::Home);
}

void CairnShell::select(CairnAction action)
{
    const CairnAction actions[] = {CairnAction::Home,  CairnAction::Chats,    CairnAction::Nodes,
                                   CairnAction::Radio, CairnAction::Position, CairnAction::Settings};
    const char *names[] = {"SENSECAP", "Chats", "Nodes", "Radios", "Map", "Settings"};
    const uint32_t colors[] = {CYAN, BLUE, PURPLE, YELLOW, 0xFF3577, MUTED};
    for (unsigned i = 0; i < 6; ++i) {
        const bool selected = actions[i] == action;
        lv_obj_set_style_bg_color(tabs[i], lv_color_hex(selected ? colors[i] : BLACK), 0);
        tint(tabs[i], selected ? BLACK : colors[i]);
        if (selected) {
            lv_label_set_text(title, names[i]);
            lv_obj_set_style_text_color(title, lv_color_hex(i == 0 ? GREEN : TEXT), 0);
        }
    }
}

void CairnDashboard::clicked(lv_event_t *event)
{
    auto *self = static_cast<CairnDashboard *>(lv_event_get_user_data(event));
    const auto action =
        static_cast<CairnAction>(reinterpret_cast<uintptr_t>(lv_obj_get_user_data(lv_event_get_target_obj(event))));
    if (self->navigate)
        self->navigate(action);
}

lv_obj_t *CairnDashboard::card(lv_obj_t *parent, const char *name, const char *symbol, uint32_t accent, CairnAction action)
{
    auto *obj = lv_button_create(parent);
    box(obj, 0, 0, 123, 122, PANEL, 15);
    lv_obj_set_user_data(obj, reinterpret_cast<void *>(static_cast<uintptr_t>(action)));
    lv_obj_add_event_cb(obj, clicked, LV_EVENT_CLICKED, this);
    auto *badge = lv_obj_create(obj);
    box(badge, 10, 11, 25, 25, accent, 6);
    lv_obj_remove_flag(badge, LV_OBJ_FLAG_CLICKABLE);
    auto *icon = text(badge, symbol, 0, 3, 25, BLACK);
    lv_obj_set_style_text_align(icon, LV_TEXT_ALIGN_CENTER, 0);
    text(obj, name, 39, 16, 81, TEXT, &lv_font_montserrat_14);
    return obj;
}

void CairnDashboard::create(lv_obj_t *parent, Navigate callback)
{
    navigate = callback;
    box(parent, 64, 44, 416, 436, BLACK);
    clock = text(parent, "--:--", 10, 8, 239, TEXT, &cairn_clock_72);
    lv_obj_add_flag(clock, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_user_data(clock, reinterpret_cast<void *>(static_cast<uintptr_t>(CairnAction::Clock)));
    lv_obj_add_event_cb(clock, clicked, LV_EVENT_CLICKED, this);
    date = text(parent, "Time not synchronized", 13, 87, 238, MUTED);
    protocol = text(parent, "UTC  /  MESHCORE", 13, 111, 238, GREEN, &lv_font_montserrat_14);

    auto *plotFrame = lv_obj_create(parent);
    box(plotFrame, 259, 10, 146, 117, PANEL, 15);
    chart = lv_chart_create(plotFrame);
    box(chart, 8, 12, 130, 93, PANEL);
    lv_chart_set_type(chart, LV_CHART_TYPE_LINE);
    lv_chart_set_point_count(chart, 120);
    lv_chart_set_range(chart, LV_CHART_AXIS_PRIMARY_Y, -130, -40);
    lv_chart_set_div_line_count(chart, 4, 0);
    lv_obj_set_style_line_color(chart, lv_color_hex(0x554067), LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 1, LV_PART_MAIN);
    lv_obj_set_style_line_dash_width(chart, 2, LV_PART_MAIN);
    lv_obj_set_style_line_dash_gap(chart, 3, LV_PART_MAIN);
    lv_obj_set_style_line_width(chart, 1, LV_PART_ITEMS);
    lv_obj_set_style_size(chart, 0, 0, LV_PART_INDICATOR);
    signalSeries = lv_chart_add_series(chart, lv_color_hex(CYAN), LV_CHART_AXIS_PRIMARY_Y);
    lv_chart_set_all_value(chart, signalSeries, LV_CHART_POINT_NONE);

    auto *cards = lv_obj_create(parent);
    box(cards, 12, 139, 393, 255, BLACK);
    lv_obj_add_flag(cards, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(cards, LV_OBJ_FLAG_SCROLL_MOMENTUM);
    lv_obj_set_scroll_dir(cards, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(cards, LV_SCROLLBAR_MODE_ACTIVE);
    lv_obj_set_style_pad_row(cards, 11, 0);
    lv_obj_set_style_pad_column(cards, 11, 0);
    lv_obj_set_flex_flow(cards, LV_FLEX_FLOW_ROW_WRAP);

    auto *power = card(cards, "POWER", LV_SYMBOL_CHARGE, GREEN, CairnAction::Tools);
    text(power, "USB", 11, 48, 103, GREEN, &lv_font_montserrat_28);
    text(power, "USB supply", 11, 86, 103, MUTED, &lv_font_montserrat_14);

    auto *chats = card(cards, "CHATS", LV_SYMBOL_ENVELOPE, BLUE, CairnAction::Chats);
    chatValue = text(chats, "No messages", 11, 49, 104, BLUE, &lv_font_montserrat_20);
    chatDetail = text(chats, "Listening", 11, 84, 104, MUTED, &lv_font_montserrat_14);

    auto *nodes = card(cards, "NODES", LV_SYMBOL_LIST, PURPLE, CairnAction::Nodes);
    nodeValue = text(nodes, "0", 11, 48, 104, TEXT, &lv_font_montserrat_28);
    text(nodes, "discovered", 11, 86, 104, MUTED, &lv_font_montserrat_14);

    auto *position = card(cards, "POSITION", LV_SYMBOL_GPS, GREEN, CairnAction::Position);
    positionValue = text(position, "Not set", 11, 49, 104, MUTED, &lv_font_montserrat_20);
    positionDetail = text(position, "GPS / saved", 11, 87, 104, MUTED, &lv_font_montserrat_14);

    auto *radio = card(cards, "LORA", LV_SYMBOL_SHUFFLE, YELLOW, CairnAction::Radio);
    frequency = text(radio, "910.525", 10, 45, 108, TEXT, &lv_font_montserrat_20);
    text(radio, "MHz", 11, 75, 104, MUTED, &lv_font_montserrat_14);
    radioDetail = text(radio, "SF7 BW62.5", 11, 98, 108, YELLOW, &lv_font_montserrat_14);

    auto *network = card(cards, "NETWORK", LV_SYMBOL_WIFI, CYAN, CairnAction::Network);
    networkValue = text(network, "Wi-Fi off", 11, 49, 104, MUTED, &lv_font_montserrat_20);
    networkDetail = text(network, "BLE off", 11, 87, 104, MUTED, &lv_font_montserrat_14);

    auto *ham = card(cards, "HAMVIEW", LV_SYMBOL_BELL, BLUE, CairnAction::HamView);
    hamValue = text(ham, "0 spots", 11, 49, 104, BLUE, &lv_font_montserrat_20);
    text(ham, "DX alerts", 11, 87, 104, MUTED, &lv_font_montserrat_14);

    auto *files = card(cards, "FILES", LV_SYMBOL_SD_CARD, PURPLE, CairnAction::Files);
    text(files, "SD card", 11, 49, 104, TEXT, &lv_font_montserrat_20);
    text(files, "Browse files", 11, 87, 104, MUTED, &lv_font_montserrat_14);

    auto *tools = card(cards, "TOOLS", LV_SYMBOL_SETTINGS, YELLOW, CairnAction::Tools);
    text(tools, "Open", 11, 49, 104, TEXT, &lv_font_montserrat_20);
    text(tools, "Utilities", 11, 87, 104, MUTED, &lv_font_montserrat_14);

    const char *captions[] = {"HamView", "Files", "Tools"};
    const CairnAction actions[] = {CairnAction::HamView, CairnAction::Files, CairnAction::Tools};
    const uint32_t accents[] = {BLUE, PURPLE, YELLOW};
    for (unsigned i = 0; i < 3; ++i) {
        auto *shortcut = lv_button_create(parent);
        box(shortcut, 12 + i * 134, 400, 123, 36, BLACK, 5);
        lv_obj_set_style_border_width(shortcut, 1, 0);
        lv_obj_set_style_border_color(shortcut, lv_color_hex(PANEL), 0);
        lv_obj_set_user_data(shortcut, reinterpret_cast<void *>(static_cast<uintptr_t>(actions[i])));
        lv_obj_add_event_cb(shortcut, clicked, LV_EVENT_CLICKED, this);
        auto *caption = text(shortcut, captions[i], 0, 9, 123, accents[i], &lv_font_montserrat_14);
        lv_obj_set_style_text_align(caption, LV_TEXT_ALIGN_CENTER, 0);
    }
}

void CairnDashboard::update(const CairnDashboardState &state)
{
    lv_label_set_text(clock, state.clock);
    lv_label_set_text(date, state.date);
    lv_label_set_text_fmt(protocol, "%s  /  %s", state.clockZone, state.protocol);
    lv_label_set_text(chatValue, state.messageFrom);
    lv_label_set_text(chatDetail, state.messageText);
    lv_label_set_text_fmt(nodeValue, "%lu", static_cast<unsigned long>(state.nodes));
    lv_label_set_text(positionValue, state.position);
    lv_label_set_text(positionDetail, state.positionDetail);
    char value[24];
    snprintf(value, sizeof(value), "%.3f", state.frequency);
    lv_label_set_text(frequency, value);
    snprintf(value, sizeof(value), "SF%u BW%g", state.sf, state.bandwidth);
    lv_label_set_text(radioDetail, state.radioReady ? (state.radioProfile[0] ? state.radioProfile : value) : "Radio starting");
    lv_label_set_text(networkValue, state.network);
    lv_obj_set_style_text_color(networkValue, lv_color_hex(state.wifiConnected ? CYAN : MUTED), 0);
    lv_label_set_text(networkDetail, state.wifiConnected ? "Connected" : "BLE off");
    lv_label_set_text_fmt(hamValue, "%lu spots", static_cast<unsigned long>(state.spots));
    auto *values = lv_chart_get_y_array(chart, signalSeries);
    for (uint16_t i = 0; i < 120; ++i)
        values[i] = i < state.samples ? state.signal[i] : LV_CHART_POINT_NONE;
    lv_chart_refresh(chart);
}
