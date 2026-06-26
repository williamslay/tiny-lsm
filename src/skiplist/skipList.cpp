#include "skiplist/skiplist.h"
#include <cstdint>
#include <iostream>
#include <memory>
#include <spdlog/spdlog.h>
#include <stdexcept>
#include <tuple>
#include <utility>

namespace tiny_lsm {

// ************************ SkipListIterator ************************
BaseIterator &SkipListIterator::operator++() {
  if (current) {
    current = current->forward_[0];
  }
  return *this;
}

bool SkipListIterator::operator==(const BaseIterator &other) const {
  const auto *other_iter =
                          dynamic_cast<const SkipListIterator *>(&other);
  if (!other_iter) {
    return false;
  }
  return current == other_iter->current;
}

bool SkipListIterator::operator!=(const BaseIterator &other) const {
  return !(*this == other);
}

SkipListIterator::value_type SkipListIterator::operator*() const {
  if (!current) {
    throw std::runtime_error("Dereferencing invalid iterator");
  }
  return {current->key_, current->value_};
}

IteratorType SkipListIterator::get_type() const {
  return IteratorType::SkipListIterator;
}

bool SkipListIterator::is_valid() const {
  return current && !current->key_.empty();
}
bool SkipListIterator::is_end() const { return current == nullptr; }

std::string SkipListIterator::get_key() const { return current->key_; }
std::string SkipListIterator::get_value() const { return current->value_; }
uint64_t SkipListIterator::get_tranc_id() const { return current->tranc_id_; }

// ************************ SkipList ************************
// 构造函数
SkipList::SkipList(int max_lvl) : max_level(max_lvl), current_level(1) {
  head = std::make_shared<SkipListNode>("", "", max_level, 0);
  dis_01 = std::uniform_int_distribution<>(0, 1);
  dis_level = std::uniform_int_distribution<>(0, (1 << max_lvl) - 1);
  gen = std::mt19937(std::random_device()());
}

int SkipList::random_level() {
  int lv = 1;
  while (dis_01(gen) == 1 && lv < max_level) {
    lv++;
  }
  return lv;
}

// 插入或更新键值对
void SkipList::put(const std::string &key, const std::string &value,
                   uint64_t tranc_id) {
  spdlog::trace("SkipList--put({}, {}, {}) called", key, value, tranc_id);
  auto current = head;
  std::vector<std::shared_ptr<SkipListNode>> update(max_level, head);
  for(int lv = current_level - 1; lv >= 0; --lv)  {
    while(current->forward_[lv] && current->forward_[lv]->key_ < key) {
      current = current->forward_[lv];
    }
    update[lv] = current;
  }
  auto target = update[0]->forward_[0];
  // update
  if (target && target->key_ == key && target->tranc_id_ == tranc_id) {
    size_bytes = size_bytes + value.size() - target->value_.size();
    target->value_ = value;
    spdlog::trace("SkipList--put({}, {}, {}), key and tranc_id_ is the same, "
                  "only update value to {}",
                  key, value, tranc_id, value);
    return;
  }
  // insert
  int insrt_level = random_level();
  auto new_node = std::make_shared<SkipListNode>(key, value,
                      insrt_level, tranc_id);
  for (int lv = 0; lv < insrt_level; lv++) {
    target = update[lv]->forward_[lv];
    new_node->set_backward(lv, update[lv]);
    update[lv]->forward_[lv] = new_node;
    new_node->forward_[lv] = target;
    if (target) {
      target->set_backward(lv, new_node);
    }
  }
  spdlog::trace("SkipList--put({}, {}, {}),  insert at level {}",
                  key, value, tranc_id, insrt_level);
  current_level = current_level > insrt_level ? current_level : insrt_level;
  size_bytes += new_node->key_.size() + new_node->value_.size() + sizeof(uint64_t);
}

// 查找键值对
SkipListIterator SkipList::get(const std::string &key, uint64_t tranc_id) {
  spdlog::trace("SkipList--get({}) called", key);

  auto current = head;
  for(int lv = current_level - 1; lv >= 0; --lv)  {
    while (current->forward_[lv] && current->forward_[lv]->key_ < key)  {
        current = current->forward_[lv];
    }
  }

  auto target = current->forward_[0];
  if (tranc_id != 0) {
    while (target && target->key_ == key && tranc_id < target->tranc_id_) {
      target = target->forward_[0];
    }
  }
  if (target && target->key_ == key) {
    return SkipListIterator(target);
  }

  return end();
}

// 删除键值对
// ! 这里的 remove 是跳表本身真实的 remove,  lsm 应该使用 put 空值表示删除,
// ! 这里只是为了实现完整的 SkipList 不会真正被上层调用
void SkipList::remove(const std::string &key) {
  std::vector<std::shared_ptr<SkipListNode>> update(max_level, head);
  auto current = head;

  for (int lv = current_level - 1; lv >= 0; --lv) {
    while (current->forward_[lv] && current->forward_[lv]->key_ < key) {
      current = current->forward_[lv];
    }
    update[lv] = current;
  }

  auto target = update[0]->forward_[0];
  if (!target || target->key_ != key) {
    return;
  }

  for (size_t lv = 0; lv < target->forward_.size(); ++lv) {
    if (update[lv]->forward_[lv] != target) {
      continue;
    }

    auto next = target->forward_[lv];
    update[lv]->forward_[lv] = next;
    if (next) {
      next->set_backward(static_cast<int>(lv), update[lv]);
    }
  }

  while (current_level > 1 && !head->forward_[current_level - 1]) {
    --current_level;
  }

  const size_t removed_size = target->key_.size() + target->value_.size() +
                              sizeof(uint64_t);
  size_bytes = size_bytes >= removed_size ? size_bytes - removed_size : 0;

  return;
}

// 刷盘时可以直接遍历最底层链表
std::vector<std::tuple<std::string, std::string, uint64_t>> SkipList::flush() {
  // std::shared_lock<std::shared_mutex> slock(rw_mutex);
  spdlog::debug("SkipList--flush(): Starting to flush skiplist data");

  std::vector<std::tuple<std::string, std::string, uint64_t>> data;
  auto node = head->forward_[0];
  while (node) {
    data.emplace_back(node->key_, node->value_, node->tranc_id_);
    node = node->forward_[0];
  }

  spdlog::debug("SkipList--flush(): Flushed {} entries", data.size());

  return data;
}

size_t SkipList::get_size() {
  // std::shared_lock<std::shared_mutex> slock(rw_mutex);
  return size_bytes;
}

// 清空跳表，释放内存
void SkipList::clear() {
  // std::unique_lock<std::shared_mutex> lock(rw_mutex);
  head = std::make_shared<SkipListNode>("", "", max_level, 0);
  size_bytes = 0;
}

SkipListIterator SkipList::begin() {
  // return SkipListIterator(head->forward[0], rw_mutex);
  return SkipListIterator(head->forward_[0]);
}

SkipListIterator SkipList::end() {
  return SkipListIterator(); // 使用空构造函数
}

// 找到前缀的起始位置
// 返回第一个前缀匹配或者大于前缀的迭代器
SkipListIterator SkipList::begin_preffix(const std::string &preffix) {
  // TODO: Lab1.3 任务：实现前缀查询的起始位置
  // ? 从最高层开始查找, 找到第一个 key >= preffix 的节点
  return SkipListIterator{};
}

// 找到前缀的终结位置
SkipListIterator SkipList::end_preffix(const std::string &prefix) {
  // TODO: Lab1.3 任务：实现前缀查询的终结位置
  // ? 找到第一个 key 不以 prefix 开头的节点作为终结位置
  return SkipListIterator{};
}

// ? 这里单调谓词的含义是, 整个数据库只会有一段连续区间满足此谓词
// ? 例如之前特化的前缀查询，以及后续可能的范围查询，都可以转化为谓词查询
// ? 返回第一个满足谓词的位置和最后一个满足谓词的迭代器
// ? 如果不存在, 返回 nullopt
// ? 谓词作用于key, 且保证满足谓词的结果只在一段连续的区间内, 例如前缀匹配的谓词
// ? predicate返回值:
// ?   0: 满足谓词
// ?   >0: 不满足谓词, 需要向右移动
// ?   <0: 不满足谓词, 需要向左移动
// ! Skiplist 中的谓词查询不会进行事务id的判断, 需要上层自己进行判断
std::optional<std::pair<SkipListIterator, SkipListIterator>>
SkipList::iters_monotony_predicate(
    std::function<int(const std::string &)> predicate) {
  // TODO: Lab1.3 任务：实现谓词查询
  // ? 分两步: 1. 利用多层跳表快速找到谓词满足区间内的一个节点
  // ?         2. 分别向前/向后扩展, 利用 backward_ 和 forward_ 确定区间边界
  // ? 注意: 向前查找时需要利用 backward_ 指针从当前节点的最高层开始回溯
  return std::nullopt;
}

// ? 打印跳表, 你可以在出错时调用此函数进行调试
void SkipList::print_skiplist() {
  for (int level = 0; level < current_level; level++) {
    std::cout << "Level " << level << ": ";
    auto current = head->forward_[level];
    while (current) {
      std::cout << current->key_;
      current = current->forward_[level];
      if (current) {
        std::cout << " -> ";
      }
    }
    std::cout << std::endl;
  }
  std::cout << std::endl;
}
} // namespace tiny_lsm
