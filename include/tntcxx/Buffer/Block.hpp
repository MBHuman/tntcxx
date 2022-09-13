#pragma once

#include <stddef.h>

namespace tnt {

class Block {
public:
  size_t id;
  size_t DATA_SIZE;
  size_t DATA_OFFSET;
};

} // namespace tnt