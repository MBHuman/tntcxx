#pragma once

#include <stddef.h>
#include <list>
#include <string>
#include <tntcxx/Buffer/EndGuard.hpp>
#include <tntcxx/Buffer/WData.hpp>
#include <tntcxx/Buffer/Block.hpp>

namespace tnt {

class Buffer {
public:
  Buffer() = default;
  Buffer(const Buffer &) = delete;
  Buffer(Buffer &&) = default;
  Buffer &operator=(const Buffer &) = delete;
  Buffer &operator=(Buffer &&) = default;
  virtual ~Buffer() = default;

  void write(WData data);
  void dropBack(size_t size);
  void dropFront(size_t size);
  char *getEnd();
  void setEnd();
  EndGuard EndGuard();
  // void insert(iterator &pos, size_t num);
  // void relsease(iterator &pos, size_t num);
  // void resize(iterator &, size_t first, size_t second);
  void flush();
  // size_t getIOV(iterator_common &pos, struct iovec *iov, size_t iovcnt);
  bool empty();
  int debugSelfchec();
  int blockSize();
  std::string dump(Buffer &buf);

private:
  Block newBlock(size_t size);
  void deleteBlock(Block block);
  bool isSameBlock(Block block) const;
  size_t leftInBLock(char *ptr) const;
  bool isEndOfBlock(char *ptr) const;

  std::list<Block> blocks;
  std::list<Block> m_iterators;
  char *m_begin;
  char *m_end;
  // m_all allocator;
};
} // namespace tnt