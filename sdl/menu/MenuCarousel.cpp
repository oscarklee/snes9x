#include "MenuCarousel.h"
#include "StringMatcher.h"
#include <dirent.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

MenuCarousel::MenuCarousel() 
    : renderer(nullptr), screenWidth(0), screenHeight(0),
      activeIndex(0), lastFrameTime(0),
      backgroundGradient(nullptr), reflectionOverlay(nullptr),
      reflectionOpacity(0.5f),
      blurRadius(2),          
      minSideBrightness(0.35f)
{
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
    
    boxartManager.setBlurRadius(blurRadius);
    animation.setPosition(0.0f);
    animation.setTarget(0.0f);

    createStaticTextures();
    boxartManager.startWorker();
}

void MenuCarousel::shutdown() {
    saveState();
    boxartManager.shutdown();
    romList.clear();
    if (backgroundGradient) { SDL_DestroyTexture(backgroundGradient); backgroundGradient = nullptr; }
    if (reflectionOverlay) { SDL_DestroyTexture(reflectionOverlay); reflectionOverlay = nullptr; }
}

void MenuCarousel::saveState() {
    const char* home_env = getenv("HOME");
    std::string home = home_env ? std::string(home_env) : "/root";
    if (romList.empty()) return;
    
    std::string path = home + "/.snes9x/last_rom";
    FILE* f = fopen(path.c_str(), "w");
    if (f) {
        int idx = getSelectedIndex();
        if (idx >= 0 && idx < (int)romList.size()) {
            fprintf(f, "%s", romList[idx].filename.c_str());
        }
        fclose(f);
    }
}

void MenuCarousel::loadState() {
    const char* home_env = getenv("HOME");
    std::string home = home_env ? std::string(home_env) : "/root";
    if (romList.empty()) return;
    
    std::string path = home + "/.snes9x/last_rom";
    FILE* f = fopen(path.c_str(), "r");
    if (f) {
        char buf[512];
        if (fgets(buf, sizeof(buf), f)) {
            std::string lastRom = buf;
            size_t last = lastRom.find_last_not_of(" \n\r\t");
            if (last != std::string::npos) {
                lastRom = lastRom.substr(0, last + 1);
            }
            
            for (size_t i = 0; i < romList.size(); i++) {
                if (romList[i].filename == lastRom) {
                    activeIndex = (int)i;
                    animation.setPosition((float)i);
                    animation.setTarget((float)i);
                    printf("Menu: Restored selection to [%zu] %s\n", i, lastRom.c_str());
                    break;
                }
            }
        }
        fclose(f);
    }
}

void MenuCarousel::createStaticTextures() {
    if (!renderer) return;

    SDL_Surface* bgSurf = SDL_CreateRGBSurfaceWithFormat(0, 1, 256, 24, SDL_PIXELFORMAT_RGB24);
    if (bgSurf) {
        SDL_LockSurface(bgSurf);
        for (int y = 0; y < 256; y++) {
            float t = (float)y / 255.0f;
            Uint8 r, g, b;
            if (t < 0.5f) {
                float localT = t * 2.0f;
                r = (Uint8)(0x0f + localT * (0x2a - 0x0f));
                g = (Uint8)(0x0f + localT * (0x1a - 0x0f));
                b = (Uint8)(0x11 + localT * (0x35 - 0x11));
            } else {
                float localT = (t - 0.5f) * 2.0f;
                r = (Uint8)(0x2a - localT * (0x2a - 0x0f));
                g = (Uint8)(0x1a - localT * (0x1a - 0x0f));
                b = (Uint8)(0x35 - localT * (0x35 - 0x11));
            }
            Uint8* row = (Uint8*)bgSurf->pixels + y * bgSurf->pitch;
            row[0] = r; row[1] = g; row[2] = b;
        }
        SDL_UnlockSurface(bgSurf);
        backgroundGradient = SDL_CreateTextureFromSurface(renderer, bgSurf);
        SDL_FreeSurface(bgSurf);
    }

    SDL_Surface* refSurf = SDL_CreateRGBSurfaceWithFormat(0, 1, 128, 32, SDL_PIXELFORMAT_RGBA8888);
    if (refSurf) {
        SDL_LockSurface(refSurf);
        for (int y = 0; y < 128; y++) {
            float t = (float)y / 127.0f;
            float factor = std::pow(t, 0.5f); 
            Uint8 alpha = (Uint8)(factor * 255);
            Uint32* row = (Uint32*)((Uint8*)refSurf->pixels + y * refSurf->pitch);
            *row = (Uint32)SDL_MapRGBA(refSurf->format, 0x0f, 0x0f, 0x11, alpha);
        }
        SDL_UnlockSurface(refSurf);
        reflectionOverlay = SDL_CreateTextureFromSurface(renderer, refSurf);
        if (reflectionOverlay) SDL_SetTextureBlendMode(reflectionOverlay, SDL_BLENDMODE_BLEND);
        SDL_FreeSurface(refSurf);
    }
}

void MenuCarousel::setLibretroNames(const std::vector<std::string>& names) {
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

    loadState();

    int n = (int)romList.size();
    if (n > 0) {
        int start = std::max(0, activeIndex - 50);
        int end = std::min(n, activeIndex + 50);
        for (int i = start; i < end; i++) {
            boxartManager.requestBoxart(romList[i].filename, romList[i].displayName, false, false);
        }
    }
}

void MenuCarousel::loadVisibleBoxarts() {
    if (romList.empty()) return;
    
    int currentIdx = getSelectedIndex();
    
    for (int offset = 0; offset <= VISIBLE_RANGE; offset++) {
        int o_list[] = {offset, -offset};
        for (int i = 0; i < 2; i++) {
            if (offset == 0 && i == 1) continue;
            int o = o_list[i];
            int idx = wrap(0, (int)romList.size(), currentIdx + o);
            RomEntry& rom = romList[idx];
            if (!rom.boxartLoaded) {
                boxartManager.requestBoxart(rom.filename, rom.displayName, true, false);
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

void MenuCarousel::moveUp() {
    if (romList.empty()) return;
    activeIndex -= 10;
    animation.setTarget((float)activeIndex);
}

void MenuCarousel::moveDown() {
    if (romList.empty()) return;
    activeIndex += 10;
    animation.setTarget((float)activeIndex);
}

void MenuCarousel::update(float deltaTime) {
    animation.update(deltaTime);
    boxartManager.pollResults();
    
    for (int offset = -VISIBLE_RANGE; offset <= VISIBLE_RANGE; offset++) {
        int idx = wrap(0, (int)romList.size(), activeIndex + offset);
        if (!romList[idx].boxartLoaded) {
            SDL_Texture* tex = boxartManager.getTexture(romList[idx].filename);
            if (tex && tex != boxartManager.getTexture("NON_EXISTENT_FORCE_PLACEHOLDER")) {
                romList[idx].boxartLoaded = true;
            }
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
    // Gradual linear scaling for a smoother "breathing" effect
    float t = std::max(0.0f, 1.0f - absOffset);
    return 0.8f + (1.3f - 0.8f) * t; 
}

float MenuCarousel::calculateBrightness(float absOffset) const {
    float t = std::max(0.0f, 1.0f - absOffset);
    return minSideBrightness + (1.0f - minSideBrightness) * t;
}

int MenuCarousel::calculateBlurLevel(float absOffset) const {
    if (absOffset < 0.5f) return 0;
    return 1;
}

void MenuCarousel::renderBackground() {
    if (backgroundGradient) {
        SDL_RenderCopy(renderer, backgroundGradient, nullptr, nullptr);
    } else {
        SDL_SetRenderDrawColor(renderer, 0x10, 0x10, 0x15, 0xFF);
        SDL_RenderClear(renderer);
    }
}

void MenuCarousel::renderReflection(int x, int y, int w, int h, const std::string& romName, float opacity) {
    if (reflectionOpacity <= 0.0f) return;
    
    SDL_Texture* tex = boxartManager.getTexture(romName, 1);
    if (!tex || tex == boxartManager.getTexture("NON_EXISTENT")) return;

    SDL_SetTextureAlphaMod(tex, (Uint8)(opacity * reflectionOpacity * 255.0f)); 
    SDL_Rect dst = {x - w/2, y, w, h};
    SDL_RenderCopyEx(renderer, tex, nullptr, &dst, 0, nullptr, SDL_FLIP_VERTICAL);
    SDL_SetTextureAlphaMod(tex, 255);

    if (reflectionOverlay) {
        SDL_RenderCopy(renderer, reflectionOverlay, nullptr, &dst);
    }
}

void MenuCarousel::renderCard(int offset, const RomEntry& rom, float animPos) {
    float visualOffset = (float)(activeIndex + offset) - animPos;
    int x = screenWidth / 2 + (int)(visualOffset * (CARD_WIDTH + GAP));
    int y = screenHeight / 2 - 20;
    
    float absOffset = std::abs(visualOffset);
    float scale = calculateScale(absOffset);
    float brightness = calculateBrightness(absOffset);
    int blurLevel = calculateBlurLevel(absOffset);
    
    SDL_Texture* tex = boxartManager.getTexture(rom.filename, blurLevel);
    int w = (int)(CARD_WIDTH * scale);
    int h = (int)(CARD_HEIGHT * scale);

    if (tex) {
        int texW, texH;
        SDL_QueryTexture(tex, nullptr, nullptr, &texW, &texH);
        float texAspect = (float)texW / texH;
        float cardAspect = (float)CARD_WIDTH / CARD_HEIGHT;
        
        if (texAspect > cardAspect) {
            h = (int)(w / texAspect);
        } else {
            w = (int)(h * texAspect);
        }
        
        Uint8 colorMod = (Uint8)(brightness * 255);
        SDL_SetTextureColorMod(tex, colorMod, colorMod, colorMod);
        SDL_Rect dst = {x - w/2, y - h/2, w, h};
        
        renderReflection(x, y + h/2 + 15, w, h, rom.filename, brightness * 0.4f);
        SDL_RenderCopy(renderer, tex, nullptr, &dst);
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
    
    SDL_RenderPresent(renderer);
}
