#ifndef MENU_CAROUSEL_H
#define MENU_CAROUSEL_H

#include <SDL2/SDL.h>
#include <string>
#include <vector>
#include "BoxartManager.h"
#include "SpringAnimation.h"

struct RomEntry {
    std::string filename;
    std::string fullPath;
    std::string displayName;
    bool boxartLoaded;
};

class MenuCarousel {
public:
    static const int CARD_WIDTH = 600;
    static const int CARD_HEIGHT = 420;
    static const int GAP = 120;
    static const int VISIBLE_RANGE = 2; // Selection + 2 items on each side = 5 total visible

    MenuCarousel();
    ~MenuCarousel();

    void init(SDL_Renderer* renderer, int width, int height);
    void shutdown();
    
    void setLibretroNames(const std::vector<std::string>& names);
    void scanRomDirectory(const std::string& romDir);
    
    void update(float deltaTime);
    void render();
    
    void moveLeft();
    void moveRight();
    void moveUp();
    void moveDown();
    
    int getSelectedIndex() const;
    std::string getSelectedRomPath() const;
    std::string getSelectedRomName() const;
    
    bool hasRoms() const { return !romList.empty(); }
    void saveState();
    void loadState();

private:
    SDL_Renderer* renderer;
    int screenWidth;
    int screenHeight;
    
    BoxartManager boxartManager;
    std::vector<RomEntry> romList;
    int activeIndex;
    SpringAnimation animation;
    uint32_t lastFrameTime;

    SDL_Texture* backgroundGradient;
    SDL_Texture* reflectionOverlay;

    // Title animation
    std::string currentTitle;
    float titleAlpha; // 0.0 to 1.0

    // Configurable visual parameters
    float reflectionOpacity;  // Base transparency for reflections (0.0 to 1.0)
    int blurRadius;           // Radius for box blur on side cards and reflections
    float minSideBrightness;  // Minimum brightness for far cards

    void createStaticTextures();
    int wrap(int min, int max, int value) const;
    
    float calculateScale(float absOffset) const;
    float calculateBrightness(float absOffset) const;
    int calculateBlurLevel(float absOffset) const;
    
    void loadVisibleBoxarts();
    void renderBackground();
    void renderCard(int offset, const RomEntry& rom, float animPos);
    void renderReflection(int x, int y, int w, int h, const std::string& romName, float opacity);
    void renderTitle(const std::string& title, int x, int y);
    void renderText(const std::string& text, int x, int y, SDL_Color color, float scale = 1.0f);
    void renderNoRomsMessage();
};

#endif
