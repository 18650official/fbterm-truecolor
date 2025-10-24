#include "emojicache.h"
#include <stdio.h> 
#include <string.h>
#include <cstddef>     // !! 修复 C++98: 为了 NULL !!
#include <utility>     // !! 修复 C++98: 为了 std::make_pair !!

EmojiCache::EmojiCache(size_t max_size, int width, int height)
    : m_max_size(max_size), m_emoji_width(width), m_emoji_height(height) 
{
    // 构造函数体，目前为空
}

EmojiCache::~EmojiCache()
{
}

// 私有辅助函数：从磁盘加载
bool EmojiCache::loadEmojiFromDisk(u32 code, std::vector<unsigned char>& data)
{
    char bitmap_path[256];
    sprintf(bitmap_path, "/usr/share/emoji/%X.rgb", code);

    FILE* fp = fopen(bitmap_path, "rb");
    if (!fp) {
        return false; // 文件不存在
    }

    size_t bytes_to_read = m_emoji_width * m_emoji_height * 3;
    data.resize(bytes_to_read); // 调整 vector 大小以容纳数据

    size_t bytes_read = fread(data.data(), 1, bytes_to_read, fp);
    fclose(fp);

    if (bytes_read != bytes_to_read) {
        data.clear(); // 读取不完整，清空数据
        return false; // 文件大小不匹配
    }

    return true; // 加载成功
}

// 公共 'get' 函数 (C++98 版本)
const unsigned char* EmojiCache::get(u32 code)
{
    // C++98 修复: 用完整的类型代替 'auto'
    typename CacheMap::iterator it = m_cache_map.find(code);

    // 1. 缓存命中 (Hit)
    if (it != m_cache_map.end()) {
        // 把它移动到 "最近使用" 队列的头部
        m_lru_list.splice(m_lru_list.begin(), m_lru_list, it->second);
        // 返回指向缓存中数据的指针
        return it->second->second.data();
    }

    // 2. 缓存未命中 (Miss)
    std::vector<unsigned char> data;
    if (!loadEmojiFromDisk(code, data)) {
        // 从磁盘加载失败 (文件不存在或损坏)
        // C++98 修复: 用 NULL 代替 'nullptr'
        return NULL;
    }

    // 3. 检查缓存是否已满
    if (m_lru_list.size() >= m_max_size) {
        // 缓存满了，T掉队尾的 (最久未使用的)
        u32 code_to_evict = m_lru_list.back().first;
        
        m_lru_list.pop_back();            // 从队列移除
        m_cache_map.erase(code_to_evict); // 从Map移除
    }

    // 4. 将新加载的Emoji添加到缓存头部
    // C++98 修复: 用 std::make_pair 代替 C++11 的 {} 和 std::move
    m_lru_list.push_front(std::make_pair(code, data));
    m_cache_map[code] = m_lru_list.begin();

    // 5. 返回新加载数据的指针
    return m_lru_list.begin()->second.data();
}