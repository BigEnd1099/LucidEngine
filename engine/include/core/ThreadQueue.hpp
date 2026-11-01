#pragma once
#include <queue>
#include <mutex>
#include <optional>
#include <functional>

namespace engine::core {
    template <typename T>
    class ThreadQueue {
    public:
        void push(T value) {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_queue.push(std::move(value));
        }

        std::optional<T> pop() {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_queue.empty()) return std::nullopt;
            T val = std::move(m_queue.front());
            m_queue.pop();
            return val;
        }

        bool empty() const {
            std::lock_guard<std::mutex> lock(m_mutex);
            return m_queue.empty();
        }

    private:
        mutable std::mutex m_mutex;
        std::queue<T> m_queue;
    };
}
