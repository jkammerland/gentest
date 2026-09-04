#pragma once

namespace shop {

struct PaymentGateway {
    virtual ~PaymentGateway()      = default;
    virtual bool charge(int cents) = 0;
};

class Checkout {
  public:
    explicit Checkout(PaymentGateway &gateway) : gateway_(gateway) {}

    bool pay(int cents) {
        if (cents <= 0) {
            return false;
        }
        return gateway_.charge(cents);
    }

  private:
    PaymentGateway &gateway_;
};

} // namespace shop
