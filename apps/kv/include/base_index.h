#pragma once

#include <string>
#include <tuple>
#include <iostream>
#include <vector>

struct BaseIndexConfig {
  uint32_t key_size;
  uint32_t value_size;
  std::string index_name = "cuckoo";
};

class BaseIndex {
public:
  virtual ~BaseIndex() {std::cout << "exit BaseIndex" << std::endl;}
  explicit BaseIndex(const BaseIndexConfig &config){};
  virtual void Util() { std::cout << "BaseIndex Util: no impl" << std::endl; return; }
  virtual void Get(std::string& key, std::string &value) = 0;
  virtual void Put(std::string& key, std::string &value) = 0;
  virtual void Scan(std::string& key, uint cnt, std::vector<uint64_t>& res) = 0;

//   virtual std::pair<uint64_t, uint64_t> RegisterPMAddr() const = 0;

  virtual void DebugInfo() const {};
};
