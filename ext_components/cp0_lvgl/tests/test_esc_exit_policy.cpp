#include "cp0_esc_exit_policy.hpp"

#include <cassert>

int main()
{
    cp0_esc_exit_policy::StateMachine policy;
    auto decision = policy.update(1000, true);
    assert(!decision.show_hint && !decision.send_terminate);
    decision = policy.update(1499, true);
    assert(!decision.show_hint && !decision.send_terminate);
    decision = policy.update(1500, true);
    assert(decision.show_hint && !decision.send_terminate);
    decision = policy.update(3999, true);
    assert(!decision.send_terminate);
    decision = policy.update(4000, true);
    assert(decision.send_terminate && !decision.send_kill);
    decision = policy.update(6999, true);
    assert(!decision.send_kill);
    decision = policy.update(7000, true);
    assert(decision.send_kill);

    cp0_esc_exit_policy::StateMachine released;
    released.update(0, true);
    decision = released.update(500, true);
    assert(decision.show_hint);
    decision = released.update(501, false);
    assert(decision.hide_hint);
    released.update(1000, true);
    decision = released.update(3499, true);
    assert(!decision.send_terminate);

    cp0_esc_exit_policy::StateMachine graceful;
    graceful.update(0, true);
    graceful.update(3000, true);
    decision = graceful.update(5500, true);
    assert(!decision.send_kill);
    decision = graceful.finish();
    assert(decision.hide_hint);
}
