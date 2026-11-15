#include "mini_test.hpp"

#include "scanforge/DeviceStateMachine.hpp"
#include "scanforge/FrameQueue.hpp"

#include <atomic>
#include <chrono>
#include <thread>

using namespace sf;

SF_TEST("BoundedQueue drains everything that was queued before close()") {
    BoundedQueue<int> queue(4);
    SF_CHECK(queue.push(1) == BoundedQueue<int>::PushOutcome::Accepted);
    SF_CHECK(queue.push(2) == BoundedQueue<int>::PushOutcome::Accepted);
    SF_CHECK(queue.size() == 2);

    queue.close();
    SF_CHECK(queue.isClosed());
    SF_CHECK(queue.push(3) == BoundedQueue<int>::PushOutcome::Closed);
    SF_CHECK(queue.pop().value_or(-1) == 1);
    SF_CHECK(queue.pop().value_or(-1) == 2);
    SF_CHECK(!queue.pop().has_value());   // closed AND drained
}

SF_TEST("BoundedQueue applies back-pressure instead of growing forever") {
    BoundedQueue<int> queue(2);
    std::atomic<int> pushed{0};
    std::thread producer([&] {
        for (int i = 1; i <= 5; ++i) {
            queue.push(i);
            ++pushed;
        }
        queue.close();
    });

    // The producer must be blocked on the 3rd push until we consume.
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
    SF_CHECK(pushed.load() <= 2);

    int sum = 0;
    while (auto item = queue.pop()) sum += *item;
    producer.join();
    SF_CHECK(sum == 15);
    SF_CHECK(queue.size() == 0);
}

SF_TEST("State machine enforces the device lifecycle") {
    DeviceStateMachine fsm;
    SF_CHECK(fsm.state() == DeviceState::Disconnected);
    SF_CHECK(!fsm.can(DeviceEvent::StartAcquire));   // cannot acquire before connect
    SF_CHECK(fsm.apply(DeviceEvent::StartAcquire).hasValue() == false);

    SF_CHECK(fsm.apply(DeviceEvent::Connect).hasValue());
    SF_CHECK(fsm.state() == DeviceState::Idle);
    SF_CHECK(fsm.apply(DeviceEvent::StartAcquire).hasValue());
    SF_CHECK(fsm.state() == DeviceState::Acquiring);
    SF_CHECK(!fsm.can(DeviceEvent::Connect));        // no re-connect while acquiring
    SF_CHECK(fsm.apply(DeviceEvent::StopAcquire).hasValue());
    SF_CHECK(fsm.state() == DeviceState::Idle);
}

SF_TEST("State machine supports failure and recovery without losing control") {
    DeviceStateMachine fsm;
    SF_CHECK(fsm.apply(DeviceEvent::Connect).hasValue());
    SF_CHECK(fsm.apply(DeviceEvent::Fail).hasValue());
    SF_CHECK(fsm.state() == DeviceState::Error);
    SF_CHECK(!fsm.can(DeviceEvent::StartAcquire));   // UI must disable the button
    SF_CHECK(fsm.apply(DeviceEvent::BeginRecover).hasValue());
    SF_CHECK(fsm.state() == DeviceState::Recovering);
    SF_CHECK(fsm.apply(DeviceEvent::RecoverDone).hasValue());
    SF_CHECK(fsm.state() == DeviceState::Idle);
    SF_CHECK(fsm.apply(DeviceEvent::Disconnect).hasValue());
    SF_CHECK(fsm.state() == DeviceState::Disconnected);
}
