#pragma once
#include <functional>
#include <typeindex>
#include <unordered_map>
#include <vector>
#include <memory>

// ─────────────────────────────────────────────────────────────────────────────
// EventDispatcher – typed event bus with deferred (once-per-frame) delivery
//
// Usage:
//   dispatcher.subscribe<DamageEvent>([](const DamageEvent& e) { ... });
//   dispatcher.emit(DamageEvent{ attackerId, targetId, 15, 85 });
//   // ... end of frame ...
//   dispatcher.flush();   // handlers fire here, in FIFO order
// ─────────────────────────────────────────────────────────────────────────────
class EventDispatcher {
public:
    // Register a listener for EventT.  Returns an id that could be used for
    // unsubscribe in the future (not implemented yet — use clear() instead).
    template <typename EventT>
    void subscribe(std::function<void(const EventT&)> handler)
    {
        auto key = std::type_index(typeid(EventT));
        handlers_[key].push_back([h = std::move(handler)](const void* raw) {
            h(*static_cast<const EventT*>(raw));
        });
    }

    // Enqueue an event.  It will NOT be delivered until flush() is called.
    template <typename EventT>
    void emit(const EventT& event)
    {
        auto key = std::type_index(typeid(EventT));
        // Store a heap copy so the event outlives the caller's stack frame.
        auto copy = std::make_shared<EventT>(event);
        pending_.push_back({ key, std::move(copy) });
    }

    // Deliver every queued event to its subscribers (FIFO order), then clear
    // the queue.  Typically called once at the end of each frame.
    void flush()
    {
        // Process into a local copy so that handlers can safely emit new
        // events (they'll be queued for the *next* flush).
        std::vector<PendingEvent> batch;
        batch.swap(pending_);

        for (auto& pe : batch) {
            auto it = handlers_.find(pe.type);
            if (it == handlers_.end()) continue;
            for (auto& fn : it->second)
                fn(pe.data.get());
        }
    }

    // Remove all subscriptions and pending events (useful on Play-mode reset).
    void clear()
    {
        handlers_.clear();
        pending_.clear();
    }

    // Remove only pending events, keep subscriptions.
    void clearQueue()
    {
        pending_.clear();
    }

private:
    using HandlerFn = std::function<void(const void*)>;

    struct PendingEvent {
        std::type_index           type;
        std::shared_ptr<void>     data;   // type-erased event
    };

    std::unordered_map<std::type_index, std::vector<HandlerFn>> handlers_;
    std::vector<PendingEvent>                                    pending_;
};
