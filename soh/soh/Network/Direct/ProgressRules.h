#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <cstddef>

namespace Shipwright::Direct {
// Upgrade levels are packed integers, not independent bits. OR-ing level 1
// and level 2 would incorrectly manufacture level 3.
inline uint32_t MergeUpgrades(uint32_t a, uint32_t b) {
    constexpr std::array<unsigned, 8> shifts{ 0, 3, 6, 9, 12, 14, 17, 20 };
    constexpr std::array<uint32_t, 8> widths{ 7, 7, 7, 7, 3, 7, 7, 7 };
    for (size_t i = 0; i < shifts.size(); ++i) {
        uint32_t mask = widths[i] << shifts[i];
        a = (a & ~mask) | (std::max((a & mask) >> shifts[i], (b & mask) >> shifts[i]) << shifts[i]);
    }
    return a;
}
inline uint32_t MergeQuestItems(uint32_t a, uint32_t b, uint16_t ownCapacity, uint16_t otherCapacity) {
    // Keep the fragment count belonging to the greater total health progress.
    // A lower-heart save's fragments must not be added on top of a higher save.
    uint32_t pieces = ownCapacity == otherCapacity ? std::max(a >> 28, b >> 28)
                      : ownCapacity > otherCapacity ? a >> 28 : b >> 28;
    return ((a | b) & 0x0FFFFFFFu) | (pieces << 28);
}
inline bool ValidUpgradeLevels(uint32_t packed) {
    constexpr std::array<unsigned, 8> shifts{ 0, 3, 6, 9, 12, 14, 17, 20 };
    constexpr std::array<uint32_t, 8> widths{ 7, 7, 7, 7, 3, 7, 7, 7 };
    constexpr std::array<uint32_t, 8> maximum{ 3, 3, 3, 2, 3, 3, 3, 3 };
    if ((packed & ~0x7FFFFFu) != 0) return false;
    for (size_t i = 0; i < shifts.size(); ++i)
        if (((packed >> shifts[i]) & widths[i]) > maximum[i]) return false;
    return true;
}
inline bool ValidInventoryItem(size_t slot, uint8_t item) {
    if (item == 0xFF) return slot < 24;
    constexpr std::array<uint8_t, 18> canonical{ 0, 1, 2, 3, 4, 5, 6, 7, 9, 10, 12, 13, 14, 15, 16, 17, 18, 19 };
    if (slot < canonical.size()) return item == canonical[slot] || (slot == 7 && item == 8) || (slot == 9 && item == 11);
    if (slot < 22) return item >= 0x14 && item <= 0x20;
    if (slot == 22) return item >= 0x2D && item <= 0x37;
    if (slot == 23) return item >= 0x21 && item <= 0x2C;
    return false;
}
inline uint8_t MergePermanentItem(size_t slot, uint8_t own, uint8_t other) {
    constexpr uint8_t None = 0xFF;
    // Bottles carry personal contents; the trade slots represent state machines.
    if (slot >= 18) return own;
    if (own == None) return other;
    if (other == None) return own;
    // Ocarina (7/8) and hookshot (10/11) are the only tiered permanent slots.
    if (slot == 7 || slot == 9) return std::max(own, other);
    return own;
}
} // namespace Shipwright::Direct
