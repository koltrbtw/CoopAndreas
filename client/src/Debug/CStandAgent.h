#pragma once

#include <string>
#include <utility>
#include <vector>

// debug instances started by the dev stand hub (--hub <host:port> --stand <id>): a TCP line channel to it, kept by a
// thread of its own and reconnected when the hub restarts. The hub sends stand commands (CDebugCommands) and gets
// their log lines back, the client sends its state twice a second. Without --hub nothing runs.
//   client -> hub:  HELLO {"stand","n","pid"}   STATE {...}   REPLY {"id","output"}
//   hub -> client:  CMD <id> <command line>
class CStandAgent
{
public:
    // before CLaunchManager::CollectCommandLineArgs, it cuts the command line with strtok
    static void ParseCommandLine();
    // once the log is set up: starts the channel thread
    static void Init();

    // game thread, every frame: the state snapshot for the hub
    static void Process();

    // game thread, in the game (CDebugCommands::Process): the commands from the hub and their output
    static std::vector<std::pair<uint32_t, std::string>> TakeCommands();
    static void Reply(uint32_t id, const std::string& output);

private:
    static void Run();
    static std::string BuildState();
};
