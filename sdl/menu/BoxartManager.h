#ifndef BOXART_MANAGER_H
#define BOXART_MANAGER_H

#include <SDL2/SDL.h>
#include <string>
#include <map>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>

struct BoxartEntry {
    SDL_Texture* texture;
    SDL_Texture* blurred;
    std::string localPath;
    bool loaded;
    bool queued;
    
    BoxartEntry() : texture(nullptr), blurred(nullptr), loaded(false), queued(false) {}
    
    void destroy() {
        if (texture) { SDL_DestroyTexture(texture); texture = nullptr; }
        if (blurred) { SDL_DestroyTexture(blurred); blurred = nullptr; }
        loaded = false;
    }
};

struct BoxartTask {
    std::string romName;
    std::string displayName;
    bool isDownload;
};

struct BoxartResult {
    std::string romName;
    SDL_Surface* surface;
    SDL_Surface* blurred;
    bool success;
    bool isDisplay;
    
    BoxartResult() : surface(nullptr), blurred(nullptr), success(false), isDisplay(false) {}
};

class BoxartManager {
public:
    BoxartManager();
    ~BoxartManager();
    
    void init(SDL_Renderer* renderer);
    void shutdown();
    
    void setBlurRadius(int radius) { blurRadius = radius; }
    
    void requestBoxart(const std::string& romName, const std::string& displayName, bool priority = false, bool isSyncOnly = false);
    void unloadBoxart(const std::string& romName);
    void pollResults();
    
    SDL_Texture* getTexture(const std::string& romName, int blurLevel = 0);
    SDL_Texture* getReflectionTexture(const std::string& romName);
    
    void fetchLibretroIndex();
    std::string getBoxartDir() const { return boxartDir; }
    
private:
    SDL_Renderer* renderer;
    std::string boxartDir;
    std::map<std::string, BoxartEntry> cache;
    std::vector<std::string> libretroNames;
    std::mutex indexMutex; // Mutex for libretroNames
    SDL_Texture* placeholderTexture;
    
    int blurRadius;
    std::vector<std::thread> workerThreads;
    std::mutex queueMutex;
    std::condition_variable condition;
    std::deque<BoxartTask> taskQueue;
    std::deque<BoxartResult> resultQueue;
    bool stopWorker;
    bool libretroIndexLoaded;

    void workerFunc();
    void processTask(const BoxartTask& task);
    
    bool downloadBoxart(const std::string& romName, const std::string& matchedName);
    void ensureDirectoryExists();
    std::string getLocalPath(const std::string& romName);
    SDL_Surface* loadImageSurface(const std::string& path);
    SDL_Texture* createPlaceholderTexture(const std::string& name);
    void cropAndScale(SDL_Surface*& surface, int targetW, int targetH);
    SDL_Surface* applyBoxBlur(SDL_Surface* src, int radius);
};

#endif
