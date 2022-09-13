#pragma once

#include <stddef.h>

namespace tnt {

class IData {
public:
  char *data;
  size_t size;
};
} // namespace tnt