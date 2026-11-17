#pragma once
#include <functional>
#include <unordered_map>
#include <cstdint>
#include <utility>

namespace engine::core {

    struct SignalConnection {
        uint64_t id{0};
        bool is_valid() const { return id != 0; }
    };

    template<typename... Args>
    class Signal {
    public:
        using SlotType = std::function<void(Args...)>;

        SignalConnection connect(SlotType slot) {
            uint64_t id = ++m_nextId;
            m_slots[id] = std::move(slot);
            return SignalConnection{id};
        }

        void disconnect(SignalConnection conn) {
            if (conn.id != 0) {
                m_slots.erase(conn.id);
            }
        }

        void emit(Args... args) const {
            for (const auto& [id, slot] : m_slots) {
                if (slot) {
                    slot(args...);
                }
            }
        }

        void clear() {
            m_slots.clear();
        }

        size_t slot_count() const {
            return m_slots.size();
        }

    private:
        std::unordered_map<uint64_t, SlotType> m_slots;
        uint64_t m_nextId{0};
    };

} // namespace engine::core
