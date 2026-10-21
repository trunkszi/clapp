#include <clapp/builder/value_parser.hpp>

enum class ambiguous {
    first[[= clapp::value{.alias = "second"}]],
    second,
};

int main() {
    return clapp::value_parser<ambiguous>::parse(clapp::os_str{"second"}).has_value() ? 0 : 1;
}
