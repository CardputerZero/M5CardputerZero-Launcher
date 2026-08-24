#include "../main/ui/model/developer_page_model.hpp"

#include <cassert>
#include <string>

using setting::DeveloperPageModel;
using setting::DeveloperOverlayLifecycleModel;
using setting::DeveloperPairViewBuildContract;

int main()
{
    int tracked = 0;
    int stale = 0;
    static_assert(noexcept(setting::developer_delete_callback_allowed(
        &tracked, &tracked, &tracked)));
    assert(setting::developer_delete_callback_allowed(&tracked, &tracked, &tracked));
    assert(!setting::developer_delete_callback_allowed(&tracked, &stale, &tracked));
    assert(!setting::developer_delete_callback_allowed(&stale, &stale, &tracked));
    assert(!setting::developer_delete_callback_allowed(
        static_cast<int *>(nullptr), &tracked, &tracked));
    static_assert(noexcept(setting::developer_async_completion_allowed(
        false, static_cast<int *>(nullptr), static_cast<int *>(nullptr))));
    assert(setting::developer_async_completion_allowed(
        true, &tracked, &tracked));
    assert(!setting::developer_async_completion_allowed(
        false, &tracked, &tracked));
    assert(!setting::developer_async_completion_allowed(
        true, static_cast<int *>(nullptr), &tracked));
    assert(!setting::developer_async_completion_allowed(
        true, &tracked, static_cast<int *>(nullptr)));

    DeveloperPairViewBuildContract pair_build;
    assert(!pair_build.ready());
    for (std::size_t index = 0;
         index < DeveloperPairViewBuildContract::REQUIRED_LABELS - 1; ++index)
        pair_build.label_created();
    assert(!pair_build.ready());
    pair_build.label_created();
    assert(pair_build.ready());
    pair_build.label_created();
    assert(!pair_build.ready());

    DeveloperPageModel model;
    model.begin_pairing(true);
    assert(model.enable_after_pair());
    assert(model.pairing_preview() == "_");
    assert(model.append_pairing_text("QAAAA"));
    assert(model.pairing_preview() == "QAAAA_");

    const std::string utf8 = "\xE4\xB8\xAD";
    assert(model.append_pairing_text(utf8));
    assert(model.erase_pairing_character());
    assert(model.pair_key() == "QAAAA");

    model.clear_pairing();
    assert(model.pair_key().empty());
    assert(!model.enable_after_pair());
    assert(!model.erase_pairing_character());

    assert(model.append_pairing_text(std::string(DeveloperPageModel::MAX_PAIR_KEY_BYTES, 'a')));
    assert(!model.append_pairing_text("b"));
    const std::string preview = model.pairing_preview();
    assert(preview == "..." + std::string(DeveloperPageModel::PAIR_PREVIEW_BYTES, 'a') + "_");

    model.reset_authorization_selection(5);
    assert(model.authorization_selection() == 0);
    assert(!model.select_previous_authorization());
    assert(model.select_next_authorization(5));
    assert(model.select_next_authorization(5));
    assert(model.authorization_selection() == 2);
    assert(model.authorization_window_start() == 1);
    assert(model.select_next_authorization(5));
    assert(model.select_next_authorization(5));
    assert(!model.select_next_authorization(5));
    assert(model.authorization_window_start() == 3);

    model.reconcile_authorization_count(2);
    assert(model.authorization_selection() == 1);
    model.reconcile_authorization_count(0);
    assert(model.authorization_selection() == 0);
    assert(!model.select_next_authorization(0));

    DeveloperOverlayLifecycleModel overlay;
    assert(!overlay.active());
    assert(overlay.open());
    assert(overlay.active());
    assert(!overlay.open());
    assert(overlay.close());
    assert(!overlay.active());
    assert(!overlay.close());
    assert(overlay.open());
    assert(overlay.close());
    return 0;
}
