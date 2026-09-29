#include "vm.hpp"

#include <cassert>
#include <string>

// The sandbox runs a document's expansion under a policy: no files unless
// allowed, and no more tokens than it grants, so a document that loops
// forever stops.

int main() {
    // A document that expands cleanly.
    {
        sandbox::VM vm({.read = true, .tokens = 10'000}, 1024 * 1024);
        const bool clean = vm.evaluate("\\set\\integer0 = \\evaluate{6*7}\\begin{document}Hello\\end{document}");
        assert((clean && !vm.error()) && "a document expands cleanly");
        assert((vm.consumed() > 0) && "and the tokens it read are counted");
        assert((vm.expander().lexicon().resolve(vm.expander().lexicon().intern("\\set")) == "\\set") &&
               "the expander it ran is the VM's own");
    }

    // A loop that never ends is stopped at the token ceiling.
    {
        sandbox::VM vm({.tokens = 500}, 1024 * 1024);
        const bool clean = vm.evaluate("\\define\\again{x\\again}\\again");
        assert((!clean && vm.error()) && "an endless document is stopped");
        bool named = false;
        for (const syntax::Traceback& fault : vm.tracebacks()) {
            named = named || fault.format().find("tokens") != std::string::npos;
        }
        assert((named) && "and the ceiling is what it says stopped it");
    }

    // Files are refused unless the policy allows them.
    {
        sandbox::VM vm({.read = false}, 1024 * 1024);
        assert((!vm.run("anything.tex") && vm.error()) && "reading a file is refused by default");
        assert((!vm.rules().read) && "which is what the policy says");
    }

    return 0;
}
