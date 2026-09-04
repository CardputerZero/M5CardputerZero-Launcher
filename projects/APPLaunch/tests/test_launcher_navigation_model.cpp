#include "../main/ui/model/launcher_navigation_model.hpp"
#include "../main/ui/model/launcher_startup_contract.hpp"

#include <cassert>

int main()
{
    int timer = 0;
    int stale_timer = 0;
    assert(launcher_startup_timer_is_current(&timer, &timer));
    assert(!launcher_startup_timer_is_current(&stale_timer, &timer));
    assert(!launcher_startup_timer_is_current(nullptr, &timer));
    assert(!launcher_startup_timer_is_current(&timer, static_cast<int *>(nullptr)));

    auto one_shot = launcher_startup_one_shot_decision(true, true, true);
    assert(one_shot.retire_timer);
    assert(one_shot.release_context);
    assert(one_shot.complete);

    one_shot = launcher_startup_one_shot_decision(true, true, false);
    assert(one_shot.retire_timer);
    assert(one_shot.release_context);
    assert(!one_shot.complete);

    one_shot = launcher_startup_one_shot_decision(true, false, false);
    assert(one_shot.retire_timer);
    assert(!one_shot.release_context);
    assert(!one_shot.complete);

    one_shot = launcher_startup_one_shot_decision(false, true, true);
    assert(!one_shot.retire_timer);
    assert(!one_shot.release_context);
    assert(!one_shot.complete);

    assert(launcher_startup_should_recover_deleted_gif(true, false));
    assert(!launcher_startup_should_recover_deleted_gif(true, true));
    assert(!launcher_startup_should_recover_deleted_gif(false, false));

    LauncherStartupGeneration generation;
    const auto first_generation = generation.begin();
    assert(generation.current(first_generation));
    assert(generation.stop(first_generation));
    const auto second_generation = generation.begin();
    assert(second_generation != first_generation);
    assert(!generation.current(first_generation));
    assert(!generation.stop(first_generation));
    assert(generation.current(second_generation));

    LauncherStartupDelayModel startup_delay;
    assert(startup_delay.state() == LauncherStartupDelayState::IDLE);
    assert(startup_delay.begin());
    assert(!startup_delay.begin());
    assert(!startup_delay.timer_created(true));
    assert(startup_delay.state() == LauncherStartupDelayState::WAITING);
    assert(startup_delay.complete());
    assert(!startup_delay.complete());

    LauncherStartupDelayModel creation_failure;
    assert(creation_failure.begin());
    assert(creation_failure.timer_created(false));
    assert(creation_failure.state() == LauncherStartupDelayState::COMPLETE);
    assert(!creation_failure.complete());

    LauncherStartupDelayModel stopped;
    assert(stopped.begin());
    stopped.stop();
    assert(stopped.state() == LauncherStartupDelayState::STOPPED);
    assert(!stopped.complete());
    assert(!stopped.timer_created(false));

    LauncherPageLifecycleModel lifecycle;
    assert(lifecycle.state() == LauncherPageState::HOME);
    assert(!lifecycle.request_home());
    assert(!lifecycle.abort_app());
    assert(lifecycle.begin_app());
    assert(!lifecycle.begin_app());
    assert(lifecycle.abort_app());
    assert(lifecycle.state() == LauncherPageState::HOME);
    assert(!lifecycle.abort_app());
    assert(lifecycle.begin_app());
    assert(lifecycle.request_home());
    assert(!lifecycle.request_home());
    assert(!lifecycle.abort_app());
    lifecycle.cancel_home_request();
    assert(lifecycle.state() == LauncherPageState::APP_ACTIVE);
    assert(lifecycle.request_home());
    assert(lifecycle.complete_home());
    assert(!lifecycle.complete_home());
    assert(lifecycle.state() == LauncherPageState::HOME);
    assert(lifecycle.begin_app());
    lifecycle.stop();
    assert(lifecycle.state() == LauncherPageState::STOPPED);
    assert(!lifecycle.abort_app());
    assert(!lifecycle.request_home());
    assert(!lifecycle.begin_app());

    LauncherNavigationModel model;
    assert(model.selected_page() == LauncherNavigationModel::INITIAL_PAGE);
    assert(model.keyboard_navigation_enabled());

    assert(model.begin_navigation(LauncherNavigationDirection::PREVIOUS));
    assert(model.selected_page() == 1);
    assert(model.is_animating());
    assert(!model.keyboard_navigation_enabled());
    assert(!model.begin_navigation(LauncherNavigationDirection::NEXT));
    assert(model.selected_page() == 1);

    assert(model.cancel_navigation());
    assert(model.selected_page() == LauncherNavigationModel::INITIAL_PAGE);
    assert(!model.is_animating());
    assert(!model.cancel_navigation());

    assert(model.begin_navigation(LauncherNavigationDirection::PREVIOUS));
    model.finish_navigation();
    assert(model.begin_navigation(LauncherNavigationDirection::PREVIOUS));
    model.finish_navigation();
    assert(model.begin_navigation(LauncherNavigationDirection::PREVIOUS));
    assert(model.selected_page() == LauncherNavigationModel::PAGE_COUNT - 1);
    model.finish_navigation();

    assert(model.begin_navigation(LauncherNavigationDirection::NEXT));
    assert(model.selected_page() == 0);
    model.finish_navigation();

    return 0;
}
