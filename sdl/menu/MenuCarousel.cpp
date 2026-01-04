#include "MenuCarousel.h"
#include "StringMatcher.h"
#include "../../var8x10font.h"
#include <dirent.h>
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>

static const int FONT_WIDTH = 8;
static const int FONT_HEIGHT = 10;

MenuCarousel::MenuCarousel() 
    : renderer(nullptr), screenWidth(0), screenHeight(0),
      activeIndex(0), lastFrameTime(0),
      backgroundGradient(nullptr), reflectionOverlay(nullptr),
      currentTitle(""), titleAlpha(1.0f),
      reflectionOpacity(0.5f),
      blurRadius(2),          
      minSideBrightness(0.35f)
{
    // Spring physics will now handle the bounce naturally
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
}

void MenuCarousel::shutdown() {
    saveState();
    boxartManager.shutdown();
    romList.clear();
    if (backgroundGradient) { SDL_DestroyTexture(backgroundGradient); backgroundGradient = nullptr; }
    if (reflectionOverlay) { SDL_DestroyTexture(reflectionOverlay); reflectionOverlay = nullptr; }
}

void MenuCarousel::saveState() {
    const char* home = getenv("HOME");
    if (!home || romList.empty()) return;
    
    std::string path = std::string(home) + "/.snes9x/last_rom";
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
    const char* home = getenv("HOME");
    if (!home || romList.empty()) return;
    
    std::string path = std::string(home) + "/.snes9x/last_rom";
    FILE* f = fopen(path.c_str(), "r");
    if (f) {
        char buf[512];
        if (fgets(buf, sizeof(buf), f)) {
            std::string lastRom = buf;
            // Robust trimming of whitespace and newlines
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

    printf("MenuCarousel: Creating static textures...\n");

    // Create background gradient (1x256)
    SDL_Surface* bgSurf = SDL_CreateRGBSurfaceWithFormat(0, 1, 256, 32, SDL_PIXELFORMAT_RGBA8888);
    if (bgSurf) {
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
            Uint32* row = (Uint32*)((Uint8*)bgSurf->pixels + y * bgSurf->pitch);
            *row = (r << 24) | (g << 16) | (b << 8) | 0xFF;
        }
        backgroundGradient = SDL_CreateTextureFromSurface(renderer, bgSurf);
        SDL_FreeSurface(bgSurf);
    }

    // Create reflection overlay (1x128)
    SDL_Surface* refSurf = SDL_CreateRGBSurfaceWithFormat(0, 1, 128, 32, SDL_PIXELFORMAT_RGBA8888);
    if (refSurf) {
        for (int y = 0; y < 128; y++) {
            float t = (float)y / 127.0f;
            float factor = std::pow(t, 0.5f); 
            Uint8 alpha = (Uint8)(factor * 255);
            Uint32* row = (Uint32*)((Uint8*)refSurf->pixels + y * refSurf->pitch);
            *row = (0x0f << 24) | (0x0f << 16) | (0x11 << 8) | alpha;
        }
        reflectionOverlay = SDL_CreateTextureFromSurface(renderer, refSurf);
        if (reflectionOverlay) SDL_SetTextureBlendMode(reflectionOverlay, SDL_BLENDMODE_BLEND);
        SDL_FreeSurface(refSurf);
    }
    printf("MenuCarousel: Static textures created.\n");
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

    loadState();

    // Initial bulk download in outside-in pattern (Process for memory as well)
    int n = (int)romList.size();
    if (n > 0) {
        printf("Menu: Starting proactive background load for %d ROMs...\n", n);
        for (int i = 0; i < (n + 1) / 2; i++) {
            boxartManager.requestBoxart(romList[i].filename, romList[i].displayName, false, false);
            if (i < n - 1 - i) {
                boxartManager.requestBoxart(romList[n - 1 - i].filename, romList[n - 1 - i].displayName, false, false);
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
                boxartManager.requestBoxart(rom.filename, rom.displayName, true, false);
            }
        }
    }

    // Proactive loading: remove aggressive unloading to keep all 700+ images in memory
    // (Total memory for all is now only ~70MB)
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
    
    // Title fade logic
    int centerIdx = getSelectedIndex();
    std::string newTitle = (centerIdx >= 0) ? romList[centerIdx].displayName : "";
    
    if (newTitle != currentTitle) {
        titleAlpha -= deltaTime * 8.0f; // Rapid fade out
        if (titleAlpha <= 0.0f) {
            currentTitle = newTitle;
            titleAlpha = 0.0f;
        }
    } else if (titleAlpha < 1.0f) {
        titleAlpha += deltaTime * 4.0f; // Slower fade in
        if (titleAlpha > 1.0f) titleAlpha = 1.0f;
    }

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
    return std::max(1.0f - absOffset * 0.15f, 0.75f);
}

float MenuCarousel::calculateBrightness(float absOffset) const {
    if (absOffset < 0.1f) return 1.0f;
    return std::max(minSideBrightness, 1.0f - absOffset * 0.3f);
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
    if (reflectionOpacity <= 0.0f) return; // Total transparency requested
    
    SDL_Texture* tex = boxartManager.getTexture(romName, 1); // Use blurred for reflection
    if (!tex || tex == boxartManager.getTexture("NON_EXISTENT")) return;

    // 1. Render flipped boxart with configurable base opacity (opacity parameter here is already dimmed)
    SDL_SetTextureAlphaMod(tex, (Uint8)(opacity * reflectionOpacity * 255.0f)); 
    SDL_Rect dst = {x - w/2, y, w, h};
    SDL_RenderCopyEx(renderer, tex, nullptr, &dst, 0, nullptr, SDL_FLIP_VERTICAL);
    SDL_SetTextureAlphaMod(tex, 255); // Reset

    // 2. Overlay the fade-out gradient
    if (reflectionOverlay) {
        SDL_RenderCopy(renderer, reflectionOverlay, nullptr, &dst);
    }
}

void MenuCarousel::renderTitle(const std::string& title, int x, int y) {
    if (currentTitle.empty()) return;
    
    int boxWidth = (int)(screenWidth * 0.75f);
    int boxHeight = 34;
    SDL_Rect bg = { x - boxWidth / 2, y - boxHeight / 2, boxWidth, boxHeight };
    
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, (Uint8)(titleAlpha * 255));
    
    // Draw square rectangle
    SDL_RenderFillRect(renderer, &bg);
    
    // Draw 1px border for the title box as well (square)
    SDL_SetRenderDrawColor(renderer, 0xCC, 0xCC, 0xCC, (Uint8)(titleAlpha * 200));
    SDL_RenderDrawRect(renderer, &bg);

    SDL_Color black = {0, 0, 0, (Uint8)(titleAlpha * 255)};
    
    // Calculate exact width for perfect centering
    int totalWidth = 0;
    for (size_t i = 0; i < currentTitle.length(); i++) {
        unsigned char c = (unsigned char)currentTitle[i];
        if (c < 32) c = '?';
        int cindex = c - 32;
        if (cindex >= 224) cindex = '?' - 32;
        int kernStart = var8x10font_kern[cindex][0];
        int kernEnd = var8x10font_kern[cindex][1];
        totalWidth += (int)((FONT_WIDTH - kernStart - kernEnd) * 1.5f);
    }
    
    renderText(currentTitle, x - totalWidth / 2, y - 7, black, 1.5f);
}

void MenuCarousel::renderText(const std::string& text, int x, int y, SDL_Color color, float scale) {
    SDL_SetRenderDrawColor(renderer, color.r, color.g, color.b, color.a);
    
    int currentX = x;
    for (size_t i = 0; i < text.length(); i++) {
        unsigned char c = (unsigned char)text[i];
        if (c < 32) c = '?';
        
        int cindex = c - 32;
        if (cindex >= 224) cindex = '?' - 32;
        int kernStart = var8x10font_kern[cindex][0];
        int kernEnd = var8x10font_kern[cindex][1];
        int charWidth = FONT_WIDTH - kernStart - kernEnd;
        
        for (int row = 0; row < FONT_HEIGHT; row++) {
            for (int col = 0; col < FONT_WIDTH; col++) {
                int charColInSheet = cindex % 16;
                int charRowInSheet = cindex / 16;
                
                int totalRow = charRowInSheet * FONT_HEIGHT + row;
                int totalCol = charColInSheet * FONT_WIDTH + col;
                
                if (var8x10font[totalRow][totalCol] == '#') {
                    if (scale == 1.0f) {
                        SDL_RenderDrawPoint(renderer, currentX + (col - kernStart), y + row);
                    } else {
                        SDL_Rect pixel = {
                            (int)(currentX + (col - kernStart) * scale),
                            (int)(y + row * scale),
                            (int)std::ceil(scale),
                            (int)std::ceil(scale)
                        };
                        SDL_RenderFillRect(renderer, &pixel);
                    }
                }
            }
        }
        currentX += (int)(charWidth * scale);
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
    
    int w = (int)(CARD_WIDTH * scale);
    int h = (int)(CARD_HEIGHT * scale);
    
    // 15px separation between image and reflection (as requested)
    renderReflection(x, y + h/2 + 15, w, h, rom.filename, brightness * 0.4f);
    
    SDL_Texture* tex = boxartManager.getTexture(rom.filename, blurLevel);
    if (tex) {
        Uint8 colorMod = (Uint8)(brightness * 255);
        SDL_SetTextureColorMod(tex, colorMod, colorMod, colorMod);
        SDL_Rect dst = {x - w/2, y - h/2, w, h};
        SDL_RenderCopy(renderer, tex, nullptr, &dst);
    }
    
    // Draw grey border - unique 1px light grey border
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0xCC, 0xCC, 0xCC, (Uint8)(brightness * 180));
    
    SDL_Rect border = {x - w/2 - 1, y - h/2 - 1, w + 2, h + 2};
    SDL_RenderDrawRect(renderer, &border);
    
    if (absOffset < 0.1f) {
        // Selection highlight removed as requested
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
    renderTitle(romList[centerIdx].displayName, screenWidth / 2, screenHeight / 2 - 170);
    
    SDL_RenderPresent(renderer);
}
