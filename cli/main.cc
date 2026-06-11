#include <CLI/CLI.hpp>
#include <fmt/core.h>

#include "util.h"

#include "commands/add.h"
#include "commands/create.h"
#include "commands/delete.h"
#include "commands/extract.h"
#include "commands/info.h"
#include "commands/list.h"
#include "commands/recover.h"
#include "commands/compress.h"

int main(int argc, char** argv) {
    CLI::App app{"Xstd archive tool", "xli"};
    app.set_version_flag("-V,--version", "xli 1.0.0 (Xstd format)");
    app.require_subcommand(1);

    // Global verbose flag — must be parsed before subcommand callback fires
    app.add_flag("-v,--verbose", xstd::cli::g_verbose,
                 "Enable verbose output");

    // Register all subcommands
    xstd::cli::RegisterCreate(app);
    xstd::cli::RegisterAdd(app);
    xstd::cli::RegisterExtract(app);
    xstd::cli::RegisterList(app);
    xstd::cli::RegisterDelete(app);
    xstd::cli::RegisterRecover(app);
    xstd::cli::RegisterInfo(app);
    xstd::cli::RegisterCompress(app);

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError& e) {
        return app.exit(e);
    } catch (const std::exception& e) {
        fmt::print(stderr, "Fatal error: {}\n", e.what());
        return 1;
    }

    return 0;
}
