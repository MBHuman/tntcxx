#pragma once

namespace tnt {

class EndGuard {

public:
    void disarm(bool isDetached);
    void arm(bool isDetached);
};

} // namespace tnt