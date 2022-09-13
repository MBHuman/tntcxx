#pragma once

namespace tnt {

class LightBase {
public:
  bool insert;
  bool remove;
  bool unlink;
  bool isDetached;
  bool isFirst;
  bool isLast;
  bool next;
  bool prev;
  bool selfCheck;
};

} // namespace tnt