#pragma once

namespace app {

struct Account {
    virtual ~Account()          = default;
    virtual int balance() const = 0;
};

} // namespace app
