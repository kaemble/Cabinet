#include "home_log.h"
#include "home.h"
#include <sstream>

static void require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int main() {
    try {
        const char* levels[]{"off","error","warn","info","debug"};
        for (int threshold=0; threshold<5; ++threshold) {
            std::ostringstream output;
            HomeLogger logger{parse_home_log_level(levels[threshold]), &output};
            for (int severity=1; severity<5; ++severity) logger.write(static_cast<HomeLogLevel>(severity),levels[severity]);
            for (int severity=1; severity<5; ++severity)
                require((output.str().find(std::string("[cabinet ")+levels[severity]+"]")!=std::string::npos)==(severity<=threshold),"Log filtering failed");
        }
        require(shell_command({"/opt/My Cabinet/cabinet-runner","--title","Link's game; $HOME `echo nope`",""}) ==
            "'/opt/My Cabinet/cabinet-runner' --title 'Link'\\''s game; $HOME `echo nope`' ''", "Shell quoting changed argument boundaries or allowed expansion");
        require(shell_argument("line\nbreak") == "'line\nbreak'", "Newline argument lost");
        require(shell_command({"cabinet-runner","--system","n64"})=="cabinet-runner --system n64","Simple command is not readable");
        for (const std::vector<std::string>& args : {std::vector<std::string>{"cabinet","--log-level","verbose"}, {"cabinet","--log-level"}}) {
            auto copy=args; std::vector<char*> argv; for (auto& arg:copy) argv.push_back(arg.data());
            bool rejected=false;
            try { run_home(static_cast<int>(argv.size()),argv.data()); }
            catch (const std::exception& error) { rejected=std::string(error.what()).find("--log-level")!=std::string::npos; }
            require(rejected,"Invalid/missing log level accepted");
        }
        for (auto args : {std::vector<std::string>{"cabinet", "--home"}, {"cabinet", "--home", "--help"}}) {
            std::vector<char*> argv; for (auto& arg : args) argv.push_back(arg.data());
            bool rejected = false;
            try { run_home(static_cast<int>(argv.size()), argv.data()); }
            catch (const std::exception& error) { rejected = std::string(error.what()).find("--home") != std::string::npos; }
            require(rejected, "Removed --home option was accepted");
        }
        std::cout << "Home log levels, CLI validation, and command quoting passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
