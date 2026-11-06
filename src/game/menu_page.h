#pragma once
// Pages of the game's own menus made by us (src/game/pc_settings_page.cpp builds them from AV Options' records, so
// they draw, sound and move as the game's pages do): rows, the guest calls that fill and read them, and the pages.
#include <cstdint>
#include <initializer_list>
#include <utility>
#include <vector>

#include "xdk/hle.h"

namespace nf {

enum MenuRowKind { kMenuSlider, kMenuChoice, kMenuButton };
struct MenuRow {
    uint32_t id;
    MenuRowKind kind;
    const char* label;
    const char* help;
    int slot = -1;  // the line it is drawn on (first_y + slot * step); -1 the next line
};

// a page from AV Options' records: its title, prompt line, help box and pictures, and these rows one under another
std::vector<uint8_t> BuildPage(const std::vector<uint8_t>& av, uint32_t (*record_size)(uint32_t), uint32_t page_id,
                               const char* title, const std::vector<MenuRow>& rows, int16_t first_y, int16_t step);

// a string in guest memory, made once for each text
uint32_t MenuString(const char* s);
// the widget message dispatcher's call (sub_72820: player, id, message, a, b)
uint32_t MenuSet(X86Context& ctx, uint32_t player, uint32_t id, uint32_t msg, uint32_t a, uint32_t b = 0);
// a choice's items (text, value) and the one shown
void MenuChoice(X86Context& ctx, uint32_t player, uint32_t id,
                const std::vector<std::pair<const char*, uint32_t>>& items, uint32_t value);
uint32_t MenuChoiceValue(X86Context& ctx, uint32_t player, uint32_t id);
std::vector<std::pair<const char*, uint32_t>> MenuNumbered(std::initializer_list<const char*> items);
// the help box's line for a row
void MenuHelp(X86Context& ctx, uint32_t player, const char* text);
// to another page (the menu's stack: back returns), and back
void MenuGoTo(X86Context& ctx, uint32_t player, uint32_t page);
void MenuBack(X86Context& ctx, uint32_t player);
// the focus on a row of the page entered (its widget, message 0x4C's): a level's menu would keep the row the page
// before had
void MenuFocus(X86Context& ctx, uint32_t player, uint32_t page, uint32_t id);

// the Graphics page and its style pages (src/game/graphics_page.cpp)
std::vector<std::vector<uint8_t>> GraphicsPages(const std::vector<uint8_t>& av, uint32_t (*record_size)(uint32_t));
bool GraphicsPageMessage(X86Context& ctx, uint32_t player, uint32_t widget, uint32_t msg, uint32_t arg);
void OpenGraphicsPage(X86Context& ctx, uint32_t player);
void OpenMorePage(X86Context& ctx, uint32_t player);
void OpenNetplayPage(X86Context& ctx, uint32_t player);  // the main menu's Netplay (LAN play)
void NetplayPageFrame(X86Context& ctx);                  // each frame: the games found  // Settings' More Settings (the style page's last row set)
bool MenuRowHidden(uint32_t page, uint32_t id);  // a row the page has hidden (the style page's other styles')

}  // namespace nf
