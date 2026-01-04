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
    workerThread = std::thread(&BoxartManager::workerFunc, this);
    
    placeholderTexture = createPlaceholderTexture("Loading...");
}

void BoxartManager::shutdown() {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        stopWorker = true;
    }
    condition.notify_all();
    if (workerThread.joinable()) {
        workerThread.join();
    }
    
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

void BoxartManager::requestBoxart(const std::string& romName, const std::string& displayName) {
    if (cache.count(romName)) return;
    
    BoxartEntry entry;
    entry.loaded = false;
    cache[romName] = entry;
    
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        taskQueue.push_back({romName, displayName, false});
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
    
    if (!exists) {
        if (!libretroIndexLoaded) {
            fetchLibretroIndex();
        }
        
        std::string matched = StringMatcher::findBestMatch(task.romName, libretroNames);
        if (!matched.empty()) {
            printf("BoxartManager: Disk miss for '%s'. Downloading best match: '%s'\n", task.romName.c_str(), matched.c_str());
            if (downloadBoxart(task.romName, matched)) {
                exists = true;
            }
        }
    } else {
        printf("BoxartManager: Disk hit for '%s'.\n", task.romName.c_str());
    }
    
    BoxartResult result;
    result.romName = task.romName;
    result.success = false;
    
    if (exists) {
        SDL_Surface* surface = loadImageSurface(localPath);
        if (surface) {
            cropToAspectRatio(surface, BOXART_ASPECT_RATIO);
            result.surface = surface;
            
            // Generate blurs and reflection in background thread to avoid UI lag
            for (int i = 0; i < 4; i++) {
                result.blurred[i] = applyBoxBlur(surface, i + 1);
            }
            result.reflection = createReflectionSurface(surface);
            result.success = true;
        }
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
            if (res.success && res.surface) {
                BoxartEntry& entry = cache[res.romName];
                entry.texture = SDL_CreateTextureFromSurface(renderer, res.surface);
                SDL_SetTextureBlendMode(entry.texture, SDL_BLENDMODE_BLEND);
                
                for (int i = 0; i < 4; i++) {
                    if (res.blurred[i]) {
                        entry.blurred[i] = SDL_CreateTextureFromSurface(renderer, res.blurred[i]);
                        SDL_SetTextureBlendMode(entry.blurred[i], SDL_BLENDMODE_BLEND);
                        SDL_FreeSurface(res.blurred[i]);
                    }
                }
                
                if (res.reflection) {
                    entry.reflection = SDL_CreateTextureFromSurface(renderer, res.reflection);
                    SDL_SetTextureBlendMode(entry.reflection, SDL_BLENDMODE_BLEND);
                    SDL_FreeSurface(res.reflection);
                }
                
                entry.loaded = true;
                entry.localPath = getLocalPath(res.romName);
                
                SDL_FreeSurface(res.surface);
                printf("BoxartManager: Async load complete for '%s'.\n", res.romName.c_str());
            } else {
                printf("BoxartManager: Async load failed for '%s'.\n", res.romName.c_str());
            }
        } else {
            // Clean up if the entry was removed from cache while processing
            if (res.surface) SDL_FreeSurface(res.surface);
            for (int i = 0; i < 4; i++) if (res.blurred[i]) SDL_FreeSurface(res.blurred[i]);
            if (res.reflection) SDL_FreeSurface(res.reflection);
        }
    }
}

void BoxartManager::fetchLibretroIndex() {
    if (libretroIndexLoaded) return;
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
    SDL_Surface* converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGBA8888, 0);
    SDL_FreeSurface(surface);
    return converted;
}

void BoxartManager::cropToAspectRatio(SDL_Surface*& surface, float targetRatio) {
    if (!surface) return;
    float currentRatio = (float)surface->w / (float)surface->h;
    int newW, newH, srcX = 0, srcY = 0;
    if (currentRatio > targetRatio) {
        newH = surface->h;
        newW = (int)(newH * targetRatio);
        srcX = (surface->w - newW) / 2;
    } else {
        newW = surface->w;
        newH = (int)(newW / targetRatio);
        srcY = (surface->h - newH) / 2;
    }
    SDL_Surface* cropped = SDL_CreateRGBSurfaceWithFormat(0, newW, newH, 32, SDL_PIXELFORMAT_RGBA8888);
    if (!cropped) return;
    SDL_Rect srcRect = {srcX, srcY, newW, newH};
    SDL_BlitSurface(surface, &srcRect, cropped, nullptr);
    SDL_FreeSurface(surface);
    surface = cropped;
}

SDL_Surface* BoxartManager::applyBoxBlur(SDL_Surface* src, int radius) {
    if (!src || radius <= 0) return nullptr;
    SDL_Surface* dst = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 32, SDL_PIXELFORMAT_RGBA8888);
    if (!dst) return nullptr;
    Uint32* srcPixels = (Uint32*)src->pixels;
    Uint32* dstPixels = (Uint32*)dst->pixels;
    int w = src->w;
    int h = src->h;
    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int r = 0, g = 0, b = 0, a = 0, count = 0;
            for (int ky = -radius; ky <= radius; ky++) {
                for (int kx = -radius; kx <= radius; kx++) {
                    int px = std::max(0, std::min(w - 1, x + kx));
                    int py = std::max(0, std::min(h - 1, y + ky));
                    Uint32 pixel = srcPixels[py * w + px];
                    r += (pixel >> 24) & 0xFF;
                    g += (pixel >> 16) & 0xFF;
                    b += (pixel >> 8) & 0xFF;
                    a += pixel & 0xFF;
                    count++;
                }
            }
            dstPixels[y * w + x] = ((r/count) << 24) | ((g/count) << 16) | ((b/count) << 8) | (a/count);
        }
    }
    return dst;
}

SDL_Surface* BoxartManager::createReflectionSurface(SDL_Surface* original) {
    if (!original) return nullptr;
    SDL_Surface* reflection = SDL_CreateRGBSurfaceWithFormat(0, original->w, original->h, 32, SDL_PIXELFORMAT_RGBA8888);
    if (!reflection) return nullptr;
    SDL_BlitSurface(original, nullptr, reflection, nullptr);
    
    Uint32* pixels = (Uint32*)reflection->pixels;
    for (int y = 0; y < reflection->h; y++) {
        float f = (float)y / (float)reflection->h;
        Uint8 alpha = (Uint8)(f * f * 150);
        for (int x = 0; x < reflection->w; x++) {
            pixels[y * reflection->w + x] = (pixels[y * reflection->w + x] & 0xFFFFFF00) | alpha;
        }
    }
    
    SDL_Surface* blurred = applyBoxBlur(reflection, 1);
    SDL_FreeSurface(reflection);
    return blurred;
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
    if (blurLevel <= 0) return it->second.texture;
    int idx = std::min(blurLevel - 1, 3);
    return it->second.blurred[idx] ? it->second.blurred[idx] : it->second.texture;
}

SDL_Texture* BoxartManager::getReflectionTexture(const std::string& romName) {
    auto it = cache.find(romName);
    if (it == cache.end() || !it->second.loaded) return nullptr;
    return it->second.reflection;
}
