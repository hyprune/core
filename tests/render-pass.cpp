#include "platform/RenderPass.hpp"
#undef NDEBUG
#include <cassert>
#include <iostream>
using hyprune::platform::RenderPass;
int main() {
    int a = 0, b = 0;
    RenderPass pass;
    // Normal paired frames.
    assert(!pass.begin(&a) && pass.active() && pass.monitor() == &a);
    pass.end();
    assert(!pass.active() && !pass.settle() && pass.leaked() == 0);
    // beginRender failed: RENDER_PRE without RENDER_POST. The next event-loop
    // entry closes it, so deferred work is not blocked forever.
    pass.begin(&a);
    assert(pass.active());
    assert(pass.settle() && !pass.active() && pass.leaked() == 1);
    assert(!pass.settle()); // idempotent
    // Leaked pass followed directly by another monitor's frame: the new PRE
    // replaces it and its POST really ends the pass (no counter drift).
    pass.begin(&a);
    assert(pass.begin(&b) && pass.leaked() == 2 && pass.monitor() == &b);
    pass.end();
    assert(!pass.active());
    // Rule 2: a genuinely open pass stays open until its POST (nothing settles
    // it from inside the pass), and many failed frames never accumulate depth.
    for (int i = 0; i < 1000; i++)
        pass.begin(&a);
    assert(pass.active());
    pass.end();
    assert(!pass.active() && pass.passes() == 1004);
    std::cout << "PASS render pass: PRE without POST recovers, no depth drift\n";
}
