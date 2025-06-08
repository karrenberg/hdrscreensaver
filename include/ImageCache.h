#pragma once
#include <memory>
#include <future>
#include <unordered_map>
#include <mutex>
#include <vector>
#include <string>
#include "LoadedImageTypes.h"

// Forward declarations
class LoadedImageTriple;

/**
 * Simple synchronous image loader for when caching is disabled
 */
class SimpleImageLoader
{
public:
    explicit SimpleImageLoader(const std::vector<std::wstring>& imageFiles);
    
    /**
     * Load an image synchronously by index
     * @param idx Index of the image to load
     * @return Shared pointer to loaded image triple, or nullptr if failed
     */
    std::shared_ptr<LoadedImageTriple> load_sync(size_t idx);

private:
    const std::vector<std::wstring>& imageFiles_;
};

/**
 * Asynchronous image cache with intelligent eviction and preloading
 * Supports forward/backward navigation with wraparound-aware caching
 */
class ImageCache
{
public:
    /**
     * Cache entry containing a future for async loading and size information
     */
    struct Entry
    {
        size_t index;
        std::shared_future<std::shared_ptr<LoadedImageTriple>> future;
        size_t bytes; // 0 if not loaded yet
    };

    /**
     * Constructor
     * @param maxBytes Maximum cache size in bytes
     * @param cachePrev Number of previous images to keep in cache
     * @param cacheNext Number of next images to keep in cache
     * @param imageFiles Reference to the list of image file paths
     */
    ImageCache(size_t maxBytes, int cachePrev, int cacheNext, const std::vector<std::wstring>& imageFiles);

    /**
     * Get an image asynchronously by index
     * @param idx Index of the image to load
     * @param isForward Whether this is a forward navigation request
     * @return Future that will contain the loaded image
     */
    std::shared_future<std::shared_ptr<LoadedImageTriple>> get_async(size_t idx, bool isForward = true);

    /**
     * Check if an image is fully loaded
     * @param idx Index of the image to check
     * @return True if the image is loaded and ready
     */
    bool is_loaded(size_t idx);

    /**
     * Preload images around the current index
     * @param imageFiles List of image file paths
     * @param currentIndex Current image index
     * @param isForward Whether we're moving forward
     */
    void fill(const std::vector<std::wstring>& imageFiles, int currentIndex, bool isForward = true);

    /**
     * Get current cache size in bytes
     * @return Number of bytes currently used by the cache
     */
    size_t cacheBytes() const;

    /**
     * Set the current index for cache management
     * @param idx Current image index
     */
    void set_current_index(size_t idx);

    /**
     * Check if an image is in cache or being loaded
     * @param idx Index of the image to check
     * @return True if the image is cached or loading
     */
    bool is_in_cache_or_inflight(size_t idx);

    /**
     * Check if an image would be evicted immediately if loaded
     * @param idx Index of the image to check
     * @param estimate Estimated size of the image
     * @param isForward Whether this is a forward navigation
     * @return True if the image would be evicted immediately
     */
    bool would_be_evicted_public(size_t idx, size_t estimate, bool isForward = true);

    /**
     * Get estimated image size for preloading decisions
     * @return Estimated size in bytes
     */
    size_t get_estimated_image_size();

    /**
     * Destructor - waits for all async operations to complete
     */
    ~ImageCache();

private:
    /**
     * Check if an index is within the protected window
     * @param idx Index to check
     * @return True if the index is in the window
     */
    bool in_window(size_t idx) const;

    /**
     * Simulate if adding an image would cause it to be evicted immediately
     * @param idx Index of the image to check
     * @param estimate Estimated size of the image
     * @param isForward Whether this is a forward navigation
     * @return True if the image would be evicted immediately
     */
    bool would_be_evicted(size_t idx, size_t estimate, bool isForward = true);

    /**
     * Evict images from cache if needed to stay under memory limit
     * @param isForward Whether we're moving forward
     */
    void evict_if_needed(bool isForward = true);

    // Cache configuration
    size_t maxBytes_;
    int cachePrev_, cacheNext_;
    size_t cacheBytes_;
    const std::vector<std::wstring>& imageFiles_;
    
    // Cache state
    std::unordered_map<size_t, Entry> cache_;
    mutable std::mutex mutex_;
    size_t currentIndex_ = 0; // Track current index for windowed eviction
    size_t reservedBytes_ = 0; // Track bytes reserved for in-flight preloads
}; 