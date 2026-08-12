#include "zclaw_secret_input_model.h"

#include <cassert>

int main()
{
    assert(zclaw::secret_input_initial_text().empty());
    assert(zclaw::apply_secret_input("existing", "") == "existing");
    assert(zclaw::apply_secret_input("", "").empty());
    assert(zclaw::apply_secret_input("existing", "replacement") == "replacement");
    return 0;
}
