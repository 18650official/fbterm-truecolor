#ifndef EMOJI_CACHE_H
#define EMOJI_CACHE_H

#include <list>
#include <map>
#include <vector>
#include "fbdev.h" // 为了 u32, u8 等类型
#include <cstddef> // !! 修复 C++98: 为了 size_t !!

class EmojiCache {
public:
    // 构造函数：
    // max_size: 缓存中最多存储多少个 emoji
    // width:    emoji 图片的宽度 (来自 FW(2))
    // height:   emoji 图片的高度 (来自 FH(1))
    EmojiCache(size_t max_size, int width, int height);
    
    // 析构函数，用于释放缓存的内存
    ~EmojiCache();

    // 获取 emoji 数据
    // 成功: 返回一个指向 emoji 像素数据 (RGBRGB...) 的指针
    // 失败: 返回 NULL (例如文件不存在)
    const unsigned char* get(u32 code);

private:
    // 从磁盘加载 emoji 的辅助函数
    bool loadEmojiFromDisk(u32 code, std::vector<unsigned char>& data);

    // C++98 修复: 添加 typedef 以便在 .cpp 中代替 'auto'
    typedef std::list<std::pair<u32, std::vector<unsigned char> > > LruList;
    typedef std::map<u32, typename LruList::iterator> CacheMap;

    size_t m_max_size;
    int m_emoji_width;
    int m_emoji_height;

    // (code, data) 对的列表，用于维护 LRU 顺序
    LruList m_lru_list;

    // map 存储 code 到 list 迭代器的映射，用于 O(1) 快速查找
    CacheMap m_cache_map;
};

#endif // EMOJI_CACHE_H