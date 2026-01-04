#include "BoxartManager.h"
#include "StringMatcher.h"
#include <SDL2/SDL_image.h>
#include <curl/curl.h>
#include <sys/stat.h>
#include <cstdlib>
#include <cstring>
#include <algorithm>
#include <fstream>
#include <regex>

static const char* LIBRETRO_BASE_URL = "https://thumbnails.libretro.com/Nintendo%20-%20Super%20Nintendo%20Entertainment%20System/Named_Boxarts/";
static const float BOXART_ASPECT_RATIO = 512.0f / 357.0f;

static size_t writeCallback(void* contents, size_t size, size_t nmemb, void* userp) {
    std::ofstream* file = static_cast<std::ofstream*>(userp);
    size_t totalSize = size * nmemb;
    file->write(static_cast<char*>(contents), totalSize);
    return totalSize;
}

static size_t stringWriteCallback(void* ptr, size_t size, size_t nmemb, void* userdata) {
    ((std::string*)userdata)->append((char*)ptr, size * nmemb);
    return size * nmemb;
}

BoxartManager::BoxartManager() 
    : renderer(nullptr), placeholderTexture(nullptr), stopWorker(false), libretroIndexLoaded(false) {
    const char* home = getenv("HOME");
    if (home) {
        boxartDir = std::string(home) + "/.snes9x/boxart";
    }
}

BoxartManager::~BoxartManager() {
    shutdown();
}

void BoxartManager::init(SDL_Renderer* r) {
    renderer = r;
    ensureDirectoryExists();
    IMG_Init(IMG_INIT_PNG | IMG_INIT_JPG);
    
    stopWorker = false;
    for (int i = 0; i < 2; i++) {
        workerThreads.emplace_back(&BoxartManager::workerFunc, this);
    }
    
    placeholderTexture = createPlaceholderTexture("Loading...");
}

void BoxartManager::shutdown() {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        stopWorker = true;
    }
    condition.notify_all();
    for (auto& t : workerThreads) {
        if (t.joinable()) t.join();
    }
    workerThreads.clear();
    
    for (auto& pair : cache) {
        pair.second.destroy();
    }
    cache.clear();
    
    if (placeholderTexture) {
        SDL_DestroyTexture(placeholderTexture);
        placeholderTexture = nullptr;
    }
    
    IMG_Quit();
}

void BoxartManager::ensureDirectoryExists() {
    const char* home = getenv("HOME");
    if (!home) return;
    
    std::string snes9xDir = std::string(home) + "/.snes9x";
    mkdir(snes9xDir.c_str(), 0755);
    mkdir(boxartDir.c_str(), 0755);
}

std::string BoxartManager::getLocalPath(const std::string& romName) {
    return boxartDir + "/" + romName + ".png";
}

void BoxartManager::requestBoxart(const std::string& romName, const std::string& displayName, bool priority, bool isDownload) {
    if (cache.count(romName)) {
        if (cache[romName].loaded || cache[romName].queued) {
            // If it was only a download-sync request but now we need it for display
            if (!isDownload && cache[romName].queued) {
                // We need to re-queue it as a display request if it's currently only a sync request
                // But simpler is to let it finish and then request again if needed.
                // For now, if it's already in memory or queued, we don't duplicate.
                // However, if we need it for display and it's ONLY queued for download, we should upgrade it.
            }
            
            if (priority && cache[romName].queued) {
                std::lock_guard<std::mutex> lock(queueMutex);
                for (auto it = taskQueue.begin(); it != taskQueue.end(); ++it) {
                    if (it->romName == romName) {
                        BoxartTask task = *it;
                        task.isDownload = isDownload && task.isDownload; // Upgrade to display if either is false
                        taskQueue.erase(it);
                        taskQueue.push_front(task);
                        break;
                    }
                }
            }
            return;
        }
    }
    
    BoxartEntry entry;
    entry.loaded = false;
    entry.queued = true;
    cache[romName] = entry;
    
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        if (priority) {
            taskQueue.push_front({romName, displayName, isDownload});
        } else {
            taskQueue.push_back({romName, displayName, isDownload});
        }
    }
    condition.notify_one();
}

void BoxartManager::unloadBoxart(const std::string& romName) {
    auto it = cache.find(romName);
    if (it != cache.end()) {
        it->second.destroy();
        cache.erase(it);
    }
    std::lock_guard<std::mutex> lock(queueMutex);
    for (auto bit = taskQueue.begin(); bit != taskQueue.end(); ) {
        if (bit->romName == romName) {
            bit = taskQueue.erase(bit);
        } else {
            ++bit;
        }
    }
}

void BoxartManager::workerFunc() {
    while (true) {
        BoxartTask task;
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            condition.wait(lock, [this] { return stopWorker || !taskQueue.empty(); });
            if (stopWorker && taskQueue.empty()) break;
            task = taskQueue.front();
            taskQueue.pop_front();
        }
        processTask(task);
    }
}

void BoxartManager::processTask(const BoxartTask& task) {
    std::string localPath = getLocalPath(task.romName);
    struct stat st;
    bool exists = (stat(localPath.c_str(), &st) == 0);
    
    // Safety check for empty or tiny files (failed/interrupted downloads)
    if (exists && st.st_size < 100) {
        remove(localPath.c_str());
        exists = false;
    }

    if (!exists) {
        {
            std::lock_guard<std::mutex> lock(indexMutex);
            if (!libretroIndexLoaded) {
                fetchLibretroIndex();
            }
        }
        
        std::string matched;
        {
            std::lock_guard<std::mutex> lock(indexMutex);
            matched = StringMatcher::findBestMatch(task.romName, libretroNames);
        }

        if (!matched.empty()) {
            if (downloadBoxart(task.romName, matched)) {
                exists = true;
            }
        }
    }
    
    BoxartResult result;
    result.romName = task.romName;
    result.success = false;
    result.isDisplay = !task.isDownload;
    
    // Only load and process surfaces if this is a display request
    if (exists && !task.isDownload) {
        SDL_Surface* surface = loadImageSurface(localPath);
        if (surface) {
            cropAndScale(surface, 256, 178); // Optimized size
            result.surface = surface;
            result.success = true;
        }
    } else if (exists && task.isDownload) {
        result.success = true; // Sync success
    }
    
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        resultQueue.push_back(result);
    }
}

void BoxartManager::pollResults() {
    std::deque<BoxartResult> results;
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        results = std::move(resultQueue);
        resultQueue.clear();
    }
    
    for (auto& res : results) {
        if (cache.count(res.romName)) {
            BoxartEntry& entry = cache[res.romName];
            entry.queued = false;
            
            if (res.success && res.isDisplay && res.surface) {
                entry.texture = SDL_CreateTextureFromSurface(renderer, res.surface);
                // No more NONE blend mode, let's keep it standard for now but optimized pixel format
                
                entry.loaded = true;
                entry.localPath = getLocalPath(res.romName);
                
                SDL_FreeSurface(res.surface);
            } else if (res.success && !res.isDisplay) {
                // Sync only, don't mark as loaded, just clear queued flag
                // printf("BoxartManager: Sync complete for '%s'.\n", res.romName.c_str());
            } else {
                if (res.isDisplay) printf("BoxartManager: Display load failed for '%s'.\n", res.romName.c_str());
            }
        } else {
            // Clean up if the entry was removed from cache while processing
            if (res.surface) SDL_FreeSurface(res.surface);
        }
    }
}

void BoxartManager::fetchLibretroIndex() {
    if (libretroIndexLoaded) return;
    
    // Note: Caller must hold indexMutex
    printf("BoxartManager: Scraping Libretro index for boxarts...\n");
    CURL* curl = curl_easy_init();
    if (!curl) return;
    
    std::string html;
    curl_easy_setopt(curl, CURLOPT_URL, LIBRETRO_BASE_URL);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stringWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &html);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res == CURLE_OK) {
        size_t pos = 0;
        while ((pos = html.find(".png\"", pos)) != std::string::npos) {
            size_t start = html.rfind("href=\"", pos);
            if (start != std::string::npos && pos - start < 256) {
                // Extract filename including .png (pos is at '.', pos+4 is after 'g')
                std::string name = html.substr(start + 6, pos - start - 6 + 4);
                libretroNames.push_back(StringMatcher::urlDecode(name));
            }
            pos += 5;
        }
        printf("BoxartManager: Indexed %zu available images from Libretro.\n", libretroNames.size());
        libretroIndexLoaded = true;
    }
}

bool BoxartManager::downloadBoxart(const std::string& romName, const std::string& matchedName) {
    std::string localPath = getLocalPath(romName);
    std::string encodedName = StringMatcher::urlEncode(matchedName);
    std::string url = std::string(LIBRETRO_BASE_URL) + encodedName;
    
    printf("BoxartManager: Downloading '%s' -> '%s.png'\n", matchedName.c_str(), romName.c_str());
    
    CURL* curl = curl_easy_init();
    if (!curl) return false;
    
    std::ofstream file(localPath, std::ios::binary);
    if (!file.is_open()) return false;
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 20L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    
    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    
    file.close();
    curl_easy_cleanup(curl);
    
    if (res == CURLE_OK && httpCode == 200) {
        printf("BoxartManager: Download successful.\n");
        return true;
    }
    remove(localPath.c_str());
    return false;
}

SDL_Surface* BoxartManager::loadImageSurface(const std::string& path) {
    SDL_Surface* surface = IMG_Load(path.c_str());
    if (!surface) return nullptr;
    // Optimized pixel format for RPi Zero
    SDL_Surface* converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGB565, 0);
    SDL_FreeSurface(surface);
    return converted;
}

void BoxartManager::cropAndScale(SDL_Surface*& surface, int targetW, int targetH) {
    if (!surface) return;
    
    float targetRatio = (float)targetW / (float)targetH;
    float currentRatio = (float)surface->w / (float)surface->h;
    
    int srcX = 0, srcY = 0, srcW = surface->w, srcH = surface->h;
    
    if (currentRatio > targetRatio) {
        srcW = (int)(surface->h * targetRatio);
        srcX = (surface->w - srcW) / 2;
    } else {
        srcH = (int)(surface->w / targetRatio);
        srcY = (surface->h - srcH) / 2;
    }
    
    SDL_Surface* optimized = SDL_CreateRGBSurfaceWithFormat(0, targetW, targetH, 16, SDL_PIXELFORMAT_RGB565);
    if (!optimized) return;
    
    SDL_Rect srcRect = {srcX, srcY, srcW, srcH};
    SDL_Rect dstRect = {0, 0, targetW, targetH};
    
    SDL_BlitScaled(surface, &srcRect, optimized, &dstRect);
    SDL_FreeSurface(surface);
    surface = optimized;
}

SDL_Texture* BoxartManager::createPlaceholderTexture(const std::string& name) {
    if (!renderer) return nullptr;
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, 512, 357, 32, SDL_PIXELFORMAT_RGBA8888);
    SDL_FillRect(s, nullptr, SDL_MapRGBA(s->format, 40, 40, 50, 255));
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, s);
    SDL_FreeSurface(s);
    return tex;
}

SDL_Texture* BoxartManager::getTexture(const std::string& romName, int blurLevel) {
    auto it = cache.find(romName);
    if (it == cache.end() || !it->second.loaded) return placeholderTexture;
    return it->second.texture;
}

SDL_Texture* BoxartManager::getReflectionTexture(const std::string& romName) {
    return nullptr;
}
