#include "ImageCache.h"
#include "SkiaImageLoader.h"
#include "GdiPlusImageLoader.h"
#include "Logger.h"
#include <algorithm>
#include <chrono>

#define USE_SKIA

// --- SimpleImageLoader implementation ---

SimpleImageLoader::SimpleImageLoader(const std::vector<std::wstring>& imageFiles) 
    : imageFiles_(imageFiles) {}

std::shared_ptr<LoadedImageTriple> SimpleImageLoader::load_sync(size_t idx) {
    if (idx >= imageFiles_.size()) {
        LOG_MSG(L"Invalid image index: " + std::to_wstring(idx));
        return nullptr;
    }

    std::wstring path = imageFiles_[idx];
    LOG_MSG(L"Synchronously loading image " + std::to_wstring(idx+1) + L": " + path);

#ifdef USE_SKIA
    return std::make_shared<LoadedImageTriple>(LoadImageWithSkia(path));
#else
    return std::make_shared<LoadedImageTriple>(LoadImageWithGdiPlus(path));
#endif
}

// --- ImageCache implementation ---

ImageCache::ImageCache(size_t maxBytes, int cachePrev, int cacheNext, const std::vector<std::wstring>& imageFiles)
    : maxBytes_(maxBytes), cachePrev_(cachePrev), cacheNext_(cacheNext), cacheBytes_(0), imageFiles_(imageFiles)
{}

std::shared_future<std::shared_ptr<LoadedImageTriple>> ImageCache::get_async(size_t idx, bool isForward) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(idx);
    if (it != cache_.end()) {
        return it->second.future;
    }

    // Start async load
    size_t estimate = 500 * 1024 * 1024;
    for (const auto& kv : cache_) {
        if (kv.second.bytes > 0) {
            estimate = kv.second.bytes;
            break;
        }
    }

    // Prevent launching if it would be evicted immediately
    if (would_be_evicted(idx, estimate, isForward)) {
        LOG_MSG(L"Skipping loading of image " + std::to_wstring(idx+1) + L"; would be evicted immediately.");
        // Return a dummy future that never becomes ready
        std::promise<std::shared_ptr<LoadedImageTriple>> p;
        return p.get_future().share();
    }

    auto fut = std::async(std::launch::async, [this, idx, estimate, isForward] {
        std::wstring path = imageFiles_[idx];
        LOG_MSG(L"Asynchronously loading image " + std::to_wstring(idx+1) + L": " + path);
        #ifdef USE_SKIA
        const auto triple = std::make_shared<LoadedImageTriple>(LoadImageWithSkia(path));
        #else
        const auto triple = std::make_shared<LoadedImageTriple>(LoadImageWithGdiPlus(path));
        #endif
        const size_t sizeInBytes = triple->sizeInBytes();
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = cache_.find(idx);
            if (it != cache_.end()) {
                it->second.bytes = sizeInBytes;
                reservedBytes_ -= estimate;
                cacheBytes_ += sizeInBytes;
                evict_if_needed(isForward);
            }
        }
        return triple;
    });

    auto shared_fut = fut.share();
    Entry entry{idx, shared_fut, 0};
    cache_[idx] = entry;
    return shared_fut;
}

bool ImageCache::is_loaded(size_t idx) {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = cache_.find(idx);
    if (it == cache_.end()) return false;
    return it->second.future.wait_for(std::chrono::milliseconds(0)) == std::future_status::ready;
}

void ImageCache::fill(const std::vector<std::wstring>& imageFiles, int currentIndex, bool isForward) {
    size_t estimate = 200 * 1024 * 1024;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (const auto& kv : cache_) {
            if (kv.second.bytes > 0) {
                estimate = kv.second.bytes;
                break;
            }
        }
    }
    const size_t numImages = imageFiles.size();
    // Preload kCachePrev before and kCacheNext after currentIndex, with wraparound
    int loaded = 0;
    // Preload previous images (wraparound)
    for (int i = 1; i <= cachePrev_; ++i) {
        size_t idx = (currentIndex + numImages - i) % numImages;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (cache_.find(idx) != cache_.end()) continue;
            if (cacheBytes_ + reservedBytes_ + estimate > maxBytes_) break;
            if (would_be_evicted(idx, estimate, false)) continue;
            reservedBytes_ += estimate;
        }
        get_async(idx, false);
        ++loaded;
    }
    // Preload next images (wraparound)
    for (int i = 1; i <= cacheNext_; ++i) {
        size_t idx = (currentIndex + i) % numImages;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (cache_.find(idx) != cache_.end()) continue;
            if (cacheBytes_ + reservedBytes_ + estimate > maxBytes_) break;
            if (would_be_evicted(idx, estimate, true)) continue;
            reservedBytes_ += estimate;
        }
        get_async(idx, true);
        ++loaded;
    }
    LOG_MSG(L"Preloaded " + std::to_wstring(loaded) + L" images into cache (wraparound aware).");
}

size_t ImageCache::cacheBytes() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return cacheBytes_;
}

void ImageCache::set_current_index(size_t idx) {
    std::lock_guard<std::mutex> lock(mutex_);
    currentIndex_ = idx;
}

bool ImageCache::is_in_cache_or_inflight(size_t idx) {
    std::lock_guard<std::mutex> lock(mutex_);
    return cache_.find(idx) != cache_.end();
}

bool ImageCache::would_be_evicted_public(size_t idx, size_t estimate, bool isForward) {
    std::lock_guard<std::mutex> lock(mutex_);
    return would_be_evicted(idx, estimate, isForward);
}

size_t ImageCache::get_estimated_image_size() {
    std::lock_guard<std::mutex> lock(mutex_);
    size_t estimate = 200 * 1024 * 1024;
    for (const auto& kv : cache_) {
        if (kv.second.bytes > 0) {
            estimate = kv.second.bytes;
            break;
        }
    }
    return estimate;
}

ImageCache::~ImageCache() {
    std::vector<std::shared_future<std::shared_ptr<LoadedImageTriple>>> futures;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto& kv : cache_) {
            if (kv.second.future.valid()) {
                futures.push_back(kv.second.future);
            }
        }
    }
    for (auto& fut : futures) {
        try {
            fut.wait();
        } catch (...) {
            // Ignore exceptions during shutdown
        }
    }
}

bool ImageCache::in_window(size_t idx) const {
    if (imageFiles_.empty()) return false;
    size_t numImages = imageFiles_.size();
    size_t start = (currentIndex_ + numImages - cachePrev_) % numImages;
    size_t end = (currentIndex_ + cacheNext_) % numImages;
    if (start <= end) {
        return idx >= start && idx <= end;
    } else {
        // Window wraps around zero
        return idx >= start || idx <= end;
    }
}

bool ImageCache::would_be_evicted(size_t idx, size_t estimate, bool isForward) {
    // If the image is in the window, always allow preloading
    if (in_window(idx)) return false;
    // Simulate cache state after adding this image
    struct SimEntry { size_t idx; size_t bytes; bool in_window; };
    std::vector<SimEntry> entries;
    size_t simBytes = cacheBytes_ + reservedBytes_ + estimate;
    for (const auto& kv : cache_) {
        if (kv.first == idx) continue; // skip if already present
        entries.push_back({kv.first, kv.second.bytes > 0 ? kv.second.bytes : estimate, in_window(kv.first)});
    }
    entries.push_back({idx, estimate, in_window(idx)});
    // Do not evict indices in the direction of movement (including wraparound)
    auto is_protected = [&](size_t candidate) {
        size_t numImages = imageFiles_.size();
        if (isForward) {
            for (int i = 1; i <= cacheNext_; ++i) {
                if (((currentIndex_ + i) % numImages) == candidate) return true;
            }
        } else {
            for (int i = 1; i <= cachePrev_; ++i) {
                if (((currentIndex_ + numImages - i) % numImages) == candidate) return true;
            }
        }
        return false;
    };
    while (simBytes > maxBytes_ && !entries.empty()) {
        // Find evictable entry furthest from window and not protected
        size_t numImages = imageFiles_.size();
        size_t maxDist = 0; int evictIdx = -1;
        for (size_t i = 0; i < entries.size(); ++i) {
            if (is_protected(entries[i].idx) || entries[i].in_window) continue;
            size_t dist;
            if (isForward) {
                dist = (currentIndex_ + numImages - entries[i].idx) % numImages;
            } else {
                dist = (entries[i].idx + numImages - currentIndex_) % numImages;
            }
            if (evictIdx == -1 || dist > maxDist) {
                maxDist = dist; evictIdx = (int)i;
            }
        }
        if (evictIdx == -1) break;
        simBytes -= entries[evictIdx].bytes;
        if (entries[evictIdx].idx == idx) return true;
        entries.erase(entries.begin() + evictIdx);
    }
    return false;
}

void ImageCache::evict_if_needed(bool isForward) {
    // Direction-aware eviction: evict the entry with the largest modular distance in the opposite direction of travel
    while (cacheBytes_ > maxBytes_ && !cache_.empty()) {
        size_t numImages = imageFiles_.size();
        size_t evictIdx = size_t(-1);
        size_t maxDist = 0;
        bool found = false;
        for (const auto& kv : cache_) {
            size_t idx = kv.first;
            if (in_window(idx)) continue; // Never evict from window
            size_t dist;
            if (isForward) {
                dist = (currentIndex_ + numImages - idx) % numImages;
            } else {
                dist = (idx + numImages - currentIndex_) % numImages;
            }
            if (!found || dist > maxDist) {
                maxDist = dist;
                evictIdx = idx;
                found = true;
            }
        }
        if (!found || evictIdx == size_t(-1)) {
            evictIdx = cache_.begin()->first;
        }
        auto mapIt = cache_.find(evictIdx);
        if (mapIt != cache_.end() && mapIt->second.bytes > 0) {
            cacheBytes_ -= mapIt->second.bytes;
            LOG_MSG(L"Evicting image " + std::to_wstring(evictIdx + 1) + L" from cache (" + std::to_wstring(mapIt->second.bytes) + L" bytes). New/max cache size: " + std::to_wstring(cacheBytes_) + L"/" + std::to_wstring(maxBytes_) + L" bytes");
            cache_.erase(mapIt);
        } else {
            cache_.erase(evictIdx);
        }
    }
} 