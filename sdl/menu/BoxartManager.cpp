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
#include <dirent.h>

static const char* LIBRETRO_BASE_URL = "http://thumbnails.libretro.com/Nintendo%20-%20Super%20Nintendo%20Entertainment%20System/Named_Boxarts/";

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
    : renderer(nullptr), placeholderTexture(nullptr), blurRadius(2), stopWorker(false), libretroIndexLoaded(false) {
    const char* home = getenv("HOME");
    if (home) {
        boxartDir = std::string(home) + "/.snes9x/boxart";
    } else {
        boxartDir = ".snes9x/boxart";
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
    placeholderTexture = createPlaceholderTexture("Loading...");
}

void BoxartManager::startWorker() {
    if (workerThreads.empty()) {
        workerThreads.emplace_back(&BoxartManager::workerFunc, this);
    }
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
    
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        for (auto& pair : cache) {
            pair.second.destroy();
        }
        cache.clear();
    }
    
    if (placeholderTexture) {
        SDL_DestroyTexture(placeholderTexture);
        placeholderTexture = nullptr;
    }
    
    IMG_Quit();
}

void BoxartManager::ensureDirectoryExists() {
    const char* home_env = getenv("HOME");
    std::string home = home_env ? std::string(home_env) : "/root";
    
    std::string snes9xDir = home + "/.snes9x";
    mkdir(snes9xDir.c_str(), 0755);
    mkdir(boxartDir.c_str(), 0755);
}

std::string BoxartManager::getLocalPath(const std::string& romName) {
    return boxartDir + "/" + romName + ".png";
}

void BoxartManager::requestBoxart(const std::string& romName, const std::string& displayName, bool priority, bool isDownload) {
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        if (cache.count(romName)) {
            BoxartEntry& entry = cache[romName];
            entry.lastUsed = SDL_GetTicks();
            if (entry.loaded || entry.queued || entry.failed) {
                if (priority && entry.queued) {
                    std::lock_guard<std::mutex> qlock(queueMutex);
                    for (auto it = taskQueue.begin(); it != taskQueue.end(); ++it) {
                        if (it->romName == romName) {
                            BoxartTask task = *it;
                            task.isDownload = isDownload && task.isDownload;
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
        entry.lastUsed = SDL_GetTicks();
        cache[romName] = entry;
    }
    
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
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = cache.find(romName);
        if (it != cache.end()) {
            it->second.destroy();
            cache.erase(it);
        }
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
    // Initial delay removed for faster boot
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
        // Reduce background CPU load to prevent audio starvation on RPi Zero
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}

void BoxartManager::processTask(const BoxartTask& task) {
    std::string romBase = task.romName;
    size_t lastDot = romBase.find_last_of('.');
    if (lastDot != std::string::npos) {
        romBase = romBase.substr(0, lastDot);
    }

    std::vector<std::string> paths;
    paths.push_back(boxartDir + "/" + task.romName + ".png"); 
    paths.push_back(boxartDir + "/" + romBase + ".png");      
    
    std::string finalPath = "";
    for (const auto& p : paths) {
        struct stat st;
        if (stat(p.c_str(), &st) == 0) {
            finalPath = p;
            break;
        }
    }

    // If not found locally, try to download from libretro
    if (finalPath.empty()) {
        fprintf(stderr, "[Boxart] Not found locally: %s\n", romBase.c_str());
        fetchLibretroIndex();
        
        if (!libretroNames.empty()) {
            std::string normalizedRom = StringMatcher::normalize(romBase);
            std::string match = StringMatcher::findBestMatchFast(normalizedRom, libretroNames, normalizedLibretroNames);
            
            if (!match.empty()) {
                fprintf(stderr, "[Boxart] Matched '%s' -> '%s'\n", romBase.c_str(), match.c_str());
                if (downloadBoxart(task.romName, match)) {
                    finalPath = getLocalPath(task.romName);
                    fprintf(stderr, "[Boxart] Downloaded OK: %s\n", finalPath.c_str());
                }
            } else {
                fprintf(stderr, "[Boxart] No match found for: %s\n", romBase.c_str());
            }
        } else {
            fprintf(stderr, "[Boxart] Libretro index empty (fetch failed?)\n");
        }
    }

    BoxartResult result;
    result.romName = task.romName;
    result.success = false;
    result.isDisplay = !task.isDownload;
    
    if (!finalPath.empty() && !task.isDownload) {
          SDL_Surface* surface = loadImageSurface(finalPath);
          if (surface) {
              scaleToFit(surface, 800, 600);
              result.surface = surface;
              result.blurred = applyBoxBlur(surface, blurRadius);
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
        if (resultQueue.empty()) return;
        results = std::move(resultQueue);
        resultQueue.clear();
    }
    
    const size_t MAX_LOADED_TEXTURES = 64;
    const size_t MAX_TEXTURES_PER_POLL = 2; // Limit GPU uploads per frame
    size_t texturesCreated = 0;
    
    for (auto it = results.begin(); it != results.end(); ) {
        bool processed = false;
        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            
            if (cache.count(it->romName)) {
                BoxartEntry& entry = cache[it->romName];
                
                if (it->success && it->isDisplay) {
                    if (texturesCreated < MAX_TEXTURES_PER_POLL) {
                        // Eviction logic
                        size_t loadedCount = 0;
                        for (auto const& [name, e] : cache) { if (e.loaded) loadedCount++; }
                        
                        if (loadedCount >= MAX_LOADED_TEXTURES) {
                            std::string oldestName = "";
                            uint32_t oldestUsed = 0xFFFFFFFF;
                            for (auto const& [name, e] : cache) {
                                if (e.loaded && e.lastUsed < oldestUsed) {
                                    oldestUsed = e.lastUsed;
                                    oldestName = name;
                                }
                            }
                            if (!oldestName.empty()) {
                                cache[oldestName].destroy();
                                cache.erase(oldestName);
                            }
                        }

                        if (it->surface) {
                            entry.texture = SDL_CreateTextureFromSurface(renderer, it->surface);
                            if (entry.texture) {
                                SDL_SetTextureBlendMode(entry.texture, SDL_BLENDMODE_BLEND);
                                entry.loaded = true;
                                entry.localPath = getLocalPath(it->romName);
                            }
                        }
                        
                        if (it->blurred) {
                            entry.blurred = SDL_CreateTextureFromSurface(renderer, it->blurred);
                            if (entry.blurred) {
                                SDL_SetTextureBlendMode(entry.blurred, SDL_BLENDMODE_BLEND);
                            }
                        }
                        entry.queued = false;
                        texturesCreated++;
                        processed = true;
                    }
                } else {
                    entry.queued = false;
                    if (!it->success && it->isDisplay) entry.failed = true;
                    processed = true;
                }
            } else {
                processed = true; // ROM no longer in cache
            }
        }
        
        if (processed) {
            if (it->surface) SDL_FreeSurface(it->surface);
            if (it->blurred) SDL_FreeSurface(it->blurred);
            it = results.erase(it);
        } else {
            ++it;
        }
    }
    
    // If some results weren't processed (due to MAX_TEXTURES_PER_POLL), put them back
    if (!results.empty()) {
        std::lock_guard<std::mutex> lock(queueMutex);
        for (auto const& res : results) {
            resultQueue.push_front(res);
        }
    }
}

void BoxartManager::fetchLibretroIndex() {
    if (libretroIndexLoaded) return;
    
    fprintf(stderr, "[Boxart] Fetching libretro index from: %s\n", LIBRETRO_BASE_URL);
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "[Boxart] ERROR: curl_easy_init failed\n");
        return;
    }
    
    std::string html;
    curl_easy_setopt(curl, CURLOPT_URL, LIBRETRO_BASE_URL);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, stringWriteCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &html);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    
    CURLcode res = curl_easy_perform(curl);
    curl_easy_cleanup(curl);
    
    if (res == CURLE_OK) {
        size_t pos = 0;
        const std::string needle = ".png\"";
        const std::string hrefNeedle = "href=\"";
        
        while ((pos = html.find(needle, pos)) != std::string::npos) {
            size_t start = html.rfind(hrefNeedle, pos);
            if (start != std::string::npos && pos - start < 256) {
                std::string name = html.substr(start + hrefNeedle.length(), pos - start - hrefNeedle.length() + 4);
                std::string decodedName = StringMatcher::urlDecode(name);
                
                libretroNames.push_back(decodedName);
                normalizedLibretroNames.push_back(StringMatcher::normalize(decodedName));
            }
            pos += needle.length();
        }
        libretroIndexLoaded = true;
        fprintf(stderr, "[Boxart] Libretro index loaded: %zu entries\n", libretroNames.size());
    } else {
        fprintf(stderr, "[Boxart] ERROR: Failed to fetch index: %s\n", curl_easy_strerror(res));
    }
}

bool BoxartManager::downloadBoxart(const std::string& romName, const std::string& matchedName) {
    std::string localPath = getLocalPath(romName);
    std::string encodedName = StringMatcher::urlEncode(matchedName);
    std::string url = std::string(LIBRETRO_BASE_URL) + encodedName;
    
    fprintf(stderr, "[Boxart] Downloading: %s\n", url.c_str());
    
    CURL* curl = curl_easy_init();
    if (!curl) {
        fprintf(stderr, "[Boxart] ERROR: curl_easy_init failed\n");
        return false;
    }
    
    std::ofstream file(localPath, std::ios::binary);
    if (!file.is_open()) {
        fprintf(stderr, "[Boxart] ERROR: Cannot write to %s\n", localPath.c_str());
        curl_easy_cleanup(curl);
        return false;
    }
    
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, writeCallback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &file);
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 15L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
    
    CURLcode res = curl_easy_perform(curl);
    long httpCode = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &httpCode);
    
    file.close();
    curl_easy_cleanup(curl);
    
    if (res == CURLE_OK && httpCode == 200) {
        return true;
    }
    
    fprintf(stderr, "[Boxart] ERROR: Download failed - curl=%s, http=%ld\n", 
            curl_easy_strerror(res), httpCode);
    remove(localPath.c_str());
    return false;
}

SDL_Surface* BoxartManager::loadImageSurface(const std::string& path) {
    SDL_Surface* surface = IMG_Load(path.c_str());
    if (!surface) {
        return nullptr;
    }
    
    // Usar el formato que funcionó en el test
    SDL_Surface* converted = SDL_ConvertSurfaceFormat(surface, SDL_PIXELFORMAT_RGB24, 0);
    SDL_FreeSurface(surface);
    return converted;
}

void BoxartManager::scaleToFit(SDL_Surface*& surface, int maxW, int maxH) {
    if (!surface) return;
    
    float ratioW = (float)maxW / surface->w;
    float ratioH = (float)maxH / surface->h;
    float scale = std::min(ratioW, ratioH);
    
    // Don't upscale
    if (scale >= 1.0f) return;
    
    int targetW = (int)(surface->w * scale);
    int targetH = (int)(surface->h * scale);
    
    SDL_Surface* optimized = SDL_CreateRGBSurfaceWithFormat(0, targetW, targetH, 24, SDL_PIXELFORMAT_RGB24);
    if (!optimized) return;
    
    SDL_Rect srcRect = {0, 0, surface->w, surface->h};
    SDL_Rect dstRect = {0, 0, targetW, targetH};
    
    SDL_BlitScaled(surface, &srcRect, optimized, &dstRect);
    SDL_FreeSurface(surface);
    surface = optimized;
}

SDL_Surface* BoxartManager::applyBoxBlur(SDL_Surface* src, int radius) {
    if (!src || radius <= 0) return nullptr;
    
    SDL_Surface* dst = SDL_CreateRGBSurfaceWithFormat(0, src->w, src->h, 24, SDL_PIXELFORMAT_RGB24);
    if (!dst) return nullptr;

    int w = src->w;
    int h = src->h;
    int srcPitch = src->pitch;
    int dstPitch = dst->pitch;
    Uint8* srcPixels = (Uint8*)src->pixels;
    Uint8* dstPixels = (Uint8*)dst->pixels;

    for (int y = 0; y < h; y++) {
        for (int x = 0; x < w; x++) {
            int r = 0, g = 0, b = 0, count = 0;
            for (int ky = -radius; ky <= radius; ky++) {
                int py = y + ky;
                if (py < 0 || py >= h) continue;
                for (int kx = -radius; kx <= radius; kx++) {
                    int px = x + kx;
                    if (px < 0 || px >= w) continue;
                    
                    Uint8* p = &srcPixels[py * srcPitch + px * 3];
                    r += p[0];
                    g += p[1];
                    b += p[2];
                    count++;
                }
            }
            if (count > 0) {
                Uint8* p = &dstPixels[y * dstPitch + x * 3];
                p[0] = r / count;
                p[1] = g / count;
                p[2] = b / count;
            }
        }
    }
    return dst;
}

SDL_Texture* BoxartManager::createPlaceholderTexture(const std::string& name) {
    if (!renderer) return nullptr;
    SDL_Surface* s = SDL_CreateRGBSurfaceWithFormat(0, 16, 16, 24, SDL_PIXELFORMAT_RGB24);
    if (!s) return nullptr;
    SDL_FillRect(s, nullptr, SDL_MapRGB(s->format, 30, 30, 35)); // Gris oscuro discreto
    SDL_Texture* tex = SDL_CreateTextureFromSurface(renderer, s);
    if (tex) SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
    SDL_FreeSurface(s);
    return tex;
}

SDL_Texture* BoxartManager::getTexture(const std::string& romName, int blurLevel) {
    std::lock_guard<std::mutex> lock(cacheMutex);
    auto it = cache.find(romName);
    if (it == cache.end() || !it->second.loaded) return placeholderTexture;
    
    it->second.lastUsed = SDL_GetTicks();
    if (blurLevel > 0 && it->second.blurred) return it->second.blurred;
    return it->second.texture;
}

SDL_Texture* BoxartManager::getReflectionTexture(const std::string& romName) {
    return nullptr;
}
