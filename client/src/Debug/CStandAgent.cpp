#include "stdafx.h"
#include "CStandAgent.h"
#include "CStandFrames.h"
#include <mutex>
#include <thread>
#include <ws2tcpip.h>
#include <CWanted.h>
#include <CClock.h>
#include <CTheScripts.h>

namespace
{
std::string g_host;
int g_nPort = 0;
std::string g_stand;
bool g_bBadArgs = false;

std::mutex g_mutex;
std::vector<std::pair<uint32_t, std::string>> g_commands;
std::vector<std::string> g_outgoing;
std::string g_state;
bool g_bStateNew = false;
}

static std::string JsonString(const std::string& text)
{
    std::string out = "\"";
    for (unsigned char c : text)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20)
            {
                char esc[8];
                sprintf_s(esc, "\\u%04x", c);
                out += esc;
            }
            else
            {
                out += static_cast<char>(c);
            }
        }
    }
    return out + "\"";
}

void CStandAgent::ParseCommandLine()
{
    if (!CCore::ms_bDebug)
    {
        return;
    }

    const char* cmd = GetCommandLineA();
    const char* pHub = strstr(cmd, "--hub ");
    const char* pStand = strstr(cmd, "--stand ");
    if (!pHub || !pStand)
    {
        return;
    }

    char host[64]{};
    char stand[32]{};
    int port = 0;
    if (sscanf_s(pHub + strlen("--hub "), "%63[^: ]:%d", host, (unsigned)sizeof(host), &port) != 2 ||
        sscanf_s(pStand + strlen("--stand "), "%31s", stand, (unsigned)sizeof(stand)) != 1)
    {
        g_bBadArgs = true; // the log is not set up yet, Init tells
        return;
    }

    g_host = host;
    g_nPort = port;
    g_stand = stand;
}

void CStandAgent::Init()
{
    if (g_bBadArgs)
    {
        logger::warn("stand agent: bad --hub <host:port> / --stand <id>, no channel");
    }
    if (g_nPort == 0)
    {
        return;
    }

    logger::info("stand agent: hub %s:%d, stand %s", g_host.c_str(), g_nPort, g_stand.c_str());
    CStandFrames::Init(g_stand, CCore::ms_nRunIndex);
    std::thread(Run).detach();
}

static bool SendAll(SOCKET sock, const std::string& data)
{
    size_t sent = 0;
    while (sent < data.size())
    {
        int n = send(sock, data.data() + sent, static_cast<int>(data.size() - sent), 0);
        if (n <= 0)
        {
            return false;
        }
        sent += n;
    }
    return true;
}

void CStandAgent::Run()
{
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(static_cast<u_short>(g_nPort));
    inet_pton(AF_INET, g_host.c_str(), &addr.sin_addr);

    char hello[128];
    sprintf_s(hello, "HELLO {\"stand\":%s,\"n\":%d,\"pid\":%lu,\"frames\":true}\n", JsonString(g_stand).c_str(),
        CCore::ms_nRunIndex, GetCurrentProcessId());

    // the hub may be down for a rebuild: retry forever, nothing is kept for it meanwhile but the latest state
    while (true)
    {
        SOCKET sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
        if (sock == INVALID_SOCKET || connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0)
        {
            if (sock != INVALID_SOCKET)
            {
                closesocket(sock);
            }
            Sleep(1000);
            continue;
        }

        {
            std::lock_guard lock(g_mutex);
            g_outgoing.clear();
            g_bStateNew = true;
        }

        std::string input;
        bool bAlive = SendAll(sock, hello);
        while (bAlive)
        {
            std::vector<std::string> lines;
            {
                std::lock_guard lock(g_mutex);
                lines.swap(g_outgoing);
                if (g_bStateNew && !g_state.empty())
                {
                    lines.push_back("STATE " + g_state + "\n");
                    g_bStateNew = false;
                }
            }
            for (const auto& line : lines)
            {
                if (!SendAll(sock, line))
                {
                    bAlive = false;
                    break;
                }
            }
            if (!bAlive)
            {
                break;
            }

            fd_set readable;
            FD_ZERO(&readable);
            FD_SET(sock, &readable);
            timeval timeout{0, 50000};
            int ready = select(0, &readable, nullptr, nullptr, &timeout);
            if (ready < 0)
            {
                break;
            }
            if (ready == 0)
            {
                continue;
            }

            char buffer[4096];
            int n = recv(sock, buffer, sizeof(buffer), 0);
            if (n <= 0)
            {
                break;
            }
            input.append(buffer, n);

            size_t end;
            while ((end = input.find('\n')) != std::string::npos)
            {
                std::string line = input.substr(0, end);
                input.erase(0, end + 1);
                if (!line.empty() && line.back() == '\r')
                {
                    line.pop_back();
                }

                // CMD <id> <command line>
                if (line.rfind("CMD ", 0) == 0)
                {
                    char* pRest = nullptr;
                    uint32_t id = strtoul(line.c_str() + 4, &pRest, 10);
                    std::lock_guard lock(g_mutex);
                    g_commands.emplace_back(id, *pRest == ' ' ? pRest + 1 : "");
                }
                // FRAMES <fps> <width>: the hub watches (CStandFrames)
                else if (line.rfind("FRAMES ", 0) == 0)
                {
                    int fps = 0, width = 0;
                    sscanf_s(line.c_str() + 7, "%d %d", &fps, &width);
                    CStandFrames::SetDemand(fps, width);
                }
            }
        }

        closesocket(sock);
        Sleep(1000);
    }
}

std::vector<std::pair<uint32_t, std::string>> CStandAgent::TakeCommands()
{
    std::vector<std::pair<uint32_t, std::string>> commands;
    if (g_nPort != 0)
    {
        std::lock_guard lock(g_mutex);
        commands.swap(g_commands);
    }
    return commands;
}

void CStandAgent::Reply(uint32_t id, const std::string& output)
{
    std::string line = "REPLY {\"id\":" + std::to_string(id) + ",\"output\":" + JsonString(output) + "}\n";
    std::lock_guard lock(g_mutex);
    g_outgoing.push_back(std::move(line));
}

void CStandAgent::Process()
{
    if (g_nPort == 0)
    {
        return;
    }

    static unsigned int nLastState = 0;
    if (GetTickCount() - nLastState < 500)
    {
        return;
    }
    nLastState = GetTickCount();

    std::string state = BuildState();
    std::lock_guard lock(g_mutex);
    g_state = std::move(state);
    g_bStateNew = true;
}

// what a test asks first: where the player is, in what, how the session and the sync look
std::string CStandAgent::BuildState()
{
    char buffer[256];
    std::string json = "{";
    auto add = [&](const char* key, const std::string& value)
    {
        if (json.size() > 1)
        {
            json += ",";
        }
        json += "\"";
        json += key;
        json += "\":";
        json += value;
    };
    auto num = [&](const char* format, auto... args)
    {
        sprintf_s(buffer, format, args...);
        return std::string(buffer);
    };
    auto boolean = [](bool b) { return std::string(b ? "true" : "false"); };

    add("connected", boolean(CNetwork::m_bConnected));
    add("authenticated", boolean(CNetwork::m_bAuthenticated));
    add("playerId", CNetwork::m_bAuthenticated ? JsonString(to_string(CNetworkPlayerManager::m_localPlayerId)) : "null");
    add("name", JsonString(CLocalPlayer::m_Name));
    add("host", boolean(CLocalPlayer::m_bIsHost));
    add("remotePlayers", num("%d", static_cast<int>(CNetworkPlayerManager::m_pPlayers.size())));

    if (CPlayerPed* pPlayer = FindPlayerPed(0))
    {
        const CVector& pos = pPlayer->GetPosition();
        add("pos", num("[%.2f,%.2f,%.2f]", pos.x, pos.y, pos.z));
        add("heading", num("%.1f", pPlayer->GetHeading() * 180.0f / 3.14159265f));
        add("health", num("%.1f", pPlayer->m_fHealth));
        add("armour", num("%.1f", pPlayer->m_fArmour));
        add("interior", num("%d", pPlayer->m_nAreaCode));
        CWanted* pWanted = FindPlayerWanted(0);
        add("wanted", num("%d", pWanted ? static_cast<int>(pWanted->m_nWantedLevel) : -1));

        CVehicle* pVehicle = pPlayer->m_nPedFlags.bInVehicle ? pPlayer->m_pVehicle : nullptr;
        if (pVehicle)
        {
            std::string seat = "\"passenger\"";
            if (pVehicle->m_pDriver == pPlayer)
            {
                seat = "\"driver\"";
            }
            else
            {
                for (int i = 0; i < 8; i++)
                {
                    if (pVehicle->m_apPassengers[i] == pPlayer)
                    {
                        seat = num("\"passenger %d\"", i);
                    }
                }
            }
            CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicle);
            add("vehicle", "{\"model\":" + num("%d", pVehicle->m_nModelIndex) + ",\"seat\":" + seat + ",\"id\":" +
                               (pNetworkVehicle ? JsonString(to_string(pNetworkVehicle->m_vehicleId)) : "null") +
                               ",\"kmh\":" + num("%.1f", pVehicle->m_vecMoveSpeed.Magnitude() * 50.0f * 3.6f) + "}");
        }
        else
        {
            add("vehicle", "null");
        }
    }

    add("onMission", boolean(CTheScripts::IsPlayerOnAMission()));
    add("clock", num("\"%02d:%02d\"", CClock::ms_nGameClockHours, CClock::ms_nGameClockMinutes));
    add("fps", num("%.0f", CTimer::game_FPS));
    add("frames", CStandFrames::StateJson());

    int nPeds = 0, nOurPeds = 0;
    for (auto pNetworkPed : CNetworkPedManager::m_pPeds)
    {
        nPeds++;
        nOurPeds += pNetworkPed->m_bSyncing;
    }
    int nVehicles = 0, nOurVehicles = 0;
    for (auto pNetworkVehicle : CNetworkVehicleManager::m_pVehicles)
    {
        nVehicles++;
        nOurVehicles += pNetworkVehicle->m_bSyncing;
    }
    add("netPeds", num("{\"total\":%d,\"ours\":%d}", nPeds, nOurPeds));
    add("netVehicles", num("{\"total\":%d,\"ours\":%d}", nVehicles, nOurVehicles));
    add("pools", num("{\"peds\":%d,\"vehicles\":%d,\"objects\":%d}", CPools::ms_pPedPool->GetNoOfUsedSpaces(),
                     CPools::ms_pVehiclePool->GetNoOfUsedSpaces(), CPools::ms_pObjectPool->GetNoOfUsedSpaces()));

    return json + "}";
}
