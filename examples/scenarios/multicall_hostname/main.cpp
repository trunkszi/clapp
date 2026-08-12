#include <clapp/clapp.hpp>

#include <print>
#include <variant>

namespace {
    /** \brief `hostname` — show the host part of the FQDN. */
    struct[[= clapp::cmd{.name = "hostname", .about = "show hostname part of FQDN"}]] applet_hostname {
    };

    /** \brief `dnsdomainname` — show the domain part of the FQDN. */
    struct[[= clapp::cmd{.name = "dnsdomainname", .about = "show domain name part of FQDN"}]]
            applet_dnsdomainname {
    };

    /** \brief The multicall root. It has no name of its own that a user ever types. */
    struct[[= clapp::cmd{.arg_required_else_help = true, .multicall = true}]] cli {
        [[= clapp::subcommand{}]] std::variant<applet_hostname, applet_dnsdomainname> command;
    };
}

int main(int argc, char **argv) {
    const cli parsed = clapp::parse<cli>(argc, argv);

    if (std::holds_alternative<applet_hostname>(parsed.command))
        std::println("www");
    else
        std::println("example.com");
}
