#pragma once

#include "settings_sound_card_detail_page.hpp"
#include "settings_sound_card_adapter.hpp"

#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <string>
#include <vector>

class LvSettingSoundCardPage4 : public DComponens::LvglComponensBase {
public:
    using Control = ui_test_soundcard::Control;

    enum class LayoutMetric : int {
        ScreenW = 320,
        ScreenH = 150,
        RowY = 34,
        RowH = 20,
        RowGap = 2,
        VisibleRows = 4,
        ContentX = 8,
        ContentW = 304,
        TitleY = 5,
        TitleW = 230,
        TitleH = 20,
        LoadingY = 48,
        EmptyY = 54,
        UnavailableY = 42,
        ErrorY = 68,
        ErrorH = 24,
        DetailErrorY = 78,
        FooterY = 126,
        FooterH = 14,
        RowTextInset = 4,
        CardNameW = 230,
        ControlNameW = 175,
        ValueX = 184,
        ValueW = 112,
    };

    static constexpr int metric(LayoutMetric value)
    {
        return static_cast<int>(value);
    }

    LvSettingSoundCardPage4();

    LvSettingSoundCardPage4(lv_obj_t *parent,
                            const NodeIter &parent_node,
                            std::function<void()> back_callback);

    void AnimateNextIn(std::function<void()> animate_over_func) override;
    void AnimateNextOut(std::function<void()> animate_over_func) override;
    void LoadNextPage() override;
    void LeaveNextPage() override;

    ~LvSettingSoundCardPage4() override;

    void create_ui(lv_obj_t *parent) override;

private:
    enum class View { Cards, Controls };
    enum class RequestKind { None, Cards, Controls, Detail, Set, RefreshDetail };

    using ApiHandler = std::function<void(int, std::string)>;

    static void api_timer_cb(lv_timer_t *timer) noexcept;

    template <typename Handler>
    void request_api(std::list<std::string> arguments, RequestKind kind, Handler handler);

    void cancel_requests();
    static std::string compact_error(const std::string &data);
    void load_cards();
    void load_controls();
    void open_detail();
    void submit_control(std::string value);
    void close_detail_async();
    static void close_detail_async(void *user_data);
    void close_detail();
    void close_detail_now();
    lv_obj_t *label(const char *text,
                    int x,
                    int y,
                    int width,
                    int height,
                    uint32_t color,
                    int font_size,
                    bool scroll = false);
    void render();
    void add_arrow(int y, const char *direction);
    void handle_key(uint32_t key);
    void handle_key_event(lv_event_t *event);

    NodeIter parent_node_;
    View view_ = View::Cards;
    std::vector<ui_test_soundcard::Card> cards_;
    std::vector<ui_test_soundcard::Control> controls_;
    int selected_index_ = 0;
    int card_index_ = -1;
    bool backend_available_ = false;
    bool loading_ = false;
    bool request_pending_ = false;
    bool detail_close_pending_ = false;
    RequestKind pending_kind_ = RequestKind::None;
    uint64_t generation_ = 0;
    uint64_t pending_generation_ = 0;
    std::string error_message_;
    lv_timer_t *api_timer_ = nullptr;
    std::shared_ptr<bool> page_lifetime_ = std::make_shared<bool>(true);
    ui_test_soundcard::SoundCardApiAdapter api_;
    std::unique_ptr<LvSettingSoundCardDetailPage> detail_page_;
};

std::unique_ptr<DComponens::LvglComponensBase> soundcard_page4_factory(
    lv_obj_t *parent, const NodeIter &page_node, std::function<void()> on_back);
