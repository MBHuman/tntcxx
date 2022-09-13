#pragma once

#include <tntcxx/Buffer/Block.hpp>
#include <tntcxx/Buffer/WData.hpp>
#include <tntcxx/Buffer/RData.hpp>

namespace tnt {

class IteratorCommon {
public:
    IteratorCommon() = default;
    void set(WData data);
    void write(WData data);
    void get(RData data);
    void read();
private:
    void adjustPositionForward();
    void moveForward();
    void moveBackward();
    Block getBlock();
};

} // namespace tnt