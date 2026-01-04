#include "MenuCarousel.h"
#include "StringMatcher.h"
#include <dirent.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>

MenuCarousel::MenuCarousel() 
    : renderer(nullptr), screenWidth(0), screenHeight(0),
      activeIndex(0), lastFrameTime(0) {
    animation.speed = 15.0f; 
}

MenuCarousel::~MenuCarousel() {
    shutdown();
}

void MenuCarousel::init(SDL_Renderer* r, int w, int h) {
    renderer = r;
    screenWidth = w;
    screenHeight = h;
    lastFrameTime = SDL_GetTicks();
    
    boxartManager.init(renderer);
    animation.setPosition(0.0f);
    animation.setTarget(0.0f);
}

void MenuCarousel::shutdown() {
    boxartManager.shutdown();
    romList.clear();
}

void MenuCarousel::setLibretroNames(const std::vector<std::string>& names) {
    // libretroNames is now dynamic in BoxartManager, but we can still set initial if provided
}

void MenuCarousel::scanRomDirectory(const std::string& romDir) {
    romList.clear();
    
    DIR* dir = opendir(romDir.c_str());
    if (!dir) return;
    
    struct dirent* ent;
    while ((ent = readdir(dir)) != nullptr) {
        std::string name = ent->d_name;
        if (name.size() <= 4) continue;
        
        std::string ext = name.substr(name.size() - 4);
        std::transform(ext.begin(), ext.end(), ext.begin(), ::tolower);
        
        if (ext == ".sfc" || ext == ".smc" || ext == ".zip" || ext == ".fig") {
            RomEntry entry;
            entry.filename = name;
            entry.fullPath = romDir + "/" + name;
            entry.displayName = StringMatcher::normalize(name);
            
            size_t dotPos = entry.displayName.find_last_of('.');
            if (dotPos != std::string::npos) {
                entry.displayName = entry.displayName.substr(0, dotPos);
            }
            
            if (!entry.displayName.empty()) {
                entry.displayName[0] = std::toupper(entry.displayName[0]);
            }
            for (size_t i = 1; i < entry.displayName.size(); i++) {
                if (entry.displayName[i-1] == ' ' && i < entry.displayName.size()) {
                    entry.displayName[i] = std::toupper(entry.displayName[i]);
                }
            }
            
            entry.boxartLoaded = false;
            romList.push_back(entry);
        }
    }
    
    closedir(dir);
    
    std::sort(romList.begin(), romList.end(), 
              [](const RomEntry& a, const RomEntry& b) {
                  return a.displayName < b.displayName;
              });
    
    activeIndex = 0;
    animation.setPosition(0.0f);
    animation.setTarget(0.0f);

    // Initial bulk download in outside-in pattern
    int n = (int)romList.size();
    if (n > 0) {
        printf("Menu: Starting bulk boxart sync for %d ROMs...\n", n);
        for (int i = 0; i < (n + 1) / 2; i++) {
            boxartManager.requestBoxart(romList[i].filename, romList[i].displayName, false);
            if (i < n - 1 - i) {
                boxartManager.requestBoxart(romList[n - 1 - i].filename, romList[n - 1 - i].displayName, false);
            }
        }
    }
}

void MenuCarousel::loadVisibleBoxarts() {
    if (romList.empty()) return;
    
    int currentIdx = getSelectedIndex();
    
    // Request visible items (prioritize center and neighbors)
    for (int offset = 0; offset <= VISIBLE_RANGE; offset++) {
        int o_list[] = {offset, -offset};
        for (int i = 0; i < 2; i++) {
            if (offset == 0 && i == 1) continue;
            int o = o_list[i];
            int idx = wrap(0, (int)romList.size(), currentIdx + o);
            RomEntry& rom = romList[idx];
            if (!rom.boxartLoaded) {
                boxartManager.requestBoxart(rom.filename, rom.displayName, true);
            }
        }
    }

    // Unload far items
    for (size_t i = 0; i < romList.size(); i++) {
        if (romList[i].boxartLoaded) {
            int dist = std::abs((int)i - currentIdx);
            if (dist > (int)romList.size() / 2) dist = (int)romList.size() - dist;
            
            if (dist > VISIBLE_RANGE) {
                printf("Menu: Unloading far boxart [%zu]: %s\n", i, romList[i].filename.c_str());
                boxartManager.unloadBoxart(romList[i].filename);
                romList[i].boxartLoaded = false;
            }
        }
    }
}

void MenuCarousel::moveLeft() {
    if (romList.empty()) return;
    activeIndex--;
    animation.setTarget((float)activeIndex);
}

void MenuCarousel::moveRight() {
    if (romList.empty()) return;
    activeIndex++;
    animation.setTarget((float)activeIndex);
}

void MenuCarousel::update(float deltaTime) {
    animation.update(deltaTime);
    
    boxartManager.pollResults();
    
    // Update loaded state from BoxartManager
    for (int offset = -VISIBLE_RANGE; offset <= VISIBLE_RANGE; offset++) {
        int idx = wrap(0, (int)romList.size(), activeIndex + offset);
        SDL_Texture* tex = boxartManager.getTexture(romList[idx].filename);
        if (tex && tex != boxartManager.getTexture("NON_EXISTENT_FORCE_PLACEHOLDER")) {
            romList[idx].boxartLoaded = true;
        }
    }
    
    loadVisibleBoxarts();
}

int MenuCarousel::wrap(int min, int max, int value) const {
    if (max <= min) return min;
    int rangeSize = max - min;
    int res = (value - min) % rangeSize;
    if (res < 0) res += rangeSize;
    return res + min;
}

int MenuCarousel::getSelectedIndex() const {
    if (romList.empty()) return -1;
    return wrap(0, romList.size(), activeIndex);
}

std::string MenuCarousel::getSelectedRomPath() const {
    int idx = getSelectedIndex();
    if (idx < 0 || idx >= (int)romList.size()) return "";
    return romList[idx].fullPath;
}

std::string MenuCarousel::getSelectedRomName() const {
    int idx = getSelectedIndex();
    if (idx < 0 || idx >= (int)romList.size()) return "";
    return romList[idx].filename;
}

float MenuCarousel::calculateScale(float absOffset) const {
    if (absOffset < 0.1f) return 1.15f;
    return std::max(1.0f - absOffset * 0.15f, 0.65f);
}

float MenuCarousel::calculateBrightness(float absOffset) const {
    if (absOffset < 0.1f) return 1.0f;
    return std::max(1.0f - absOffset * 0.4f, 0.3f);
}

int MenuCarousel::calculateBlurLevel(float absOffset) const {
    if (absOffset < 0.1f) return 0;
    return std::min((int)(absOffset * 2.0f), 4);
}

void MenuCarousel::renderBackground() {
    SDL_SetRenderDrawColor(renderer, 0x0f, 0x0f, 0x11, 0xFF);
    SDL_RenderClear(renderer);
    
    for (int y = 0; y < screenHeight; y++) {
        float t = (float)y / (float)screenHeight;
        Uint8 r, g, b;
        if (t < 0.5f) {
            float localT = t * 2.0f;
            r = (Uint8)(0x0f + localT * (0x1a - 0x0f));
            g = (Uint8)(0x0f + localT * (0x1a - 0x0f));
            b = (Uint8)(0x11 + localT * (0x20 - 0x11));
        } else {
            float localT = (t - 0.5f) * 2.0f;
            r = (Uint8)(0x1a - localT * (0x1a - 0x0f));
            g = (Uint8)(0x1a - localT * (0x1a - 0x0f));
            b = (Uint8)(0x20 - localT * (0x20 - 0x11));
        }
        SDL_SetRenderDrawColor(renderer, r, g, b, 0xFF);
        SDL_RenderDrawLine(renderer, 0, y, screenWidth, y);
    }
}

void MenuCarousel::renderReflection(int x, int y, int w, int h, const std::string& romName, float opacity) {
    SDL_Texture* tex = boxartManager.getReflectionTexture(romName);
    if (!tex) return;
    SDL_SetTextureAlphaMod(tex, (Uint8)(opacity * 255));
    SDL_Rect dst = {x - w/2, y, w, h};
    SDL_RenderCopyEx(renderer, tex, nullptr, &dst, 0, nullptr, SDL_FLIP_VERTICAL);
}

void MenuCarousel::renderTitle(const std::string& title, int x, int y) {
    static std::string lastTitle;
    if (title != lastTitle) {
        int idx = getSelectedIndex();
        bool loaded = romList[idx].boxartLoaded;
        printf("Menu Selection: [%d] %s | Image status: %s\n", idx, title.c_str(), loaded ? "LOADED" : "PENDING");
        lastTitle = title;
    }
    
    // Draw semi-transparent background for central title
    int textWidth = title.length() * 10;
    SDL_Rect bg = {x - textWidth/2 - 10, y - 15, textWidth + 20, 30};
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 180);
    SDL_RenderFillRect(renderer, &bg);
}

void MenuCarousel::renderCard(int offset, const RomEntry& rom, float animPos) {
    float visualOffset = (float)(activeIndex + offset) - animPos;
    int x = screenWidth / 2 + (int)(visualOffset * (CARD_WIDTH + GAP));
    int y = screenHeight / 2 - 50;
    
    float absOffset = std::abs(visualOffset);
    float scale = calculateScale(absOffset);
    float brightness = calculateBrightness(absOffset);
    int blurLevel = calculateBlurLevel(absOffset);
    
    int w = (int)(CARD_WIDTH * scale);
    int h = (int)(CARD_HEIGHT * scale);
    
    renderReflection(x, y + h/2 + 10, w, h, rom.filename, brightness * 0.4f);
    
    SDL_Texture* tex = boxartManager.getTexture(rom.filename, blurLevel);
    if (tex) {
        Uint8 colorMod = (Uint8)(brightness * 255);
        SDL_SetTextureColorMod(tex, colorMod, colorMod, colorMod);
        SDL_Rect dst = {x - w/2, y - h/2, w, h};
        SDL_RenderCopy(renderer, tex, nullptr, &dst);
        
        // Draw card label in bottom-left corner of the card
        if (absOffset < 1.5f) {
            std::string label = rom.displayName;
            if (label.length() > 20) label = label.substr(0, 17) + "...";
            
            SDL_Rect labelBg = {x - w/2 + 5, y + h/2 - 25, (int)label.length() * 8 + 10, 20};
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 150);
            SDL_RenderFillRect(renderer, &labelBg);
        }
    }
    
    if (absOffset < 0.1f) {
        SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0xFF, 50);
        SDL_Rect border = {x - w/2 - 2, y - h/2 - 2, w + 4, h + 4};
        SDL_RenderDrawRect(renderer, &border);
    }
}

void MenuCarousel::renderNoRomsMessage() {
    SDL_SetRenderDrawColor(renderer, 0x0f, 0x0f, 0x11, 0xFF);
    SDL_RenderClear(renderer);
}

void MenuCarousel::render() {
    if (romList.empty()) {
        renderNoRomsMessage();
        SDL_RenderPresent(renderer);
        return;
    }
    
    renderBackground();
    float animPos = animation.getPosition();
    
    std::vector<std::pair<int, int>> renderOrder;
    for (int offset = -VISIBLE_RANGE; offset <= VISIBLE_RANGE; offset++) {
        int zIndex = 100 - std::abs(offset);
        renderOrder.push_back({zIndex, offset});
    }
    std::sort(renderOrder.begin(), renderOrder.end());
    
    for (const auto& item : renderOrder) {
        int offset = item.second;
        int dataIdx = wrap(0, (int)romList.size(), activeIndex + offset);
        renderCard(offset, romList[dataIdx], animPos);
    }
    
    int centerIdx = getSelectedIndex();
    renderTitle(romList[centerIdx].displayName, screenWidth / 2, screenHeight / 2 + (int)(CARD_HEIGHT * 1.15f / 2) + 40);
    
    SDL_RenderPresent(renderer);
}
