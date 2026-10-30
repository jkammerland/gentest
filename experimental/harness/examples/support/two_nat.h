#pragma once

#include "gentest/harness.h"

namespace harness_examples {
// Scenario data for the NAT tests.
inline auto twoNat() -> gentest::harness::Topology {
    return {.nodes  = {{.name = "internet", .ipv4Forwarding = false, .bridgeAddress = "198.18.0.1/24"},
                       {.name = "nat_a", .ipv4Forwarding = true, .bridgeAddress = {}},
                       {.name = "nat_b", .ipv4Forwarding = true, .bridgeAddress = {}},
                       {.name = "peer_a", .ipv4Forwarding = false, .bridgeAddress = {}},
                       {.name = "peer_b", .ipv4Forwarding = false, .bridgeAddress = {}}},
            .links  = {{.left  = {.node = "internet", .interface = "a", .address = {}},
                        .right = {.node = "nat_a", .interface = "wan", .address = "198.18.0.2/24"}},
                       {.left  = {.node = "internet", .interface = "b", .address = {}},
                        .right = {.node = "nat_b", .interface = "wan", .address = "198.18.0.3/24"}},
                       {.left  = {.node = "nat_a", .interface = "lan", .address = "10.0.1.1/24"},
                        .right = {.node = "peer_a", .interface = "eth0", .address = "10.0.1.2/24"}},
                       {.left  = {.node = "nat_b", .interface = "lan", .address = "10.0.2.1/24"},
                        .right = {.node = "peer_b", .interface = "eth0", .address = "10.0.2.2/24"}}},
            .routes = {{.node = "peer_a", .destination = "default", .gateway = "10.0.1.1"},
                       {.node = "peer_b", .destination = "default", .gateway = "10.0.2.1"}},
            .nats   = {{.node = "nat_a", .insideInterface = "lan", .outsideInterface = "wan"},
                       {.node = "nat_b", .insideInterface = "lan", .outsideInterface = "wan"}}};
}
} // namespace harness_examples
