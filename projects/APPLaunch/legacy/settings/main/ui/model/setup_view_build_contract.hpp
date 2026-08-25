#pragma once

#include <cstddef>

class SetupMainViewBuildContract
{
public:
    explicit constexpr SetupMainViewBuildContract(std::size_t required_rows)
        : required_rows_(required_rows)
    {
    }

    constexpr void selection_created() { selection_created_ = true; }
    constexpr void hint_created() { hint_created_ = true; }
    constexpr void row_created() { ++created_rows_; }

    constexpr bool ready() const
    {
        return selection_created_ && hint_created_ &&
               created_rows_ == required_rows_;
    }

private:
    std::size_t required_rows_;
    std::size_t created_rows_ = 0;
    bool selection_created_ = false;
    bool hint_created_ = false;
};
