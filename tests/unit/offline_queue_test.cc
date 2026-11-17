#include <cassert>
#include <string>

#include "client/transport/OfflineQueue.h"

int main() {
    eat::client::OfflineQueue queue(2);
    eat::protocol::JsonValue params = eat::protocol::JsonValue::Object();
    params.Set("text", eat::protocol::JsonValue::String("meal"));

    assert(queue.Enqueue("k1", "meal.create", params));
    assert(!queue.Enqueue("k1", "meal.create", params));
    assert(queue.Enqueue("k2", "meal.create", params));
    assert(queue.Full());
    assert(!queue.Enqueue("k3", "meal.create", params));

    eat::client::QueuedWrite write;
    assert(queue.Peek(write));
    assert(write.idem_key == "k1");
    assert(write.method == "meal.create");

    queue.MarkFailed("k1", "timeout", 2);
    queue.MarkFailed("k1", "timeout", 2);
    assert(queue.NeedsManual() == 1);

    queue.MarkDone("k1");
    assert(queue.size() == 1);
    assert(queue.Peek(write));
    assert(write.idem_key == "k2");

    queue.MarkDone("k2");
    assert(queue.size() == 0);
    return 0;
}