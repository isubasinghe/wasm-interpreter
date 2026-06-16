#pragma once

#include <cstddef>
#include <functional>
#include <initializer_list>
#include <iterator>
#include <stdexcept>
#include <utility>
#include <vector>

template <typename Key, typename Value, typename Equal = std::equal_to<Key>>
// Vector-backed map for small key sets. Insert and erase follow std::vector
// iterator invalidation rules.
class VecMap {
  public:
    using key_type = Key;
    using mapped_type = Value;
    using value_type = std::pair<Key, Value>;
    using container_type = std::vector<value_type>;
    using iterator = typename container_type::iterator;
    using const_iterator = typename container_type::const_iterator;

    VecMap() = default;

    VecMap(std::initializer_list<value_type> values) {
      for(const auto &value: values) {
        insert_or_assign(value.first, value.second);
      }
    }

    iterator begin() {
      return entries.begin();
    }

    const_iterator begin() const {
      return entries.begin();
    }

    const_iterator cbegin() const {
      return entries.cbegin();
    }

    iterator end() {
      return entries.end();
    }

    const_iterator end() const {
      return entries.end();
    }

    const_iterator cend() const {
      return entries.cend();
    }

    bool empty() const {
      return entries.empty();
    }

    size_t size() const {
      return entries.size();
    }

    void reserve(size_t capacity) {
      entries.reserve(capacity);
    }

    void clear() {
      entries.clear();
    }

    iterator find(const Key &key) {
      return find_entry(key);
    }

    const_iterator find(const Key &key) const {
      return find_entry(key);
    }

    bool contains(const Key &key) const {
      return find(key) != end();
    }

    Value *get(const Key &key) {
      auto it = find(key);
      if(it == end()) {
        return nullptr;
      }
      return &it->second;
    }

    const Value *get(const Key &key) const {
      auto it = find(key);
      if(it == end()) {
        return nullptr;
      }
      return &it->second;
    }

    Value &at(const Key &key) {
      auto value = get(key);
      if(value == nullptr) {
        throw std::out_of_range("VecMap key not found");
      }
      return *value;
    }

    const Value &at(const Key &key) const {
      auto value = get(key);
      if(value == nullptr) {
        throw std::out_of_range("VecMap key not found");
      }
      return *value;
    }

    Value &operator[](const Key &key) {
      auto it = find(key);
      if(it != end()) {
        return it->second;
      }

      entries.emplace_back(key, Value{});
      return entries.back().second;
    }

    std::pair<iterator, bool> insert(const value_type &value) {
      auto it = find(value.first);
      if(it != end()) {
        return {it, false};
      }

      entries.push_back(value);
      return {std::prev(entries.end()), true};
    }

    std::pair<iterator, bool> insert(value_type &&value) {
      auto it = find(value.first);
      if(it != end()) {
        return {it, false};
      }

      entries.push_back(std::move(value));
      return {std::prev(entries.end()), true};
    }

    template <typename Mapped>
    std::pair<iterator, bool> insert_or_assign(const Key &key, Mapped &&value) {
      auto it = find(key);
      if(it != end()) {
        it->second = std::forward<Mapped>(value);
        return {it, false};
      }

      entries.emplace_back(key, std::forward<Mapped>(value));
      return {std::prev(entries.end()), true};
    }

    bool erase(const Key &key) {
      auto it = find(key);
      if(it == end()) {
        return false;
      }

      entries.erase(it);
      return true;
    }

  private:
    iterator find_entry(const Key &key) {
      for(auto it = entries.begin(); it != entries.end(); ++it) {
        if(equal(it->first, key)) {
          return it;
        }
      }
      return entries.end();
    }

    const_iterator find_entry(const Key &key) const {
      for(auto it = entries.begin(); it != entries.end(); ++it) {
        if(equal(it->first, key)) {
          return it;
        }
      }
      return entries.end();
    }

    container_type entries;
    Equal equal;
};
