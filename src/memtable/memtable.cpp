#include "memtable/memtable.h"
#include "config/config.h"
#include "consts.h"
#include "iterator/iterator.h"
#include "skiplist/skiplist.h"
#include "sst/sst.h"
#include "spdlog/spdlog.h"
#include <algorithm>
#include <cstddef>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <sys/types.h>
#include <utility>
#include <vector>

namespace tiny_lsm {

class BlockCache;

// MemTable implementation using PIMPL idiom
MemTable::MemTable() : frozen_bytes(0) {
  current_table = std::make_shared<SkipList>();
}
MemTable::~MemTable() = default;

void MemTable::put_(const std::string &key, const std::string &value,
                    uint64_t tranc_id) {
  // TODO: you may need check the tranc_id here???
  current_table->put(key, value, tranc_id);
}

void MemTable::put(const std::string &key, const std::string &value,
                   uint64_t tranc_id) {
  spdlog::trace("MemTable--put({}, {}, {}) called", key, value, tranc_id);

  std::unique_lock<std::shared_mutex> lk1(cur_mtx);
  put_(key, value, tranc_id);
  if (current_table->get_size() >
    TomlConfig::getInstance().getLsmPerMemSizeLimit()) {
    std::unique_lock<std::shared_mutex> lk2(frozen_mtx);
    frozen_cur_table_();
    spdlog::debug("MemTable--Current table size exceeded limit. Frozen and "
                  "created new table.");
  }
}

void MemTable::put_batch(
    const std::vector<std::pair<std::string, std::string>> &kvs,
    uint64_t tranc_id) {
  spdlog::trace("MemTable--put_batch({}) with {} keys called",
               tranc_id, kvs.size());

  std::unique_lock<std::shared_mutex> lk(cur_mtx);
  for (auto it : kvs) {
    put_(it.first, it.second, tranc_id);
  }
  if (current_table->get_size() >
    TomlConfig::getInstance().getLsmPerMemSizeLimit()) {
    std::unique_lock<std::shared_mutex> lk2(frozen_mtx);
    frozen_cur_table_();
    spdlog::debug("MemTable--Current table size exceeded limit. Frozen and "
                  "created new table.");
  }
}

SkipListIterator MemTable::cur_get_(const std::string &key, uint64_t tranc_id) {
  return current_table->get(key, tranc_id);
}

SkipListIterator MemTable::frozen_get_(const std::string &key,
                                       uint64_t tranc_id) {
  for (auto it : frozen_tables) {
    auto res = it->get(key, tranc_id);
    if (res.is_valid()) {
      return res;
    }
  }
  return SkipListIterator{};
}

SkipListIterator MemTable::get(const std::string &key, uint64_t tranc_id) {
  spdlog::trace("MemTable--get({}, {}) called", key, tranc_id);

  std::shared_lock<std::shared_mutex> slk1(cur_mtx);
  auto res = cur_get_(key, tranc_id);
  if (res.is_valid()) {
    return res;
  }
  slk1.unlock();
  // miss in current_table
  spdlog::debug("MemTable--Current table get miss.");
  std::shared_lock<std::shared_mutex> slk2(frozen_mtx);
  res = frozen_get_(key, tranc_id);
  if (res.is_valid()) {
    return res;
  }
  slk2.unlock();
  // miss in frozen table
  spdlog::debug("MemTable--Frozen table get miss.");
  return SkipListIterator{};
}

SkipListIterator MemTable::get_(const std::string &key, uint64_t tranc_id) {
  spdlog::trace("MemTable--get_({}, {}) called", key, tranc_id);

  auto res = cur_get_(key, tranc_id);
  if (res.is_valid()) {
    return res;
  }
  // miss in current_table
  spdlog::debug("MemTable--Current table get_ miss.");
  res = frozen_get_(key, tranc_id);
  if (res.is_valid()) {
    return res;
  }
  // miss in frozen table
  spdlog::debug("MemTable--Frozen table get_ miss.");
  return SkipListIterator{};
}

std::vector<
    std::pair<std::string, std::optional<std::pair<std::string, uint64_t>>>>
MemTable::get_batch(const std::vector<std::string> &keys, uint64_t tranc_id) {
  spdlog::trace("MemTable--get_batch with {} keys", keys.size());

  std::vector<
      std::pair<std::string, std::optional<std::pair<std::string, uint64_t>>>>
      results;
  results.reserve(keys.size());

  // 1. 先获取活跃表的锁
  std::shared_lock<std::shared_mutex> slock1(cur_mtx);
  for (size_t idx = 0; idx < keys.size(); idx++) {
    auto key = keys[idx];
    auto cur_res = cur_get_(key, tranc_id);
    if (cur_res.is_valid()) {
      // 值存在且不为空
      results.emplace_back(
          key, std::make_pair(cur_res.get_value(), cur_res.get_tranc_id()));
    } else {
      // 如果活跃表中未找到，先占位
      results.emplace_back(key, std::nullopt);
    }
  }

  // 2. 如果某些键在活跃表中未找到，还需要查找冻结表
  if (!std::any_of(results.begin(), results.end(), [](const auto &result) {
        return !result.second.has_value();
      })) {
    return results;
  }

  slock1.unlock(); // 释放活跃表的锁
  std::shared_lock<std::shared_mutex> slock2(frozen_mtx); // 获取冻结表的锁
  for (size_t idx = 0; idx < keys.size(); idx++) {
    if (results[idx].second.has_value()) {
      continue; // 如果在活跃表中已经找到，则跳过
    }
    auto key = keys[idx];
    auto frozen_result = frozen_get_(key, tranc_id);
    if (frozen_result.is_valid()) {
      // 值存在且不为空
      results[idx] =
          std::make_pair(key, std::make_pair(frozen_result.get_value(),
                                             frozen_result.get_tranc_id()));
    } else {
      results[idx] = std::make_pair(key, std::nullopt);
    }
  }

  return results;
}

void MemTable::remove_(const std::string &key, uint64_t tranc_id) {
  MemTable::put_(key, "", tranc_id);
}

void MemTable::remove(const std::string &key, uint64_t tranc_id) {
  spdlog::trace("MemTable--remove({}, {}) called", key, tranc_id);
  MemTable::put(key, "", tranc_id);
}

void MemTable::remove_batch(const std::vector<std::string> &keys,
                            uint64_t tranc_id) {
  spdlog::trace("MemTable--remove_batch({}) with {} keys called",
               tranc_id, keys.size());
  std::unique_lock<std::shared_mutex> lk(cur_mtx);
  for (auto it : keys) {
    put_(it, "", tranc_id);
  }
  if (current_table->get_size() >
    TomlConfig::getInstance().getLsmPerMemSizeLimit()) {
    std::unique_lock<std::shared_mutex> lk2(frozen_mtx);
    frozen_cur_table_();
    spdlog::debug("MemTable--Current table size exceeded limit. Frozen and "
                  "created new table.");
  }
}

void MemTable::clear() {
  spdlog::info("MemTable--clear(): Clearing all tables");

  std::unique_lock<std::shared_mutex> lock1(cur_mtx);
  std::unique_lock<std::shared_mutex> lock2(frozen_mtx);
  frozen_tables.clear();
  current_table->clear();
}

// 将最老的 memtable 写入 SST, 并返回控制类
std::shared_ptr<SST>
MemTable::flush_last(SSTBuilder &builder, std::string &sst_path, size_t sst_id,
                     std::vector<uint64_t> &flushed_tranc_ids,
                     std::shared_ptr<BlockCache> block_cache) {
  spdlog::debug("MemTable--flush_last(): Starting to flush memtable to SST{}",
                sst_id);

  // 由于 flush 后需要移除最老的 memtable, 因此需要加写锁
  std::unique_lock<std::shared_mutex> lock(frozen_mtx);

  uint64_t max_tranc_id = 0;
  uint64_t min_tranc_id = UINT64_MAX;

  if (frozen_tables.empty()) {
    // 如果当前表为空，直接返回nullptr
    if (current_table->get_size() == 0) {
      spdlog::debug(
          "MemTable--flush_last(): Current table is empty, returning null");

      return nullptr;
    }
    // 将当前表加入到frozen_tables头部
    frozen_tables.push_front(current_table);
    frozen_bytes += current_table->get_size();
    // 创建新的空表作为当前表
    current_table = std::make_shared<SkipList>();
  }

  // 将最老的 memtable 写入 SST
  std::shared_ptr<SkipList> table = frozen_tables.back();
  frozen_tables.pop_back();
  frozen_bytes -= table->get_size();

  std::vector<std::tuple<std::string, std::string, uint64_t>> flush_data =
      table->flush();
  for (auto &[k, v, t] : flush_data) {
    if (k == "" && v == "") {
      flushed_tranc_ids.push_back(t);
    }
    max_tranc_id = (std::max)(t, max_tranc_id);
    min_tranc_id = (std::min)(t, min_tranc_id);
    builder.add(k, v, t);
  }
  auto sst = builder.build(sst_id, sst_path, block_cache);

  spdlog::info("MemTable--flush_last(): SST{} built successfully at '{}'",
               sst_id, sst_path);

  return sst;
}

void MemTable::frozen_cur_table_() {
  frozen_bytes += current_table->get_size();
  frozen_tables.push_front(std::move(current_table));
  current_table = std::make_shared<SkipList>();
}

void MemTable::frozen_cur_table() {
  spdlog::trace("MemTable--frozen_cur_table(): Acquiring locks and freezing "
                "current table");

  std::unique_lock<std::shared_mutex> lock1(cur_mtx);
  std::unique_lock<std::shared_mutex> lock2(frozen_mtx);
  frozen_cur_table_();
}

size_t MemTable::get_cur_size() {
  std::shared_lock<std::shared_mutex> slock(cur_mtx);
  return current_table->get_size();
}

size_t MemTable::get_frozen_size() {
  std::shared_lock<std::shared_mutex> slock(frozen_mtx);
  return frozen_bytes;
}

size_t MemTable::get_total_size() {
  std::shared_lock<std::shared_mutex> slock1(cur_mtx);
  std::shared_lock<std::shared_mutex> slock2(frozen_mtx);
  return get_frozen_size() + get_cur_size();
}

// TODO: 需要进一步判断这里的 HeapIterator 能否跳过删除元素
HeapIterator MemTable::begin(uint64_t tranc_id) {
  // TODO: Lab2.2 MemTable 的迭代器
  // ? 加 cur_mtx 和 frozen_mtx 读锁, 遍历所有表收集 SearchItem
  // ? 每个 item 包含 key, value, table_idx, 0, tranc_id
  // ? 过滤 tranc_id 不可见的记录 (tranc_id != 0 && iter.get_tranc_id() > tranc_id)
  // ? 返回 HeapIterator(item_vec, tranc_id)
  return {};
}

HeapIterator MemTable::end() {
  // TODO: Lab2.2 MemTable 的迭代器
  // ? 加读锁后返回空 HeapIterator
  return HeapIterator{};
}

HeapIterator MemTable::iters_preffix(const std::string &preffix,
                                     uint64_t tranc_id) {
  // TODO: Lab2.3 MemTable 的前缀迭代器
  // ? 加读锁, 对所有表调用 begin_preffix/end_preffix 遍历前缀范围
  // ? 过滤事务可见性, 同 key 只保留最新版本
  return {};
}

std::optional<std::pair<HeapIterator, HeapIterator>>
MemTable::iters_monotony_predicate(
    uint64_t tranc_id, std::function<int(const std::string &)> predicate) {
  // TODO: Lab2.3 MemTable 的谓词查询迭代器起始范围
  // ? 加读锁, 对所有表调用 iters_monotony_predicate 获取结果
  // ? 过滤事务可见性, 同 key 只保留最新版本
  // ? 若结果为空返回 nullopt; 否则返回 make_pair(HeapIterator(item_vec, tranc_id, true), HeapIterator{})
  return std::nullopt;
}
} // namespace tiny_lsm
