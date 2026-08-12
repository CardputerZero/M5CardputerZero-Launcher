#include "../main/ui/model/ssh_connection_model.hpp"

#include <cassert>
#include <iterator>

namespace {

void clear_active(SshConnectionModel &model)
{
    while (model.erase_last()) {
    }
}

} // namespace

int main()
{
    SshConnectionModel model;
    assert(model.label(0) == std::string("Host"));
    assert(model.value(0) == "192.168.1.1");
    assert(model.valid());
    assert(model.validate().error == SshConnectionModel::Error::NONE);

    auto arguments = model.arguments();
    assert(arguments.size() == 3);
    auto argument = arguments.begin();
    assert(*argument++ == "-o");
    assert(*argument++ == "StrictHostKeyChecking=no");
    assert(*argument == "pi@192.168.1.1");

    assert(!model.select_previous());
    assert(model.select_next());
    clear_active(model);
    assert(model.append("2222"));
    arguments = model.arguments();
    argument = arguments.begin();
    std::advance(argument, 2);
    assert(*argument++ == "-p");
    assert(*argument++ == "2222");
    assert(*argument == "pi@192.168.1.1");

    clear_active(model);
    assert(model.append("70000"));
    assert(!model.valid() && model.arguments().empty());
    assert(model.validate().field == SshConnectionModel::Field::PORT);
    clear_active(model);
    assert(model.append("65535"));
    assert(model.valid());
    clear_active(model);
    assert(model.append("0"));
    assert(!model.valid() && model.arguments().empty());
    clear_active(model);
    assert(model.append("+22"));
    assert(!model.valid());
    clear_active(model);
    assert(model.append("22 "));
    assert(!model.valid());
    clear_active(model);
    assert(model.append("999999999999999999999999999999"));
    assert(!model.valid());
    clear_active(model);
    assert(model.append("22"));

    assert(model.select_previous());
    clear_active(model);
    assert(model.append("host"));
    assert(model.append("\xE7\x95\x8C"));
    assert(model.erase_last());
    assert(model.value(0) == "host");
    assert(!model.append("\n"));

    clear_active(model);
    assert(model.append("-oProxyCommand=bad"));
    assert(!model.valid());

    SshConnectionModel loaded;
    assert(loaded.set_values("example.com", "2222", "alice"));
    assert(loaded.value(0) == "example.com");
    assert(!loaded.set_values("", "22", "alice"));
    assert(loaded.value(0) == "example.com");
}
