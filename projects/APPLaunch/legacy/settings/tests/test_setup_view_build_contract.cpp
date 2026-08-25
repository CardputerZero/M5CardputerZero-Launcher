#include "../main/ui/model/setup_view_build_contract.hpp"

#include <cassert>

int main()
{
    SetupMainViewBuildContract build(7);
    assert(!build.ready());

    build.selection_created();
    assert(!build.ready());
    build.hint_created();
    assert(!build.ready());

    for (int row = 0; row < 6; ++row) build.row_created();
    assert(!build.ready());
    build.row_created();
    assert(build.ready());

    SetupMainViewBuildContract missing_selection(0);
    missing_selection.hint_created();
    assert(!missing_selection.ready());

    SetupMainViewBuildContract too_many_rows(1);
    too_many_rows.selection_created();
    too_many_rows.hint_created();
    too_many_rows.row_created();
    assert(too_many_rows.ready());
    too_many_rows.row_created();
    assert(!too_many_rows.ready());
}
