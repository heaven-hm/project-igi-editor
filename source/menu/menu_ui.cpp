#include "menu_ui.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>

namespace igi {
namespace {

std::string UpperAscii(std::string s) {
    for (char& c : s) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return s;
}

std::string FieldTextOf(const MenuWidget& widget, const char* name) {
    return widget.FieldText(name);
}

// A declared number.  `MenuWidget::FieldNumber` covers the numeric kinds; the
// script's string table can also carry a number as a String value, so that
// encoding is read here too.
double FieldNumber(const MenuWidget& widget, const char* name, double fallback) {
    const MenuValue* value = widget.FindValue(name);
    if (value == nullptr) {
        return fallback;
    }
    if (value->IsNumber()) {
        return value->AsNumber();
    }
    if (value->kind == MenuValueKind::String && !value->text.empty()) {
        char* end = nullptr;
        const double parsed = std::strtod(value->text.c_str(), &end);
        if (end && end != value->text.c_str()) {
            return parsed;
        }
    }
    return fallback;
}

int FieldInt(const MenuWidget& widget, const char* name, int fallback) {
    return static_cast<int>(std::lround(FieldNumber(widget, name, fallback)));
}

bool FieldBool(const MenuWidget& widget, const char* name, bool fallback) {
    const MenuValue* found = widget.FindValue(name);
    if (found == nullptr) {
        return fallback;
    }
    const MenuValue& value = *found;
    if (value.kind == MenuValueKind::String || value.kind == MenuValueKind::Name) {
        const std::string upper = UpperAscii(value.text);
        if (upper == "TRUE") return true;
        if (upper == "FALSE") return false;
        return fallback;
    }
    return value.AsNumber() != 0.0;
}

// Retail widget types that declare a screen rect instead of being placed by a
// parent (src/menu/menumanager.c `MenuManager_OpenScreen` HOOK 0x418710
// resolves them with `Task_FindById` HOOK 0x4F1030).
bool IsScreenType(const std::string& type) {
    return type == "MenuScreen" || type == "DialogWindow";
}

bool IsTextType(const std::string& type) {
    return type == "MenuText" || type == "MenuTextConditional";
}

// Widgets whose create handler sets `centreX`/`centreY` to 1, so
// `MenuWindow_Place` (HOOK 0x424420) centres them in the parent when the parent
// is not lining children up along that axis:
//   MenuText / MenuTextConditional  src/menuscreen/menutext.c:103-104, 318-319
//   SlideBar                        src/menuscreen/slidebar.c:102-103
//   ListBox                         src/menuscreen/listbox.c:101-102
//   ToggleBox                       src/ui/togglebox.c:59-60
//   DialogWindow                    src/menu/dialogwindow.c:68-69
// MenuFrame keeps `MenuWindow_Create`'s 0 (src/widget/menuwindow.c:116-127).
bool IsCentredType(const std::string& type) {
    return IsTextType(type) || type == "SlideBar" || type == "ListBox" ||
           type == "ToggleBox" || type == "DialogWindow";
}

struct LineUpState {
    bool horizontal = false;
    bool vertical = false;
    int x = 0;
    int y = 0;
    int spacingX = 0;
    int spacingY = 0;
};

// `MenuWindow_Arrange` HOOK 0x4244B0, src/widget/menuwindow.c:200-266.  The
// arithmetic (including the integer division) is the retail code's.
LineUpState ComputeLineUp(const MenuWidget& parent, const MenuUiRect& parent_rect,
                          int total_w, int total_h, int count) {
    LineUpState state;
    const MenuUiLineUp mode =
        static_cast<MenuUiLineUp>(FieldInt(parent, menu_field::kLineUpMode, 0));
    if (mode == MenuUiLineUp::None) {
        return state;
    }
    const int spacing_x = FieldInt(parent, menu_field::kXSpacing, 0);
    const int spacing_y = FieldInt(parent, menu_field::kYSpacing, 0);
    const int denominator = count + 1;
    const int n = count - 1 < 1 ? 1 : count - 1;

    switch (mode) {
        case MenuUiLineUp::VertTop:
            state.spacingY = spacing_y;
            state.y = parent_rect.y + state.spacingY;
            state.vertical = true;
            break;
        case MenuUiLineUp::VertCentre:
            state.vertical = true;
            state.spacingY = denominator != 0 ? (parent_rect.h - total_h) / denominator : 0;
            state.y = parent_rect.y + state.spacingY;
            break;
        case MenuUiLineUp::VertBottom:
            state.spacingY = spacing_y;
            state.vertical = true;
            state.y = parent_rect.h - state.spacingY * count + parent_rect.y - total_h;
            break;
        case MenuUiLineUp::VertFillOut:
            state.y = parent_rect.y + spacing_y;
            state.vertical = true;
            state.spacingY = (parent_rect.h - spacing_y * 2 - total_h) / n;
            break;
        case MenuUiLineUp::HorLeft:
            state.spacingX = spacing_x;
            state.x = parent_rect.x + state.spacingX;
            state.horizontal = true;
            break;
        case MenuUiLineUp::HorCentre:
            state.spacingX = denominator != 0 ? (parent_rect.w - total_w) / denominator : 0;
            state.x = parent_rect.x + state.spacingX;
            state.horizontal = true;
            break;
        case MenuUiLineUp::HorRight:
            state.spacingX = spacing_x;
            state.x = parent_rect.w - state.spacingX * count + parent_rect.x - total_w;
            state.horizontal = true;
            break;
        case MenuUiLineUp::HorFillOut:
            state.x = parent_rect.x + spacing_x;
            state.spacingX = (parent_rect.w - spacing_x * 2 - total_w) / n;
            state.horizontal = true;
            break;
        case MenuUiLineUp::None:
        default:
            break;
    }
    return state;
}

// Retail draws an item's text from the text window inside it; a selectable
// MenuFrame has no text field of its own, so its label is the first descendant
// that declares one.
std::string FirstDescendantLabel(const MenuWidget& widget) {
    for (const MenuWidget& child : widget.children) {
        const std::string own = FieldTextOf(child, menu_field::kTextResource);
        if (!own.empty()) {
            return own;
        }
        const std::string nested = FirstDescendantLabel(child);
        if (!nested.empty()) {
            return nested;
        }
    }
    return std::string();
}

// Retail `MenuWindow_GetSize` HOOK 0x424400 reports the window's declared
// width/height; a text window's size comes from the rendered label
// (`MAKE_LABEL`, src/menuscreen/menutext.c:317-330 calls `Text_GetWidth` and
// `Text_GetHeight`).  Widgets whose retail size handler computes something else
// (ListBox, SlideBar, ToggleBox) contribute their declared Width/Height when the
// script declared them, and 0 otherwise; see MENU_UI.md for that gap.
void SizeOf(const MenuWidget& widget, const MenuUiMetrics& metrics, int* out_w, int* out_h) {
    const std::string& type = widget.type;
    if (IsTextType(type)) {
        const std::string label = FieldTextOf(widget, menu_field::kTextResource);
        *out_w = label.empty() ? 0
                               : (metrics.text_width ? metrics.text_width(label)
                                                     : static_cast<int>(label.size()) * 7);
        *out_h = metrics.line_height;
        return;
    }
    *out_w = FieldInt(widget, menu_field::kWidth, 0);
    *out_h = FieldInt(widget, menu_field::kHeight, 0);
}

int DeclaredX(const MenuWidget& widget) {
    return FieldInt(widget, menu_field::kXPos, 0);
}

int DeclaredY(const MenuWidget& widget) {
    return FieldInt(widget, menu_field::kYPos, 0);
}

MenuUiRect ScreenRectOf(const MenuWidget& screen) {
    MenuUiRect rect;
    if (screen.type == "DialogWindow") {
        rect.x = FieldInt(screen, menu_field::kXPos, 0);
        rect.y = FieldInt(screen, menu_field::kYPos, 0);
        rect.w = FieldInt(screen, menu_field::kWidth, 0);
        rect.h = FieldInt(screen, menu_field::kHeight, 0);
        return rect;
    }
    const int x0 = FieldInt(screen, menu_field::kFrameX0, 0);
    const int y0 = FieldInt(screen, menu_field::kFrameY0, 0);
    const int x1 = FieldInt(screen, menu_field::kFrameX1, 0);
    const int y1 = FieldInt(screen, menu_field::kFrameY1, 0);
    rect.x = x0;
    rect.y = y0;
    rect.w = x1 - x0;
    rect.h = y1 - y0;
    return rect;
}

// Retail selectability, from the definition alone:
//   MenuFrame  "Is Selectable"          src/widget/menuframe.c:57
//   MenuText   non-empty "On Click Script"  src/menuscreen/menutext.c
//              `MenuText_Changed` HOOK 0x420F40
//   MenuTextConditional  same, then the enable expression may clear it
//              `MenuTextConditional_Changed` HOOK 0x420640
bool SelectableFromDefinition(const MenuWidget& widget) {
    if (widget.type == "MenuFrame") {
        return FieldBool(widget, menu_field::kIsSelectable, false);
    }
    if (IsTextType(widget.type)) {
        return !FieldTextOf(widget, menu_field::kOnClickScript).empty();
    }
    return false;
}

bool IsInertByDefault(MenuUiAction action) {
    return action == MenuUiAction::None;
}

struct BuildContext {
    const MenuUiBuildOptions& options;
    MenuUiModel* model = nullptr;
    MenuUiScreen* screen = nullptr;
    int collected_items = 0;
    bool item_cap_hit = false;
};

MenuUiEntry MakeEntry(const MenuWidget& widget, const MenuUiRect& rect, bool has_children,
                      bool pruned_ancestor, BuildContext& ctx) {
    MenuUiEntry entry;
    entry.widget_type = widget.type;
    entry.widget_name = widget.name;
    entry.task_id = widget.id;
    entry.rect = rect;
    entry.is_container = has_children;

    const std::string text = FieldTextOf(widget, menu_field::kTextResource);
    entry.has_text = !text.empty();
    entry.label = text;
    entry.font = FieldTextOf(widget, menu_field::kFontResource);
    entry.colour_index = FieldInt(widget, menu_field::kColourIndex, 1);
    entry.action = FieldTextOf(widget, menu_field::kOnClickScript);
    entry.sound = FieldTextOf(widget, menu_field::kClickSoundResource);
    entry.enable_expression = FieldTextOf(widget, menu_field::kIsEnabledExpression);
    entry.visible_expression = FieldTextOf(widget, menu_field::kIsVisibleExpression);
    entry.condition_unverified =
        !entry.enable_expression.empty() || !entry.visible_expression.empty();
    entry.visible = true;  // an unevaluated "isVisible Expression" cannot hide it

    entry.selectable = SelectableFromDefinition(widget);
    // MenuTextConditional with an enable expression: retail sets
    // `w.selectable = enabled` from the evaluated expression, which the editor
    // cannot run, so the item stays selectable and `condition_unverified` says
    // so.
    entry.item_candidate = entry.selectable && !pruned_ancestor;
    entry.collected = entry.item_candidate && entry.enabled;
    return entry;
}

void WalkChildren(const MenuWidget& parent, const MenuUiRect& parent_rect,
                  bool parent_pruned, BuildContext& ctx);

void VisitWidget(const MenuWidget& widget, const MenuUiRect& rect, bool pruned_ancestor,
                 BuildContext& ctx) {
    const bool has_children = !widget.children.empty();
    const MenuUiEntry entry = MakeEntry(widget, rect, has_children, pruned_ancestor, ctx);

    const int index = static_cast<int>(ctx.screen->entries.size());
    ctx.screen->entries.push_back(entry);

    if (entry.item_candidate && entry.enabled) {
        if (ctx.collected_items < ctx.options.max_items) {
            ctx.screen->selectable.push_back(index);
            ctx.collected_items++;
            ctx.screen->entries.back().item_label =
                entry.label.empty() ? FirstDescendantLabel(widget) : entry.label;
        } else {
            ctx.item_cap_hit = true;
            ctx.screen->entries.back().collected = false;
        }
    }

    // Retail draws the children of a selectable window too, but the collection
    // walk (`MenuManager_AddItems` HOOK 0x418840) returns at the selectable
    // window, so its descendants are never items.
    const bool prune_for_children = pruned_ancestor || entry.collected;
    WalkChildren(widget, rect, prune_for_children, ctx);
}

void WalkChildren(const MenuWidget& parent, const MenuUiRect& parent_rect, bool parent_pruned,
                  BuildContext& ctx) {
    if (parent.children.empty()) {
        return;
    }

    int total_w = 0;
    int total_h = 0;
    int count = 0;
    std::vector<std::pair<int, int>> sizes;
    sizes.reserve(parent.children.size());
    for (const MenuWidget& child : parent.children) {
        int w = 0;
        int h = 0;
        SizeOf(child, ctx.options.metrics, &w, &h);
        sizes.emplace_back(w, h);
        total_w += w;
        total_h += h;
        count++;
    }

    LineUpState lineup = ComputeLineUp(parent, parent_rect, total_w, total_h, count);

    for (size_t i = 0; i < parent.children.size(); ++i) {
        const MenuWidget& child = parent.children[i];
        const int w = sizes[i].first;
        const int h = sizes[i].second;

        int x = 0;
        int y = 0;
        if (lineup.horizontal) {
            x = lineup.x;
            lineup.x += w + lineup.spacingX;
        } else if (IsCentredType(child.type)) {
            // MenuWindow_Place: ((parent->w - child->w) >> 1) + parent->x
            x = ((parent_rect.w - w) >> 1) + parent_rect.x;
        } else {
            x = DeclaredX(child);
        }

        if (lineup.vertical) {
            y = lineup.y;
            lineup.y += h + lineup.spacingY;
        } else if (IsCentredType(child.type)) {
            y = ((parent_rect.h - h) >> 1) + parent_rect.y;
        } else {
            y = DeclaredY(child);
        }

        MenuUiRect rect{x, y, w, h};
        VisitWidget(child, rect, parent_pruned, ctx);
    }
}

void RecomputeSelectable(MenuUiModel& model, MenuUiScreen& screen) {
    screen.selectable.clear();
    int count = 0;
    for (size_t i = 0; i < screen.entries.size(); ++i) {
        MenuUiEntry& entry = screen.entries[i];
        entry.collected = entry.item_candidate && entry.enabled;
        if (entry.collected && count < model.max_items) {
            screen.selectable.push_back(static_cast<int>(i));
            count++;
        } else if (entry.collected) {
            entry.collected = false;
        }
    }
    if (screen.selectable.empty()) {
        model.item_index = -1;
    } else if (model.item_index < 0 || model.item_index >= static_cast<int>(screen.selectable.size())) {
        model.item_index = 0;
    }
}

}  // namespace

const char* MenuUiActionName(MenuUiAction action) {
    switch (action) {
        case MenuUiAction::None: return "None";
        case MenuUiAction::Resume: return "Resume";
        case MenuUiAction::ToggleMode: return "ToggleMode";
        case MenuUiAction::ToggleCollision: return "ToggleCollision";
        case MenuUiAction::ToggleFont: return "ToggleFont";
        case MenuUiAction::SelectLevel: return "SelectLevel";
        case MenuUiAction::ToggleAutoSave: return "ToggleAutoSave";
        case MenuUiAction::ToggleLogging: return "ToggleLogging";
        case MenuUiAction::CycleLogLevel: return "CycleLogLevel";
        case MenuUiAction::ToggleMusic: return "ToggleMusic";
        case MenuUiAction::CycleWeather: return "CycleWeather";
        case MenuUiAction::ToggleLightmaps: return "ToggleLightmaps";
        case MenuUiAction::CalculateLightmaps: return "CalculateLightmaps";
        case MenuUiAction::ToggleTerrainOptions: return "ToggleTerrainOptions";
        case MenuUiAction::ToggleTerrainTexture: return "ToggleTerrainTexture";
        case MenuUiAction::ToggleTerrainHeight: return "ToggleTerrainHeight";
        case MenuUiAction::ToggleTerrainDiscard: return "ToggleTerrainDiscard";
        case MenuUiAction::ToggleTerrainFog: return "ToggleTerrainFog";
        case MenuUiAction::AdjustFogIntensity: return "AdjustFogIntensity";
        case MenuUiAction::ResetLevel: return "ResetLevel";
        case MenuUiAction::SaveLevel: return "SaveLevel";
        case MenuUiAction::OpenEditorOptions:
            return "OpenEditorOptions";
        case MenuUiAction::Quit: return "Quit";
    }
    return "None";
}

bool IsMenuUiWidgetType(const std::string& type) {
    static const char* kTypes[] = {
        "MenuFrame", "MenuScreen", "MenuText", "MenuTextConditional", "SlideBar",
        "ListBox", "ToggleBox", "DialogWindow", "ControlsMenu", "MenuButton",
        "ModelViewBox", "TextBox", "PictureBox", "AsciiBox", "InputBox",
        "ScrollListBox", "TypewriterBox", "BackgroundFX",
    };
    for (const char* known : kTypes) {
        if (type == known) {
            return true;
        }
    }
    return false;
}

const MenuUiScreen* MenuUiModel::CurrentScreen() const {
    if (screen_index < 0 || screen_index >= static_cast<int>(screens.size())) {
        return nullptr;
    }
    return &screens[static_cast<size_t>(screen_index)];
}

const MenuUiScreen* MenuUiModel::FindScreen(int id) const {
    if (id == 0) {
        return nullptr;
    }
    for (const MenuUiScreen& screen : screens) {
        if (screen.id == id) {
            return &screen;
        }
    }
    return nullptr;
}

int MenuUiModel::SelectableCount() const {
    const MenuUiScreen* screen = CurrentScreen();
    return screen ? static_cast<int>(screen->selectable.size()) : 0;
}

int MenuUiModel::FocusedEntryIndex() const {
    const MenuUiScreen* screen = CurrentScreen();
    if (!screen || item_index < 0 ||
        item_index >= static_cast<int>(screen->selectable.size())) {
        return -1;
    }
    return screen->selectable[static_cast<size_t>(item_index)];
}

const MenuUiEntry* MenuUiModel::FocusedEntry() const {
    const MenuUiScreen* screen = CurrentScreen();
    const int index = FocusedEntryIndex();
    if (!screen || index < 0) {
        return nullptr;
    }
    return &screen->entries[static_cast<size_t>(index)];
}

MenuUiRect MenuUiModel::ToViewport(const MenuUiRect& rect) const {
    // SCREEN_X/SCREEN_Y, src/menuscreen/menuscreen.h:141-142.
    MenuUiRect out = rect;
    out.x = (viewport_width >> 1) - 320 + rect.x;
    out.y = (viewport_height >> 1) - 240 + rect.y;
    return out;
}

MenuUiRect MenuUiToRetail(const MenuUiRect& rect, int viewport_width, int viewport_height) {
    MenuUiRect out = rect;
    out.x = rect.x - ((viewport_width >> 1) - 320);
    out.y = rect.y - ((viewport_height >> 1) - 240);
    return out;
}

int MenuUiModel::HitTest(int viewport_x, int viewport_y) const {    const MenuUiScreen* screen = CurrentScreen();
    if (!screen) {
        return -1;
    }
    // `MenuManager_Run` converts the pointer into the retail 640x480 space
    // before the windows test themselves:
    //   in->x += 320 - (Display_GetMode()[1] >> 1);
    //   in->y += 240 - (Display_GetMode()[2] >> 1);
    // src/menu/menumanager.c:390-392.
    const int x = viewport_x + 320 - (viewport_width >> 1);
    const int y = viewport_y + 240 - (viewport_height >> 1);

    // `MenuWindow_HitTest` HOOK 0x4248A0, src/widget/menuwindow.c:335-360:
    // inside = rx < x && ry < y && x < rx + w && y < ry + h.
    auto inside = [&](const MenuUiRect& r) {
        return r.x < x && r.y < y && x < r.x + r.w && y < r.y + r.h;
    };

    int first_any = -1;
    for (size_t i = 0; i < screen->entries.size(); ++i) {
        if (!inside(screen->entries[i].rect)) {
            continue;
        }
        if (screen->entries[i].collected) {
            return static_cast<int>(i);
        }
        if (first_any < 0) {
            first_any = static_cast<int>(i);
        }
    }
    return first_any;
}

bool MenuUiModel::MoveDown() {
    const MenuUiScreen* screen = CurrentScreen();
    if (!screen || screen->selectable.empty()) {
        item_index = -1;
        return false;
    }
    // MenuManager_ReadInput, KEY_DOWN (-4): if (++i >= itemCount) i = 0;
    int i = item_index + 1;
    if (i >= static_cast<int>(screen->selectable.size())) {
        i = 0;
    }
    item_index = i;
    return true;
}

bool MenuUiModel::MoveUp() {
    const MenuUiScreen* screen = CurrentScreen();
    if (!screen || screen->selectable.empty()) {
        item_index = -1;
        return false;
    }
    // MenuManager_ReadInput, KEY_UP (-3): if (--i < 0) i = itemCount - 1;
    int i = item_index - 1;
    if (i < 0) {
        i = static_cast<int>(screen->selectable.size()) - 1;
    }
    item_index = i;
    return true;
}

bool MenuUiModel::FocusEntry(int entry_index) {
    const MenuUiScreen* screen = CurrentScreen();
    if (!screen || entry_index < 0 ||
        entry_index >= static_cast<int>(screen->entries.size())) {
        return false;
    }
    if (!screen->entries[static_cast<size_t>(entry_index)].collected) {
        return false;
    }
    for (size_t i = 0; i < screen->selectable.size(); ++i) {
        if (screen->selectable[i] == entry_index) {
            if (item_index == static_cast<int>(i)) {
                return false;
            }
            item_index = static_cast<int>(i);
            return true;
        }
    }
    return false;
}

MenuUiActivation MenuUiModel::Activate() const {
    MenuUiActivation result;
    const MenuUiEntry* entry = FocusedEntry();
    if (!entry) {
        return result;
    }
    result.hit = true;
    result.entry_index = FocusedEntryIndex();
    result.editor_action = entry->editor_action;
    result.inert = entry->inert;
    result.action = entry->action;
    return result;
}

const std::string* MenuUiModel::Back() const {
    const MenuUiScreen* screen = CurrentScreen();
    return screen ? &screen->on_back : nullptr;
}

bool MenuUiModel::OpenScreen(int id) {
    for (size_t i = 0; i < screens.size(); ++i) {
        if (screens[i].id == id && id != 0) {
            screen_index = static_cast<int>(i);
            // MenuManager_CollectItems resets itemIndex to 0 and
            // MenuManager_SetFocus(0) focuses the first item (or none).
            item_index = screens[i].selectable.empty() ? -1 : 0;
            return true;
        }
    }
    return false;
}

bool MenuUiModel::PushScreen(int id) {
    const MenuUiScreen* screen = CurrentScreen();
    if (!screen || screen_stack.size() >= 16) {
        return false;
    }
    if (!OpenScreen(id)) {
        return false;
    }
    screen_stack.push_back(screen->id);
    return true;
}

bool MenuUiModel::PopScreen() {
    if (screen_stack.empty()) {
        return false;
    }
    const int id = screen_stack.back();
    screen_stack.pop_back();
    return OpenScreen(id);
}

bool MenuUiModel::ScreenOnStack(int id) const {
    return std::find(screen_stack.begin(), screen_stack.end(), id) != screen_stack.end();
}

void MenuUiModel::SetEntryEnabled(int entry_index, bool enabled) {
    MenuUiScreen* screen = nullptr;
    if (screen_index >= 0 && screen_index < static_cast<int>(screens.size())) {
        screen = &screens[static_cast<size_t>(screen_index)];
    }
    if (!screen || entry_index < 0 ||
        entry_index >= static_cast<int>(screen->entries.size())) {
        return;
    }
    screen->entries[static_cast<size_t>(entry_index)].enabled = enabled;
    RecomputeSelectable(*this, *screen);
}

void MenuUiModel::SetEntryVisible(int entry_index, bool visible) {
    MenuUiScreen* screen = nullptr;
    if (screen_index >= 0 && screen_index < static_cast<int>(screens.size())) {
        screen = &screens[static_cast<size_t>(screen_index)];
    }
    if (!screen || entry_index < 0 ||
        entry_index >= static_cast<int>(screen->entries.size())) {
        return;
    }
    screen->entries[static_cast<size_t>(entry_index)].visible = visible;
}

MenuUiModel BuildMenuUiModel(const MenuDefinition& definition,
                             const MenuUiBuildOptions& options) {
    MenuUiModel model;
    model.viewport_width = options.viewport_width;
    model.viewport_height = options.viewport_height;
    model.max_items = options.max_items > 0 ? options.max_items : 32;
    model.source = definition.source;

    if (!definition.ok) {
        model.error = definition.error.empty() ? "menu definition is not valid"
                                               : definition.error;
        return model;
    }

    BuildContext ctx{options, &model, nullptr, 0, false};

    // The manager's own "Current menuscreen" is the screen retail opens first
    // (the script sets it; `MenuManager_New` HOOK 0x418B00 then runs
    // `MenuManager_OpenScreen` HOOK 0x418710).
    model.declared_screen_id = FieldInt(definition.root, menu_field::kCurrentMenuScreen, 0);

    for (const MenuWidget& child : definition.root.children) {
        if (!IsScreenType(child.type)) {
            model.warnings.push_back("skipped non-screen widget '" + child.type +
                                     "' directly under the menu manager");
            continue;
        }
        MenuUiScreen screen;
        screen.type = child.type;
        screen.name = child.name;
        screen.popup = child.type == "DialogWindow";
        screen.background = FieldTextOf(child, menu_field::kBackground);
        screen.on_back = FieldTextOf(child, menu_field::kOnPressEscape);
        screen.frame = ScreenRectOf(child);
        screen.id = child.id;
        model.screens.push_back(std::move(screen));
    }

    if (model.screens.empty()) {
        model.error = "menu definition declares no MenuScreen or DialogWindow";
        return model;
    }

    for (size_t i = 0; i < model.screens.size(); ++i) {
        MenuUiScreen& screen = model.screens[i];
        const MenuWidget& widget = definition.root.children[i];
        ctx.screen = &screen;
        ctx.collected_items = 0;
        ctx.item_cap_hit = false;
        WalkChildren(widget, screen.frame, false, ctx);
        if (ctx.item_cap_hit) {
            model.warnings.push_back(
                "screen '" + screen.name + "' has more items than the retail item array (32); "
                "the extra items are present but not selectable");
        }
    }

    model.screen_index = 0;
    if (model.declared_screen_id != 0) {
        if (const MenuUiScreen* declared = model.FindScreen(model.declared_screen_id)) {
            model.screen_index = static_cast<int>(declared - model.screens.data());
        } else {
            model.warnings.push_back(
                "declared screen id " + std::to_string(model.declared_screen_id) +
                " is not in the definition; opening the first screen");
        }
    } else {
        model.warnings.push_back(
            "the menu definition does not declare a current screen id; opening the first screen");
    }

    const MenuUiScreen* current = model.CurrentScreen();
    model.item_index = (current && !current->selectable.empty()) ? 0 : -1;
    model.ok = true;
    return model;
}

}  // namespace igi
