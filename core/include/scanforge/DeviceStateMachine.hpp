#pragma once

// Six-state device state machine. Pure C++ (header-only) on purpose: the UI
// enables/disables buttons from can(), while the controller enforces the same
// rules at runtime. One source of truth, no duplicated if/else in the widgets.
// Reference: Qt QStateMachine docs (official examples), CloudCompare's
// mainwindow UI enable/disable logic (GPL - concept only).

#include "Result.hpp"

#include <array>
#include <string>

namespace sf {

enum class DeviceState : std::uint8_t {
    Disconnected = 0,
    Idle,
    Acquiring,
    Processing,
    Error,
    Recovering
};

enum class DeviceEvent : std::uint8_t {
    Connect = 0,
    Disconnect,
    StartAcquire,
    StopAcquire,
    EnterProcessing,
    FinishProcessing,
    Fail,
    BeginRecover,
    RecoverDone
};

[[nodiscard]] inline const char* toString(DeviceState s) noexcept {
    constexpr std::array<const char*, 6> names = {
        "Disconnected", "Idle", "Acquiring", "Processing", "Error", "Recovering"};
    return names[static_cast<std::size_t>(s)];
}

[[nodiscard]] inline const char* toString(DeviceEvent e) noexcept {
    constexpr std::array<const char*, 9> names = {
        "Connect", "Disconnect", "StartAcquire", "StopAcquire",
        "EnterProcessing", "FinishProcessing", "Fail", "BeginRecover",
        "RecoverDone"};
    return names[static_cast<std::size_t>(e)];
}

// Allowed transitions. Anything not listed here is rejected with InvalidState,
// which is what prevents "change calibration while acquiring" and similar bugs.
[[nodiscard]] constexpr bool canTransition(DeviceState from, DeviceEvent e) noexcept {
    using S = DeviceState; using E = DeviceEvent;
    switch (from) {
    case S::Disconnected: return e == E::Connect;
    case S::Idle:         return e == E::Disconnect || e == E::StartAcquire ||
                                 e == E::Fail;
    case S::Acquiring:    return e == E::StopAcquire || e == E::EnterProcessing ||
                                 e == E::Fail || e == E::Disconnect;
    case S::Processing:   return e == E::FinishProcessing || e == E::Fail ||
                                 e == E::StopAcquire;
    case S::Error:        return e == E::BeginRecover || e == E::Disconnect;
    case S::Recovering:   return e == E::RecoverDone || e == E::Fail ||
                                 e == E::Disconnect;
    }
    return false;
}

class DeviceStateMachine {
public:
    [[nodiscard]] DeviceState state() const noexcept { return state_; }

    [[nodiscard]] bool can(DeviceEvent e) const noexcept {
        return canTransition(state_, e);
    }

    Status apply(DeviceEvent e) {
        if (!canTransition(state_, e)) {
            return Status::failure(makeError(
                ErrorCode::InvalidState,
                std::string("cannot handle ") + toString(e) +
                " while " + toString(state_),
                "DeviceStateMachine::apply"));
        }
        switch (e) {
        case DeviceEvent::Connect:          state_ = DeviceState::Idle;        break;
        case DeviceEvent::Disconnect:       state_ = DeviceState::Disconnected;break;
        case DeviceEvent::StartAcquire:     state_ = DeviceState::Acquiring;   break;
        case DeviceEvent::StopAcquire:      state_ = DeviceState::Idle;        break;
        case DeviceEvent::EnterProcessing:  state_ = DeviceState::Processing;  break;
        case DeviceEvent::FinishProcessing: state_ = DeviceState::Idle;        break;
        case DeviceEvent::Fail:             state_ = DeviceState::Error;       break;
        case DeviceEvent::BeginRecover:     state_ = DeviceState::Recovering;  break;
        case DeviceEvent::RecoverDone:      state_ = DeviceState::Idle;        break;
        }
        return ok();
    }

private:
    DeviceState state_ = DeviceState::Disconnected;
};

} // namespace sf
